/*
 * Ambitus
 *
 * The floating palette of the Studio mode, above the page: pen, highlighter,
 * eraser, lasso, text and LaTeX, five colours and three sizes. Every button is
 * bound to an action of Xournal++ (select-tool, tool-color, tool-size), so GTK
 * marks the active one by itself.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>

namespace ambitus {

class StudioPalette {
public:
    explicit StudioPalette(GtkOverlay* overlay);

    void setVisible(bool visible);

private:
    GtkWidget* box = nullptr;
};

}  // namespace ambitus
