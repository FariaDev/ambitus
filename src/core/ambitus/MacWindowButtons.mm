/*
 * Ambitus
 *
 * The window buttons of macOS over the title bar (see MacWindowButtons.h).
 *
 * @license GNU GPLv2 or later
 */

#include "MacWindowButtons.h"

#import <AppKit/AppKit.h>

// gdkquartz-cocoa-access.h has no extern "C" around its declarations, so including it from C++ asks the linker for a
// mangled name that libgdk does not export. The symbol is plain C; declare it here with the C linkage it has.
extern "C" NSWindow* gdk_quartz_window_get_nswindow(GdkWindow* window);

namespace ambitus::macwindow {

void useSystemWindowButtons(GtkWindow* window) {
    GdkWindow* gdkWindow = gtk_widget_get_window(GTK_WIDGET(window));
    if (!gdkWindow) {
        return;
    }
    NSWindow* nsWindow = gdk_quartz_window_get_nswindow(gdkWindow);
    if (!nsWindow) {
        return;
    }
    // The header bar of Ambitus is the title bar. FullSizeContentView lets it go under the system buttons, which need
    // the titled style to exist at all
    const NSWindowStyleMask buttons = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                                      NSWindowStyleMaskFullSizeContentView;
    [nsWindow setStyleMask:[nsWindow styleMask] | buttons];
    [nsWindow setTitlebarAppearsTransparent:YES];
    [nsWindow setTitleVisibility:NSWindowTitleHidden];
}

}  // namespace ambitus::macwindow
