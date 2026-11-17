/*
 * Ambitus
 *
 * The window buttons of macOS (close, minimize, zoom) over the title bar of the window, instead of the ones GTK draws.
 * Implemented in MacWindowButtons.mm; nothing here is called on the other systems.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>

namespace ambitus::macwindow {

/// Gives the NSWindow of `window` its title bar with the system buttons, transparent, so the header bar of GTK draws
/// under them. Needs the window realized (its NSWindow exists then); calling it again changes nothing.
void useSystemWindowButtons(GtkWindow* window);

}  // namespace ambitus::macwindow
