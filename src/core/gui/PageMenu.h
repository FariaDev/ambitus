/*
 * Ambitus
 *
 * The menu of the right mouse button on the page, in the style of the Summa menus: a Lucide icon on the left, the
 * shortcut dimmed on the right, a thin separator, and what deletes in red, last. With a selection it offers convert
 * to text, send to Summa, cut, copy, arrange (to front, to back) and delete; with none, paste, insert image, new page
 * below, the paper of the page, vertical space, send the page to Summa and delete the page.
 *
 * It is a GtkPopover on the GtkOverlay of the pages, pointing at the click, made with PopupMenu (which says how the
 * items, the shortcuts, the submenus (Arrange, Paper) and the keyboard work). The items call the actions of the window
 * (win.cut, win.paste, win.select-tool...), so Xournal++ does the work.
 *
 * Who opens it: the right button of the mouse (or touchpad), when it has no tool of its own in the settings, in the
 * Studio and Focus modes. The stylus button keeps the radial menu, and so does the Classic mode (see RadialMenu).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>

#include "gui/PopupMenu.h"
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
    bool hasInk() const;

    Control* control;
    XournalView* xournal;
    GtkOverlay* overlay;
    PopupMenu menu;  ///< the popover and its items, shared with the "⋯" of the title bar
};

}  // namespace ambitus
