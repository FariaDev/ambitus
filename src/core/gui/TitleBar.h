/*
 * Ambitus
 *
 * The title bar of the window (client side decorations): the ∂ mark, the tabs (the
 * library, then one per open document), the screen modes (Classic, Studio, Focus),
 * undo and the "⋯" menu: the few things that live nowhere else (new, open, export,
 * settings...), in the style of the Summa menus (PopupMenu). The rest of the menu bar
 * is found with the command box (Ctrl+K), which reads the same menu model.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "gui/PopupMenu.h"

namespace ambitus {

enum class EditorMode { CLASSIC = 0, STUDIO = 1, FOCUS = 2 };

/// A tab of the strip, as shown
struct TabItem {
    std::string title;
    std::string color;  ///< "#rrggbb" of the course; empty for none
    bool active = false;
    bool modified = false;
    bool closable = true;
    /// A notebook that has a name to change: not a draft (it is named when kept) and not an untitled one
    bool renamable = false;

    bool operator==(const TabItem& o) const {
        return title == o.title && color == o.color && active == o.active && modified == o.modified &&
               closable == o.closable && renamable == o.renamable;
    }
};

class TitleBar {
public:
    /// menu: the model of the menu bar; the "⋯" takes the shortcuts that only it knows
    TitleBar(GtkWindow* window, GMenuModel* menu, std::function<void(EditorMode)> onMode);

    GtkWidget* getWidget() const { return bar; }

    /// The library hides what only makes sense in the editor
    void setEditorShown(bool editor);
    /// A board has no screen modes
    void setModesShown(bool shown);
    /// Marks the mode without calling back
    void setMode(EditorMode mode);

    /// The tabs of the documents; the library tab is active when none of them is
    void setTabs(std::vector<TabItem> items);
    /// index in the list given to setTabs
    void setTabCallbacks(std::function<void(size_t)> activate, std::function<void(size_t)> close);
    /// Renames the notebook of a tab: a double click on the name of the tab, or "Renomear…" of the "⋯" menu for the
    /// active one. Only for the tabs that are renamable.
    void setRenameCallback(std::function<void(size_t)> rename);

private:
    static constexpr int MENU_WIDTH = 292;  ///< px of the column of the "⋯" menu (300 with the padding, as in the r6)

    void openMenu();
    /// The menu of a document tab (right button): close it
    void openTabMenu(GtkWidget* tab, size_t index);
    void rebuildTabs();
    void closeLater(size_t index);

    GtkWindow* window;
    GtkWidget* bar = nullptr;
    GtkWidget* strip = nullptr;
    GtkWidget* libraryTab = nullptr;
    std::vector<TabItem> items;
    std::function<void(size_t)> onActivate;
    std::function<void(size_t)> onClose;
    std::function<void(size_t)> onRename;
    GtkWidget* modes = nullptr;
    GtkWidget* undo = nullptr;
    GtkWidget* more = nullptr;
    PopupMenu moreMenu;
    GtkWidget* modeButtons[3] = {};
    std::function<void(EditorMode)> onMode;
    bool editorShown = true;
    bool modesShown = true;
    bool updating = false;
};

}  // namespace ambitus
