/*
 * Ambitus
 *
 * The command palette (Ctrl+/): a search over the top of the page that finds any
 * command of the menu by name and runs it with Enter. Only what can be done now
 * is listed; the shortcut of each command shows beside it, to be learned.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "ambitus/Commands.h"

namespace ambitus {

class CommandPalette {
public:
    CommandPalette(GtkOverlay* overlay, GtkWindow* window, std::function<GMenuModel*()> menu, GtkWidget* focusBack);
    ~CommandPalette();
    CommandPalette(const CommandPalette&) = delete;
    CommandPalette& operator=(const CommandPalette&) = delete;

    void open();
    void close();
    bool isOpen() const;

private:
    void refresh();
    void move(int delta);
    void runSelected();
    GActionGroup* groupOf(const std::string& action, std::string& name) const;

    GtkWindow* window;
    std::function<GMenuModel*()> menu;
    GtkWidget* focusBack;
    GtkWidget* frame = nullptr;
    GtkWidget* entry = nullptr;
    GtkWidget* list = nullptr;
    GtkWidget* empty = nullptr;
    std::vector<Command> commands;  ///< what can be done now, taken at open
    std::vector<size_t> shown;      ///< indices into commands, in the order of the rows
    std::string shownQuery;         ///< the text the rows are for
    guint closeIdle = 0;            ///< the close after a focus out, waiting for a click on a row
};

}  // namespace ambitus
