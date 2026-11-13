#include "BoardShift.h"

#include <algorithm>

#include "control/Control.h"
#include "control/zoom/ZoomControl.h"
#include "gui/Layout.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "util/Util.h"

namespace ambitus::board {

void shiftContent(Control* control, const PageRef& page, double dx, double dy) {
    Document* doc = control->getDocument();
    doc->lock();
    for (Layer* layer: page->getLayers()) {
        for (auto& e: layer->getElements()) {
            e->move(dx, dy);
        }
    }
    Document::setPageSize(page, page->getWidth() + dx, page->getHeight() + dy);
    const size_t pageNr = doc->indexOf(page);
    doc->unlock();
    control->firePageSizeChanged(pageNr);

    // once the view has its new size, scroll by as much as the content moved
    const double zoom = control->getZoomControl()->getZoom();  // pixels per point
    Util::execInUiThread([control, x = dx * zoom, y = dy * zoom]() {
        if (auto* win = control->getWindow()) {
            win->getXournal()->getLayout()->scrollRelative(x, y);
        }
    });
}

ShiftUndoAction::ShiftUndoAction(PageRef page, double dx, double dy, std::unique_ptr<UndoAction> cause):
        UndoAction("ambitus::board::ShiftUndoAction"), dx(dx), dy(dy), cause(std::move(cause)) {
    this->page = std::move(page);
}

auto ShiftUndoAction::undo(Control* control) -> bool {
    shiftContent(control, page, -dx, -dy);
    return !cause || cause->undo(control);
}

auto ShiftUndoAction::redo(Control* control) -> bool {
    bool ok = !cause || cause->redo(control);
    shiftContent(control, page, dx, dy);
    return ok;
}

auto ShiftUndoAction::getText() -> std::string { return cause ? cause->getText() : std::string(); }

auto ShiftUndoAction::getPages() -> std::vector<PageRef> {
    auto pages = cause ? cause->getPages() : std::vector<PageRef>{};
    if (std::find(pages.begin(), pages.end(), page) == pages.end()) {
        pages.push_back(page);
    }
    return pages;
}

}  // namespace ambitus::board
