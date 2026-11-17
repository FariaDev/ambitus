#include "BoardShift.h"

#include <algorithm>
#include <utility>

#include "control/Control.h"
#include "control/zoom/ZoomControl.h"
#include "gui/Layout.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "undo/UndoRedoHandler.h"
#include "util/Util.h"

namespace ambitus::board {

void moveContent(Document& doc, const PageRef& page, double dx, double dy, Control* control) {
    doc.lock();
    for (Layer* layer: page->getLayers()) {
        for (auto& e: layer->getElements()) {
            e->move(dx, dy);
        }
    }
    Document::setPageSize(page, page->getWidth() + dx, page->getHeight() + dy);
    const size_t pageNr = doc.indexOf(page);
    doc.unlock();
    if (!control) {
        return;
    }
    control->firePageSizeChanged(pageNr);

    // once the view has its new size, scroll by as much as the content moved; that scroll is not a pan of the person
    const double zoom = control->getZoomControl()->getZoom();  // pixels per point
    Util::execInUiThread([control, x = dx * zoom, y = dy * zoom]() {
        if (auto* win = control->getWindow()) {
            Layout* layout = win->getXournal()->getLayout();
            Layout::ProgrammaticScroll shift(*layout);
            layout->scrollRelative(x, y);
        }
    });
}

void moveAndRecord(UndoRedoHandler& history, Document& doc, const PageRef& page, double dx, double dy,
                   Control* control) {
    const bool wasChanged = history.isChanged();
    if (history.canUndo()) {
        if (auto* last = dynamic_cast<ShiftUndoAction*>(history.lastUndoAction())) {
            last->addMove(dx, dy);
        } else {
            UndoActionPtr cause = history.takeLastUndoAction();
            history.addUndoAction(std::make_unique<ShiftUndoAction>(doc, page, dx, dy, std::move(cause)));
        }
    }
    moveContent(doc, page, dx, dy, control);
    if (!wasChanged) {
        // the top of the history is a new object now: keep the document saved, the page is what was saved
        history.documentSaved();
    }
}

ShiftUndoAction::ShiftUndoAction(Document& doc, PageRef page, double dx, double dy, std::unique_ptr<UndoAction> cause):
        UndoAction("ambitus::board::ShiftUndoAction"), doc(&doc), dx(dx), dy(dy), cause(std::move(cause)) {
    this->page = std::move(page);
}

void ShiftUndoAction::addMove(double x, double y) {
    dx += x;
    dy += y;
}

auto ShiftUndoAction::undo(Control* control) -> bool {
    moveContent(*doc, page, -dx, -dy, control);
    return !cause || cause->undo(control);
}

auto ShiftUndoAction::redo(Control* control) -> bool {
    bool ok = !cause || cause->redo(control);
    moveContent(*doc, page, dx, dy, control);
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
