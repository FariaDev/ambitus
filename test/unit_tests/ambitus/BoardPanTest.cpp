/*
 * Ambitus
 *
 * This file is part of the Ambitus UnitTests
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "ambitus/Board.h"
#include "ambitus/BoardShift.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "undo/UndoAction.h"
#include "undo/UndoRedoHandler.h"

using namespace ambitus::board;

namespace {

/// A stroke written on the board, undoable: undo takes it off the page, redo puts it back. It needs no control.
class WriteStrokeAction: public UndoAction {
public:
    WriteStrokeAction(PageRef page, Layer* layer, const Element* stroke):
            UndoAction("WriteStrokeAction"), layer(layer), element(stroke) {
        this->page = std::move(page);
    }

    bool undo(Control*) override {
        elementOwn = layer->removeElement(element).e;
        return elementOwn != nullptr;
    }

    bool redo(Control*) override {
        layer->addElement(std::move(elementOwn));
        return true;
    }

    std::string getText() override { return "stroke"; }

private:
    Layer* layer;
    const Element* element;
    ElementPtr elementOwn;
};

std::unique_ptr<Stroke> makeStroke(double x, double y) {
    auto s = std::make_unique<Stroke>();
    s->setWidth(2);
    s->addPoint(Point(x, y));
    s->addPoint(Point(x + 60, y + 20));
    return s;
}

class BoardPan: public ::testing::Test {
protected:
    BoardPan(): doc(&handler) {
        page = std::make_shared<XojPage>(2000, 1500);
        page->setBackgroundType(pageType());
        layer = page->getSelectedLayer();
        // written before any action: nothing in the history refers to it
        base = makeStroke(100, 100).release();
        layer->addElement(ElementPtr(base));
        doc.addPage(page);
    }

    /// Where the first point of the stroke is, in page points
    static double xOf(const Element* e) { return static_cast<const Stroke*>(e)->getPoint(0).x; }
    static double yOf(const Element* e) { return static_cast<const Stroke*>(e)->getPoint(0).y; }

    bool onPage(const Element* e) const {
        for (const Element* el: layer->getElementsView()) {
            if (el == e) {
                return true;
            }
        }
        return false;
    }

    /// Writes a stroke and records it as the action of the person
    const Element* write(UndoRedoHandler& history, double x, double y) {
        auto s = makeStroke(x, y);
        const Element* raw = s.get();
        layer->addElement(std::move(s));
        history.addUndoAction(std::make_unique<WriteStrokeAction>(page, layer, raw));
        return raw;
    }

    DocumentHandler handler;
    Document doc;
    PageRef page;
    Layer* layer = nullptr;
    Element* base = nullptr;
};

}  // namespace

TEST(BoardGeometry, farFromTheEdgesNothingMoves) {
    const Size page{10000, 8000};
    const Range view(2000, 2000, 3000, 2700);
    auto g = growAhead(page, view);
    EXPECT_DOUBLE_EQ(g.width, page.width);
    EXPECT_DOUBLE_EQ(g.height, page.height);
    auto m = moveAhead(page, view);
    EXPECT_DOUBLE_EQ(m.width, 0);
    EXPECT_DOUBLE_EQ(m.height, 0);
}

TEST(BoardGeometry, nearTheRightEdgeTheBoardGrowsOneViewAhead) {
    // the view is 1000 pt wide and ends 500 pt before the right edge: one view must be ahead of it
    const Range view(1500, 2000, 2500, 2700);
    auto g = growAhead(Size{3000, 8000}, view);
    EXPECT_DOUBLE_EQ(std::fmod(g.width, RASTER), 0.0);
    EXPECT_GE(g.width - view.maxX, view.getWidth());
    EXPECT_DOUBLE_EQ(g.width, 3510);
    EXPECT_DOUBLE_EQ(g.height, 8000);
    // no move: the content is far from the left
    auto m = moveAhead(Size{3000, 8000}, view);
    EXPECT_DOUBLE_EQ(m.width, 0);
}

TEST(BoardGeometry, nearTheBottomEdgeTheBoardGrowsOneViewAhead) {
    const Range view(2000, 1800, 3000, 2500);  // 700 pt high, 500 pt before the bottom
    auto g = growAhead(Size{10000, 3000}, view);
    EXPECT_DOUBLE_EQ(g.height, 3204);
    EXPECT_DOUBLE_EQ(std::fmod(g.height, RASTER), 0.0);
    EXPECT_DOUBLE_EQ(g.width, 10000);
}

TEST(BoardGeometry, nearTheLeftOrTopEdgeTheContentMovesInWholeDots) {
    // scrolled to 10 pt before the left edge of the page: one view of room is needed on that side
    const Range view(-10, 2000, 990, 2700);
    auto m = moveAhead(Size{10000, 8000}, view);
    EXPECT_DOUBLE_EQ(m.width, 1026);
    EXPECT_DOUBLE_EQ(std::fmod(m.width, RASTER), 0.0);
    EXPECT_GE(view.minX + m.width, view.getWidth());
    EXPECT_DOUBLE_EQ(m.height, 0);
    EXPECT_DOUBLE_EQ(growAhead(Size{10000, 8000}, view).width, 10000);  // the right edge is far

    // and at the top
    const Range top(2000, -5, 3000, 695);
    m = moveAhead(Size{10000, 8000}, top);
    EXPECT_DOUBLE_EQ(m.height, 720);
    EXPECT_DOUBLE_EQ(m.width, 0);
}

TEST(BoardGeometry, zoomedOutTheRoomAheadIsAtMostAStartingBoard) {
    // zoomed out, a view covers some 20000 pt: the room is still only a starting board (1602 pt), not a view
    auto g = growAhead(Size{3000, 3000}, Range(0, 0, 20000, 2000));
    EXPECT_DOUBLE_EQ(g.width, 21618);
    auto m = moveAhead(Size{10000, 8000}, Range(-10, 2000, 15000, 2700));
    EXPECT_DOUBLE_EQ(m.width, 1620);
}

TEST(BoardGeometry, theBoardNeverGrowsOrMovesPastItsCap) {
    // at the cap, a view that reaches the right edge grows nothing
    auto g = growAhead(Size{MAX_SIDE, 8000}, Range(MAX_SIDE - 1500, 2000, MAX_SIDE - 500, 2700));
    EXPECT_DOUBLE_EQ(g.width, MAX_SIDE);
    // just short of it, the growth stops at the cap rather than past it
    g = growAhead(Size{MAX_SIDE - RASTER, 8000}, Range(MAX_SIDE - 1500, 2000, MAX_SIDE - 500, 2700));
    EXPECT_DOUBLE_EQ(g.width, MAX_SIDE);

    // at the cap, a move on the left makes no room
    auto m = moveAhead(Size{MAX_SIDE, 8000}, Range(-10, 2000, 990, 2700));
    EXPECT_DOUBLE_EQ(m.width, 0);
    // with 40 pt left under the cap, the move is 36 pt: whole dots, inside the cap
    m = moveAhead(Size{MAX_SIDE - 40, 8000}, Range(-10, 2000, 990, 2700));
    EXPECT_DOUBLE_EQ(m.width, 36);

    EXPECT_DOUBLE_EQ(roomWithinCap(1000, 0), 0);
    EXPECT_DOUBLE_EQ(roomWithinCap(1000, -5), 0);
    EXPECT_DOUBLE_EQ(roomWithinCap(1000, 90), 90);
    EXPECT_DOUBLE_EQ(roomWithinCap(MAX_SIDE, 90), 0);

    // writing beyond the cap does not grow past it either
    EXPECT_DOUBLE_EQ(grownToFit(Size{START_WIDTH, START_HEIGHT}, MAX_SIDE, 0).width, MAX_SIDE);
}

TEST(BoardGeometry, theCapIsAWholeNumberOfDots) { EXPECT_DOUBLE_EQ(std::fmod(MAX_SIDE, RASTER), 0.0); }

TEST(BoardGeometry, aMoveWaitsForAnEmptyRedoHistoryAndNothingSelected) {
    EXPECT_TRUE(mayMoveContent(false, false));
    EXPECT_FALSE(mayMoveContent(false, true));  // undone actions wait to be redone at the old coordinates
    EXPECT_FALSE(mayMoveContent(true, false));  // a selection or a text box holds coordinates
}

TEST_F(BoardPan, undoTakesTheStrokeAndThePanBackInOnePress) {
    UndoRedoHandler history(nullptr);
    const Element* written = write(history, 700, 600);

    // the view went past the left and the top: everything moves right and down, the page grows as much
    moveAndRecord(history, doc, page, 180, 90, nullptr);
    EXPECT_DOUBLE_EQ(page->getWidth(), 2180);
    EXPECT_DOUBLE_EQ(page->getHeight(), 1590);
    EXPECT_DOUBLE_EQ(xOf(base), 280);
    EXPECT_DOUBLE_EQ(yOf(written), 690);

    // one undo: the stroke is gone and what was there before it is where it was before it
    ASSERT_TRUE(history.canUndo());
    history.undo();
    EXPECT_FALSE(onPage(written));
    EXPECT_FALSE(history.canUndo());
    EXPECT_DOUBLE_EQ(xOf(base), 100);
    EXPECT_DOUBLE_EQ(yOf(base), 100);
    EXPECT_DOUBLE_EQ(page->getWidth(), 2000);
    EXPECT_DOUBLE_EQ(page->getHeight(), 1500);

    // redo: the stroke is back where it was written, in the moved board
    ASSERT_TRUE(history.canRedo());
    history.redo();
    EXPECT_TRUE(onPage(written));
    EXPECT_DOUBLE_EQ(xOf(written), 880);
    EXPECT_DOUBLE_EQ(yOf(written), 690);
    EXPECT_DOUBLE_EQ(xOf(base), 280);
    EXPECT_DOUBLE_EQ(yOf(base), 190);
    EXPECT_FALSE(history.canRedo());
}

TEST_F(BoardPan, severalPansAfterAnEditAreOneUndo) {
    UndoRedoHandler history(nullptr);
    const Element* written = write(history, 700, 600);
    moveAndRecord(history, doc, page, 180, 90, nullptr);
    moveAndRecord(history, doc, page, 36, 0, nullptr);
    moveAndRecord(history, doc, page, 0, 18, nullptr);

    history.undo();
    EXPECT_FALSE(onPage(written));
    EXPECT_FALSE(history.canUndo());
    EXPECT_DOUBLE_EQ(xOf(base), 100);
    EXPECT_DOUBLE_EQ(yOf(base), 100);
    EXPECT_DOUBLE_EQ(page->getWidth(), 2000);
    EXPECT_DOUBLE_EQ(page->getHeight(), 1500);

    history.redo();
    EXPECT_DOUBLE_EQ(xOf(written), 700 + 180 + 36);
    EXPECT_DOUBLE_EQ(yOf(written), 600 + 90 + 18);
    EXPECT_DOUBLE_EQ(xOf(base), 100 + 180 + 36);
    EXPECT_DOUBLE_EQ(yOf(base), 100 + 90 + 18);
}

TEST_F(BoardPan, withAnEmptyHistoryAPanNeedsNoUndoEntry) {
    UndoRedoHandler history(nullptr);
    moveAndRecord(history, doc, page, 180, 90, nullptr);
    EXPECT_FALSE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
    // the content and the page moved all the same
    EXPECT_DOUBLE_EQ(xOf(base), 280);
    EXPECT_DOUBLE_EQ(page->getWidth(), 2180);

    // a later edit is undone without touching the pan
    const Element* written = write(history, 700, 600);
    history.undo();
    EXPECT_FALSE(onPage(written));
    EXPECT_DOUBLE_EQ(xOf(base), 280);
}

TEST_F(BoardPan, aPanIsNotAnEditOfASavedBoard) {
    UndoRedoHandler history(nullptr);
    write(history, 700, 600);
    history.documentSaved();
    ASSERT_FALSE(history.isChanged());

    moveAndRecord(history, doc, page, 180, 90, nullptr);
    EXPECT_FALSE(history.isChanged());  // the window does not ask to save a pan

    // an edit after the pan is a change; undoing it is back to the saved board
    write(history, 300, 300);
    EXPECT_TRUE(history.isChanged());
    history.undo();
    EXPECT_FALSE(history.isChanged());
}
