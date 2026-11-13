#include "ConvertInk.h"

#include <memory>
#include <thread>
#include <utility>

#include <cairo.h>

#include "control/Control.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "library/InkReader.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Color.h"
#include "util/Util.h"  // for DPI_NORMALIZATION_FACTOR, execInUiThread
#include "util/i18n.h"
#include "view/ElementContainerView.h"
#include "view/View.h"

namespace ambitus {

namespace {
/// What the model sees: the selection on white, at about the resolution of a scan
constexpr double SELECTION_DPI = 200.0;

auto messageFor(library::InkStop stop) -> std::string {
    using library::InkStop;
    switch (stop) {
        case InkStop::NoSumma:
            return _("Converting uses Summa: open Summa and try again.");
        case InkStop::Disabled:
            return _("Switch on “Search the handwriting too” in the settings of Summa to convert.");
        case InkStop::NoModel:
        case InkStop::NoVision:
        case InkStop::Free:
            return _("The model chosen in Summa does not read images, or is free.");
        case InkStop::Claude:
            return _("The handwriting is not read with the Claude engine of Summa yet.");
        case InkStop::Quota:
            return _("The quota of the model is over for now.");
        case InkStop::Busy:
            return _("Summa is reading another page. Try again in a moment.");
        default:
            return _("The reading failed. Try again.");
    }
}

auto pngOf(EditSelection* sel) -> std::string {
    const double scale = SELECTION_DPI / Util::DPI_NORMALIZATION_FACTOR;
    const int width = std::max(1, static_cast<int>(sel->getWidth() * scale));
    const int height = std::max(1, static_cast<int>(sel->getHeight() * scale));
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, -sel->getOriginalXOnView(), -sel->getOriginalYOnView());
    xoj::view::ElementContainerView view(sel);
    view.draw(xoj::view::Context::createDefault(cr));
    cairo_destroy(cr);
    std::string png;
    cairo_surface_write_to_png_stream(
            surface,
            +[](void* closure, const unsigned char* data, unsigned int length) -> cairo_status_t {
                static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
                return CAIRO_STATUS_SUCCESS;
            },
            &png);
    cairo_surface_destroy(surface);
    return png;
}

/// What is needed to put the text where the strokes were, once the answer comes
struct Pending {
    EditSelection* sel;  ///< compared to the selection then, never dereferenced unless it is still the one
    PageRef page;
    double x, y;
};

void place(Control* ctrl, const Pending& p, std::string text) {
    XournalView* xournal = ctrl->getWindow()->getXournal();
    Document* doc = ctrl->getDocument();
    doc->lock_shared();
    const size_t pageNr = doc->indexOf(p.page);
    doc->unlock_shared();
    XojPageView* view = pageNr == npos ? nullptr : xournal->getViewFor(pageNr);
    if (!view) {
        return;  // the page is gone
    }
    // still the same selection: the text takes the place of the strokes; otherwise it goes beside them
    if (xournal->getSelection() == p.sel) {
        xournal->deleteSelection(p.sel);
    }
    auto t = std::make_unique<Text>();
    t->setColor(Colors::black);
    t->setFont(ctrl->getSettings()->getFont());
    t->setText(std::move(text));
    t->setTransformation(xoj::util::Matrix::TRANSLATION(p.x, p.y));

    Layer* layer = p.page->getSelectedLayer();
    ctrl->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(p.page, layer, t.get()));
    auto sel = SelectionFactory::createFromFloatingElement(ctrl, p.page, layer, view, std::move(t));
    xournal->setSelection(sel.release());
}
}  // namespace

auto tidyReading(std::string text) -> std::string {
    auto trim = [](std::string& s) {
        const auto first = s.find_first_not_of(" \t\r\n");
        const auto last = s.find_last_not_of(" \t\r\n");
        s = first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
    };
    trim(text);
    if (text.rfind("```", 0) == 0) {
        const auto lineEnd = text.find('\n');
        const auto fence = text.rfind("```");
        if (lineEnd != std::string::npos && fence != std::string::npos && fence > lineEnd) {
            text = text.substr(lineEnd + 1, fence - lineEnd - 1);
            trim(text);
        }
    }
    return text;
}

void convertSelectionToText(Control* ctrl) {
    MainWindow* win = ctrl->getWindow();
    EditSelection* sel = win ? win->getXournal()->getSelection() : nullptr;
    bool strokes = false;
    if (sel) {
        for (const Element* e: sel->getElementsView()) {
            strokes = strokes || e->getType() == ELEMENT_STROKE;
        }
    }
    if (!strokes) {
        win->showToast(_("Select the handwriting with the lasso first."));
        return;
    }
    auto bridge = library::findSummaBridge();
    if (!bridge) {
        win->showToast(messageFor(library::InkStop::NoSumma));
        return;
    }
    Pending pending{sel, sel->getSourcePage(), sel->getXOnView(), sel->getYOnView()};
    std::string png = pngOf(sel);
    win->showToast(_("Reading the handwriting…"), true);
    std::thread([ctrl, bridge = *bridge, png = std::move(png), pending] {
        auto reply = library::postInk(bridge, png, std::chrono::seconds(120));
        Util::execInUiThread([ctrl, reply = std::move(reply), pending] {
            MainWindow* win = ctrl->getWindow();
            if (!win) {
                return;
            }
            if (reply.stop != library::InkStop::None) {
                win->showToast(messageFor(reply.stop));
                return;
            }
            std::string text = tidyReading(reply.text);
            if (text.empty()) {
                win->showToast(_("Nothing legible in the selection."));
                return;
            }
            win->showToast(_("Converted. Ctrl+Z brings the strokes back."));
            place(ctrl, pending, std::move(text));
        });
    }).detach();
}

}  // namespace ambitus
