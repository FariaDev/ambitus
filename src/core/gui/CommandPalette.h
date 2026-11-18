/*
 * Ambitus
 *
 * The command box (Ctrl+K and Ctrl+/), the palette of Summa: a light veil over
 * the window, the box at the top and what it finds in sections. It finds any
 * command of the menu by name, and the notebooks of the library, and runs it
 * with Enter. Only what can be done now is listed; the shortcut of each
 * command shows beside it, to be learned, and the options that are on show a
 * check. With nothing typed it shows the notebooks opened last.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "ambitus/Commands.h"
#include "library/LibraryView.h"

namespace ambitus {

namespace palette {
/// The fade in and out (ms), the motion tokens of Summa
constexpr int FADE_IN_MS = 180;
constexpr int FADE_OUT_MS = 120;
}  // namespace palette

class CommandPalette {
public:
    /// What the box needs of the library
    struct Notebooks {
        std::function<std::vector<library::NotebookRef>()> all;     ///< every notebook, newest first
        std::function<std::vector<library::NotebookRef>()> recent;  ///< the ones opened last, newest first
        std::function<void(const fs::path&)> open;                  ///< as the library opens one
        std::function<void(const fs::path&)> openBeside;            ///< beside the notebook in the window
    };

    CommandPalette(GtkOverlay* overlay, GtkWindow* window, std::function<GMenuModel*()> menu, GtkWidget* focusBack,
                   Notebooks notebooks);
    ~CommandPalette();
    CommandPalette(const CommandPalette&) = delete;
    CommandPalette& operator=(const CommandPalette&) = delete;

    /// beside: the palette is for the notebook to open beside the one in the window, and shows only notebooks;
    /// Alt+Enter opens beside in any case
    void open(bool beside = false);
    void close();
    bool isOpen() const;

private:
    void refresh();
    void select(int index);
    void move(int delta);
    void scrollTo(GtkListBoxRow* row);
    void runSelected(bool beside);
    GtkWidget* makeRow(const PaletteRow& row, const std::string& query);
    std::string shortcutOf(const Command& c) const;
    GActionGroup* groupOf(const std::string& action, std::string& name) const;

    GtkWindow* window;
    std::function<GMenuModel*()> menu;
    GtkWidget* focusBack;
    Notebooks notebooks;
    GtkWidget* revealer = nullptr;  ///< the veil and the box, faded in and out together
    GtkWidget* panel = nullptr;     ///< the box
    GtkWidget* entry = nullptr;
    GtkWidget* hint = nullptr;  ///< what the empty entry says, over it
    GtkWidget* scroller = nullptr;
    GtkWidget* list = nullptr;
    GtkWidget* empty = nullptr;
    std::vector<Command> commands;            ///< what can be done now, taken at open
    std::vector<library::NotebookRef> books;  ///< the notebooks, taken at open
    std::vector<size_t> recentBooks;          ///< indices into books, newest first
    std::vector<PaletteRow> shown;            ///< in the order of the rows
    std::string shownQuery;                   ///< the text the rows are for
    bool opening = false;                     ///< the text is being cleared: no new search for it
    bool besideMode = false;                  ///< opened to put a notebook beside the one in the window
    guint closeIdle = 0;                      ///< the close after a focus out, waiting for a click on a row
};

}  // namespace ambitus
