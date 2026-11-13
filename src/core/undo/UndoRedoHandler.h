/*
 * Xournal++
 *
 * Handles Undo and Redo
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <deque>   // for deque
#include <string>  // for string
#include <vector>  // for vector

#include "model/PageRef.h"  // for PageRef

#include "UndoAction.h"  // for UndoActionPtr

class Control;

class UndoRedoListener {
public:
    virtual void undoRedoChanged() = 0;
    virtual void undoRedoPageChanged(PageRef page) = 0;

    virtual ~UndoRedoListener() = default;
};

class UndoRedoHandler {
public:
    explicit UndoRedoHandler(Control* control);
    virtual ~UndoRedoHandler();

    void undo();
    void redo();

    bool canUndo();
    bool canRedo();

    void addUndoAction(UndoActionPtr action);
    /// Ambitus: takes the last action out of the history (null when empty), to put it back wrapped
    UndoActionPtr takeLastUndoAction();

    /// Ambitus: the history of a document kept aside while another one is in the window (tabs)
    struct State {
        std::deque<UndoActionPtr> undoList;
        std::deque<UndoActionPtr> redoList;
        UndoAction* savedUndo = nullptr;
        UndoAction* autosavedUndo = nullptr;
        /// whether the document had changes since it was saved
        bool isChanged() const;
        /// whether the document had changes since it was autosaved
        bool isChangedAutosave() const;
        /// marks the document as autosaved as it is now
        void autosaved();
    };
    /// Takes the whole history out, leaving the handler empty
    State takeState();
    /// Puts a history taken by takeState back
    void restoreState(State state);

    std::string undoDescription();
    std::string redoDescription();

    void clearContents();

    void fireUpdateUndoRedoButtons(const std::vector<PageRef>& pages);
    void addUndoRedoListener(UndoRedoListener* listener);

    bool isChanged();
    bool isChangedAutosave();
    void documentAutosaved();
    void documentSaved();

private:
    void clearRedo();
    void printContents();

private:
    std::deque<UndoActionPtr> undoList;
    std::deque<UndoActionPtr> redoList;

    UndoAction* savedUndo = nullptr;
    UndoAction* autosavedUndo = nullptr;

    std::vector<UndoRedoListener*> listener;

    Control* control = nullptr;
};
