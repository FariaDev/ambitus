/*
 * Ambitus
 *
 * "Rename": a small card over the window, in the mold of the "Keep draft" one. It shows the name of a notebook or a
 * board with the part before ".xopp" selected, what is wrong with a new name under the field, and the subject it is
 * in. Enter renames, Esc or Cancel gives up. The card only asks: what it got goes to the callback, and MainWindow
 * moves the notebook (ambitus/Rename.h).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>

#include <gtk/gtk.h>

#include "filesystem.h"

namespace ambitus {

class RenameCard {
public:
    /// overlay: the one over the whole window, so the card shows over the library and over the page alike
    RenameCard(GtkOverlay* overlay, GtkWindow* window);
    ~RenameCard();
    RenameCard(const RenameCard&) = delete;
    RenameCard& operator=(const RenameCard&) = delete;

    /// Opens for the notebook `file` (a board when `board`). onRename gets the name typed, cleaned and checked (a name
    /// that is not taken in the folder), once, after the card is gone; Esc, Cancel or a click outside calls onCancel.
    void open(const fs::path& file, bool board, std::function<void(const std::string&)> onRename,
              std::function<void()> onCancel = {});
    void close();
    bool isOpen() const;

private:
    void close(bool restoreFocus);
    void reveal(GtkWidget* focus);
    void rename();
    void refresh();
    std::string typedName() const;
    bool onKey(GtkWidget* from, GdkEventKey* e);
    void watchKeys(GtkWidget* w);

    GtkWindow* window;
    GtkWidget* revealer = nullptr;
    GtkWidget* card = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* entry = nullptr;
    GtkWidget* where = nullptr;
    GtkWidget* problem = nullptr;
    GtkWidget* cancelButton = nullptr;
    GtkWidget* renameButton = nullptr;

    fs::path file;
    bool board = false;
    bool valid = false;
    GtkWidget* focusBack = nullptr;  ///< what had the focus before; weak
    std::function<void(const std::string&)> onRename;
    std::function<void()> onCancel;
};

}  // namespace ambitus
