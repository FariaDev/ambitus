/*
 * Ambitus
 *
 * A tip for the buttons that sit over the page (children of the GtkOverlay of the window). The tooltips of GTK3 do not
 * work there: the window of an overlay child has the overlay itself as user data, so the lookup of the tooltip starts
 * at the overlay and finds the main child that lies underneath first. This helper shows the tip by itself, on
 * enter-notify and leave-notify of the button, in a window of its own.
 *
 * Like the tip of the Summa: it appears after 500 ms with the pointer still on the button; while another tip was closed
 * less than 600 ms ago, the next one comes at once ("warm"); it goes away when the pointer leaves, on click and on
 * scroll. It fades in over 180 ms and out over 120 ms, unless the system asked for less animation.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>

#include <gtk/gtk.h>

namespace ambitus {

/// Gives the button a tip. `markup` is called each time the tip is about to show and returns Pango markup, such as
/// `Name  <span alpha="70%">Shortcut</span>`. Calling it again for the same button replaces the previous tip.
void setOverlayTip(GtkWidget* button, std::function<std::string()> markup);

/// The same, for a tip with a plain text (it is escaped here)
void setOverlayTipText(GtkWidget* button, const char* text);

}  // namespace ambitus
