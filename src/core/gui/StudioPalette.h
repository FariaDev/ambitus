/*
 * Ambitus
 *
 * The floating palette of the Studio mode, above the page: pen, highlighter,
 * eraser, selection, text and LaTeX, and five colours. Every button is bound to
 * an action of Xournal++ (select-tool, tool-color), so GTK marks the active one
 * by itself. Touching the active tool again opens its options in a popover,
 * wired to the settings and actions Xournal++ already has.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>

class Control;

namespace ambitus {

class StudioPalette {
public:
    StudioPalette(Control* control, GtkOverlay* overlay);

    void setVisible(bool visible);

private:
    GtkWidget* box = nullptr;
};

}  // namespace ambitus
