#include "SummaPoll.h"

#include <algorithm>  // for max, min
#include <atomic>
#include <chrono>
#include <cmath>  // for lround
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include <cairo.h>
#include <glib.h>

#include "control/Control.h"
#include "control/jobs/ImageExport.h"
#include "control/jobs/ProgressListener.h"
#include "control/settings/Settings.h"
#include "library/InkReader.h"
#include "model/Document.h"
#include "model/PageRef.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfPage.h"
#include "util/PathUtil.h"     // for fromGFilename, toGFilename
#include "util/StringUtils.h"  // for char_cast
#include "util/Util.h"         // for execInUiThread, DPI_NORMALIZATION_FACTOR
#include "util/raii/GObjectSPtr.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundView.h"

#include "SendPage.h"  // for choosePage, currentSendMode, wireCrop
#include "filesystem.h"

namespace ambitus {

using namespace std::chrono_literals;

struct SummaPoller::Shared {
    Control* ctrl = nullptr;
    /// Written on the UI thread only (the destructor); read by the UI callbacks, which also run there
    bool alive = true;
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::condition_variable wake;
    xoj::util::GObjectSPtr<GCancellable> cancel{g_cancellable_new(), xoj::util::adopt};
};

namespace {
/// Summa takes a page up to 8 MB; the PNG goes in base64 (a third more), so a part is drawn smaller past this
constexpr size_t PART_PNG_LIMIT = 5 * 1024 * 1024;

/// What Summa gets for a request
struct Snapshot {
    fs::path notebook;  ///< empty when the notebook was never saved
    size_t page = 0;
    std::string name;
    std::string png;
    std::optional<library::PageCrop> crop;  ///< the part the PNG shows, when it is a part
};

auto readFileText(const fs::path& file) -> std::optional<std::string> {
    gchar* data = nullptr;
    gsize len = 0;
    if (!g_file_get_contents(Util::toGFilename(file).c_str(), &data, &len, nullptr)) {
        return std::nullopt;
    }
    std::string text(data, len);
    g_free(data);
    return text;
}

/// UI thread. Nothing when no notebook is open in the window (the library, or the blank start) or the page could not
/// be drawn. What is sent follows the setting, as the button "Send to Summa" does (a lasso selection goes alone).
auto snapshotCurrentPage(Control* ctrl) -> std::optional<Snapshot> {
    bool active = false;
    for (const auto& tab: ctrl->getTabs()) {
        active = active || tab.active;
    }
    Document* doc = ctrl->getDocument();
    doc->lock_shared();
    const size_t pages = doc->getPageCount();
    const fs::path file = doc->getFilepath();
    doc->unlock_shared();
    if (!active) {
        return std::nullopt;
    }
    const summacrop::Pick what = choosePage(ctrl, true, currentSendMode(ctrl));
    if (what.page >= pages) {
        return std::nullopt;
    }
    // a lasso selection keeps its elements out of the layers while it lasts: they go back before the page is drawn
    ctrl->clearSelectionEndText();
    auto png = renderCurrentPage(doc, what.page, what.crop);
    if (!png || png->empty()) {
        return std::nullopt;
    }
    Snapshot snap;
    snap.notebook = file;
    snap.page = what.page;
    snap.name = file.empty() ? std::string() : std::string(char_cast(file.stem().u8string()));
    snap.png = std::move(*png);
    if (what.crop) {
        snap.crop = wireCrop(what.crop);
    }
    return snap;
}

/// Runs fn on the UI thread and waits for it, giving up when the poller is stopping or the time is up
template <typename T>
auto onUiThread(const std::shared_ptr<SummaPoller::Shared>& shared, std::function<T()> fn,
                std::chrono::milliseconds limit) -> std::optional<T> {
    auto promise = std::make_shared<std::promise<std::optional<T>>>();
    auto future = promise->get_future();
    Util::execInUiThread([shared, promise, fn = std::move(fn)] {
        if (!shared->alive) {
            promise->set_value(std::nullopt);
            return;
        }
        promise->set_value(fn());
    });
    const auto until = std::chrono::steady_clock::now() + limit;
    while (future.wait_for(100ms) != std::future_status::ready) {
        if (shared->stop || std::chrono::steady_clock::now() > until) {
            return std::nullopt;
        }
    }
    return future.get();
}

/// Sleeps, unless the poller is stopped meanwhile; true when it is
auto sleepOrStop(SummaPoller::Shared& shared, std::chrono::seconds time) -> bool {
    std::unique_lock lock(shared.mutex);
    return shared.wake.wait_for(lock, time, [&] { return shared.stop.load(); });
}

void answerRequest(const std::shared_ptr<SummaPoller::Shared>& shared, const library::SummaBridge& bridge,
                   const std::string& id) {
    Control* ctrl = shared->ctrl;
    auto snap = onUiThread<std::optional<Snapshot>>(shared, [ctrl] { return snapshotCurrentPage(ctrl); }, 4s);
    if (!snap) {
        return;  // the window is closing, or the UI did not answer: Summa gives up on its own
    }
    const std::string json =
            *snap ? library::pageAnswerJson(id, std::string(char_cast((*snap)->notebook.u8string())), (*snap)->page,
                                            (*snap)->name, (*snap)->png, (*snap)->crop) :
                    library::noPageAnswerJson(id);
    library::postPageAnswer(bridge, json, shared->cancel.get());
}

void run(const std::shared_ptr<SummaPoller::Shared>& shared) {
    Control* ctrl = shared->ctrl;
    int failures = 0;
    while (!shared->stop) {
        auto enabled = onUiThread<bool>(shared, [ctrl] { return ctrl->getSettings()->getAmbitusSummaBridge(); }, 10s);
        if (!enabled || !*enabled) {
            if (sleepOrStop(*shared, 5s)) {
                return;
            }
            continue;
        }
        auto bridge = library::findSummaBridge();
        library::PollReply reply = bridge ? library::pollPageRequest(*bridge, shared->cancel.get()) :
                                            library::PollReply{library::PollKind::NoSumma, {}};
        switch (reply.kind) {
            case library::PollKind::Idle:
                failures = 0;
                break;
            case library::PollKind::Request:
                failures = 0;
                answerRequest(shared, *bridge, reply.id);
                break;
            default:
                if (sleepOrStop(*shared, library::pollBackoff(++failures))) {
                    return;
                }
                break;
        }
    }
}
}  // namespace

namespace {
/// Appends the bytes of a PNG to the string it is written into
cairo_status_t appendPng(void* closure, const unsigned char* data, unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

/// The PNG of a part of the page at `scale` pixels per point. The part is drawn as the export draws a page (the PDF
/// background, the paper, the ruling and the layers), opaque: the paper is painted under the ink.
auto drawPart(Document* doc, size_t page, const summacrop::Rect& part, double scale) -> std::optional<std::string> {
    const int width = std::max(1, static_cast<int>(std::lround(part.width * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(part.height * scale)));
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return std::nullopt;
    }
    cairo_t* cr = cairo_create(surface);
    bool drawn = false;
    {
        std::shared_lock<Document> lock(*doc);
        ConstPageRef pageRef = doc->getPage(page);
        // the page's own coordinates in, the corner of the part at the corner of the picture
        cairo_scale(cr, scale, scale);
        cairo_translate(cr, -part.x, -part.y);
        bool pdfMissing = false;  // a PDF page that is missing would leave a hole in the picture: no picture then
        if (pageRef->getBackgroundType().isPdfPage()) {
            XojPdfPageSPtr popplerPage = doc->getPdfPage(pageRef->getPdfPageNr());
            pdfMissing = !popplerPage;
            if (popplerPage) {
                popplerPage->render(cr);
            }
        }
        if (!pdfMissing) {
            xoj::view::BackgroundFlags flags;
            flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;  // drawn above
            flags.showImage = xoj::view::SHOW_IMAGE_BACKGROUND;
            flags.showRuling = xoj::view::SHOW_RULING_BACKGROUND;
            DocumentView view;
            view.drawPage(pageRef, cr, true /* dont render eraseable */, flags);
            drawn = true;
        }
    }
    cairo_destroy(cr);
    std::optional<std::string> png;
    if (drawn) {
        std::string out;
        if (cairo_surface_write_to_png_stream(surface, appendPng, &out) == CAIRO_STATUS_SUCCESS) {
            png = std::move(out);
        }
    }
    cairo_surface_destroy(surface);
    return png;
}

/// The PNG of a part: at the resolution Summa's tutor reads pages at, or smaller when the longest side would pass
/// summacrop::PART_MAX_SIDE, and smaller again while the PNG would pass PART_PNG_LIMIT
auto renderPart(Document* doc, size_t page, const summacrop::Rect& part) -> std::optional<std::string> {
    double scale = library::PAGE_DPI / Util::DPI_NORMALIZATION_FACTOR;
    scale = std::min(scale, summacrop::PART_MAX_SIDE / std::max(part.width, part.height));
    std::optional<std::string> png;
    for (int attempt = 0; attempt < 6; attempt++) {
        png = drawPart(doc, page, part, scale);
        if (!png || png->size() <= PART_PNG_LIMIT) {
            break;
        }
        scale *= 0.7;
    }
    return png;
}
}  // namespace

/// The PNG of the page, drawn from the document in memory by the export of the program (what --create-img uses), at
/// the resolution Summa's tutor reads pages at, with the paper and its ruling as the person sees them: opaque. With a
/// part (`crop`), only that part is drawn, at the same resolution.
auto renderCurrentPage(Document* doc, size_t page, const std::optional<summacrop::Rect>& crop)
        -> std::optional<std::string> {
    if (crop) {
        return renderPart(doc, page, *crop);
    }
    {
        // a page too big for the export (a board grown far) goes whole too, drawn smaller as a part is
        doc->lock_shared();
        const double width = doc->getPage(page)->getWidth();
        const double height = doc->getPage(page)->getHeight();
        doc->unlock_shared();
        if (std::max(width, height) * library::PAGE_DPI / Util::DPI_NORMALIZATION_FACTOR > summacrop::PART_MAX_SIDE) {
            return renderPart(doc, page, summacrop::Rect{0, 0, width, height});
        }
    }
    gchar* dir = g_dir_make_tmp("ambitus-check-XXXXXX", nullptr);
    if (!dir) {
        return std::nullopt;
    }
    const fs::path tmp = Util::fromGFilename(dir);
    g_free(dir);
    const fs::path out = tmp / "page.png";
    std::optional<std::string> png;
    {
        PageRangeVector range;
        range.emplace_back(page, page);
        DummyProgressListener progress;
        ImageExport exporter(doc, out, EXPORT_GRAPHICS_PNG, EXPORT_BACKGROUND_ALL, range);
        exporter.setQualityParameter(EXPORT_QUALITY_DPI, library::PAGE_DPI);
        exporter.exportGraphics(&progress);
        if (exporter.getLastErrorMsg().empty()) {
            png = readFileText(out);
        }
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return png;
}

SummaPoller::SummaPoller(Control* ctrl): shared(std::make_shared<Shared>()) {
    shared->ctrl = ctrl;
    worker = std::thread([shared = this->shared] { run(shared); });
}

SummaPoller::~SummaPoller() {
    shared->alive = false;
    {
        std::lock_guard lock(shared->mutex);  // the worker may be between its check and its wait
        shared->stop = true;
    }
    g_cancellable_cancel(shared->cancel.get());
    shared->wake.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

}  // namespace ambitus
