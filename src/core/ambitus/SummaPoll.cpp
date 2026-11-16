#include "SummaPoll.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <glib.h>

#include "control/Control.h"
#include "control/jobs/ImageExport.h"
#include "control/jobs/ProgressListener.h"
#include "control/settings/Settings.h"
#include "library/InkReader.h"
#include "model/Document.h"
#include "util/PathUtil.h"     // for fromGFilename, toGFilename
#include "util/StringUtils.h"  // for char_cast
#include "util/Util.h"         // for execInUiThread
#include "util/raii/GObjectSPtr.h"

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
/// What Summa gets for a request
struct Snapshot {
    fs::path notebook;  ///< empty when the notebook was never saved
    size_t page = 0;
    std::string name;
    std::string png;
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
/// be drawn.
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
    const size_t page = ctrl->getCurrentPageNo();
    if (!active || page >= pages) {
        return std::nullopt;
    }
    auto png = renderCurrentPage(doc, page);
    if (!png || png->empty()) {
        return std::nullopt;
    }
    Snapshot snap;
    snap.notebook = file;
    snap.page = page;
    snap.name = file.empty() ? std::string() : std::string(char_cast(file.stem().u8string()));
    snap.png = std::move(*png);
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
    const std::string json = *snap ? library::pageAnswerJson(id, std::string(char_cast((*snap)->notebook.u8string())),
                                                             (*snap)->page, (*snap)->name, (*snap)->png) :
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

/// The PNG of the page, drawn from the document in memory by the export of the program (what --create-img uses), at
/// the resolution Summa's tutor reads pages at, with the paper and its ruling as the person sees them: opaque.
auto renderCurrentPage(Document* doc, size_t page) -> std::optional<std::string> {
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
