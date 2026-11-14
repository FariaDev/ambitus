/*
 * Ambitus
 *
 * The menu of the right mouse button on the page, in the style of the Summa menus: a Lucide icon on the left, the
 * shortcut dimmed on the right, a thin separator, and what deletes in red, last. With a selection it offers convert
 * to text, send to Summa, cut, copy, arrange (to front, to back) and delete; with none, paste, insert image, new page
 * below, the paper of the page, vertical space, send the page to Summa and delete the page.
 *
 * It is a GtkPopover on the GtkOverlay of the pages, pointing at the click. The items call the actions of the window
 * (win.cut, win.paste, win.select-tool...), so Xournal++ does the work; an action that is disabled gives a faded item,
 * and the shortcuts are the ones the application registered for the action, never written here. The submenus (Arrange,
 * Paper) are a second popover beside the item. It closes with Esc, with a click outside and on choosing an item.
 *
 * Who opens it: the right button of the mouse (or touchpad), when it has no tool of its own in the settings, in the
 * Studio and Focus modes. The stylus button keeps the radial menu, and so does the Classic mode (see RadialMenu).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>

#include <gtk/gtk.h>

#include "gui/inputdevices/InputEvents.h"

class Control;
class XournalView;

namespace ambitus {

class PageMenu {
public:
    PageMenu(Control* control, XournalView* xournal, GtkOverlay* overlay);
    ~PageMenu();
    PageMenu(const PageMenu&) = delete;
    PageMenu& operator=(const PageMenu&) = delete;

    /// Whether the editor mode has the menu (Studio and Focus, and boards); in the Classic mode the radial menu stays
    bool enabled() const;
    /// Opens the menu where the event happened. Without a selection, the page under the pointer becomes the current one
    void open(const InputEvent& e);
    void close();

    static constexpr int WIDTH = 220;  ///< px of the column of items

private:
    struct Item;

    /// An item that calls an action of the window; `action` has no "win." prefix. Disabled when the action is
    GtkWidget* addItem(GtkWidget* box, const char* icon, const char* text, const char* action, GVariant* target,
                       bool danger = false);
    /// An item that opens a submenu beside it; `fill` puts the items of the submenu in the box it gets
    void addSubmenuItem(GtkWidget* box, const char* icon, const char* text,
                        const std::function<void(GtkWidget*)>& fill);
    void addSeparator(GtkWidget* box);
    GtkWidget* newPopover(GtkWidget* relativeTo, GtkPositionType position);
    /// The shortcut of the action, as the tips of the palette show it; empty when it has none
    std::string shortcutOf(const char* action, GVariant* target) const;
    bool hasInk() const;
    GtkWindow* window() const;

    Control* control;
    XournalView* xournal;
    GtkOverlay* overlay;
    GtkWidget* popover = nullptr;
    GtkWidget* submenu = nullptr;
};

}  // namespace ambitus
