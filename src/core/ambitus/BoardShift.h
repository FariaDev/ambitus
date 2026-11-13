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

namespace ambitus::board {

/// Moves everything on the page by (dx, dy), grows the page by as much and scrolls the view along.
/// Negative values move back and shrink.
void shiftContent(Control* control, const PageRef& page, double dx, double dy);

/// The action that brought content to the edge, followed by the move that made room for it.
class ShiftUndoAction: public UndoAction {
public:
    ShiftUndoAction(PageRef page, double dx, double dy, std::unique_ptr<UndoAction> cause);

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;
    std::vector<PageRef> getPages() override;

private:
    double dx, dy;
    std::unique_ptr<UndoAction> cause;
};

}  // namespace ambitus::board
