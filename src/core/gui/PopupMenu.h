/*
 * Ambitus
 *
 * The menu in the style of the Summa menus, common to the right button of the page (PageMenu) and to the "⋯" of the
 * title bar (TitleBar): a GtkPopover with a column of items, each with a Lucide icon on the left, the label and the
 * shortcut dimmed on the right, a thin separator between groups, and what deletes in red. The styles are the
 * popover.amb-selpop / amb-ctxpop classes of ambitus.css.
 *
 * The items call the actions of the window ("cut", "paste"...) or of the application ("app.preferences"), so
 * Xournal++ does the work; an action that is disabled (or that does not exist) gives a faded item, and the shortcuts
 * are the ones the application registered for the action, never written here. A submenu is a second popover beside
 * the item.
 *
 * Keyboard: ↑ ↓ (and Home, End) move the highlight, Enter or Space runs the item, → opens a submenu, ← closes it, Esc
 * closes the menu; a click outside closes it too (the popover is modal).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <map>
#include <string>

#include <gtk/gtk.h>

namespace ambitus {

class PopupMenu {
public:
    /// window: the window whose actions the items call (asked when needed, as the menu may live before it is shown);
    /// onClosed: called when the menu goes away, with the submenu already gone
    explicit PopupMenu(std::function<GtkWindow*()> window, std::function<void()> onClosed = {});
    ~PopupMenu();
    PopupMenu(const PopupMenu&) = delete;
    PopupMenu& operator=(const PopupMenu&) = delete;

    /// The shortcuts that the application does not know, from the "accel" attribute of the items of the menu bar
    /// model (Xournal++ keeps Ctrl+N, Ctrl+O... only there)
    void setFallbackAccels(GMenuModel* menu);

    /// Starts a menu: drops the previous one and makes a new popover hanging from `relativeTo`. Returns the column
    /// that the items go in, `width` px wide
    GtkWidget* begin(GtkWidget* relativeTo, GtkPositionType position, int width);
    GtkPopover* popover() const { return GTK_POPOVER(pop); }

    /// An item that calls an action. `action` is of the window ("cut"), or of the application when it starts with
    /// "app.". `icon` may be null (an empty space of its size). Disabled when the action is
    GtkWidget* addItem(GtkWidget* box, const char* icon, const char* text, const char* action, GVariant* target,
                       bool danger = false);
    /// An item that runs `run` when it is clicked (a menu whose items are not actions of the window). `key` is the
    /// shortcut shown on the right, when there is one; the item is always enabled
    GtkWidget* addCallItem(GtkWidget* box, const char* icon, const char* text, std::function<void()> run,
                           const char* key = nullptr);
    /// An item that opens a submenu beside it; `fill` puts the items of the submenu in the box it gets
    void addSubmenuItem(GtkWidget* box, const char* icon, const char* text,
                        const std::function<void(GtkWidget*)>& fill);
    void addSeparator(GtkWidget* box);

    /// Shows the menu made since begin()
    void popup();
    void close();

private:
    struct Item;

    GtkWidget* newPopover(GtkWidget* relativeTo, GtkPositionType position);
    /// The shortcut of the action, as the tips of the palette show it; empty when it has none
    std::string shortcutOf(const char* action, GVariant* target) const;
    GtkWindow* window() const;
    GActionGroup* groupOf(const std::string& action, std::string& name) const;

    void setActive(GtkWidget* column, GtkWidget* item);
    void moveActive(GtkWidget* popover, int step);
    bool onKey(GtkWidget* popover, GdkEventKey* e);

    std::function<GtkWindow*()> getWindow;
    std::function<void()> onClosed;
    std::map<std::string, std::string> fallbackAccels;  ///< detailed action name → accelerator
    GtkWidget* pop = nullptr;
    GtkWidget* submenu = nullptr;
};

}  // namespace ambitus
