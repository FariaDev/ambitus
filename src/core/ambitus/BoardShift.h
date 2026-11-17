/*
 * Ambitus
 *
 * A board grows to the left and up by moving everything on it right and down
 * and growing the page by as much; the view scrolls along, so nothing seems to
 * move. The move is kept in the undo history together with the action that
 * caused it: older actions (deleted strokes, scale and rotate centres) keep
 * the coordinates they had, and undoing goes through the move first.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "model/PageRef.h"
#include "undo/UndoAction.h"

class Control;
class Document;
class UndoRedoHandler;

namespace ambitus::board {

/// Moves everything on the page by (dx, dy) and grows the page by as much. With a control, the view scrolls along.
/// Negative values move back and shrink. Not in the undo history: see moveAndRecord.
void moveContent(Document& doc, const PageRef& page, double dx, double dy, Control* control);

/// Moves the content of a board and keeps the history right: a move made after an action is folded into the move on
/// top of the history, or wraps the last action, so one undo takes back the action and the move. With an empty
/// history nothing refers to the old coordinates, so there is no entry. A move does not make a saved document
/// changed, so the history stays saved when it was. The caller checks mayMoveContent.
void moveAndRecord(UndoRedoHandler& history, Document& doc, const PageRef& page, double dx, double dy,
                   Control* control);

/// The move of the content of a board, with the action that brought the content to the edge (or the move itself)
class ShiftUndoAction: public UndoAction {
public:
    ShiftUndoAction(Document& doc, PageRef page, double dx, double dy, std::unique_ptr<UndoAction> cause);

    /// Folds another move of the same page, made after this one, into it: one undo takes back both
    void addMove(double dx, double dy);

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;
    std::vector<PageRef> getPages() override;

private:
    Document* doc;
    double dx, dy;
    std::unique_ptr<UndoAction> cause;
};

}  // namespace ambitus::board
