/*
 * Ambitus
 *
 * A short notice at the bottom of the page area that goes away by itself: what
 * an action is doing ("Lendo a escrita…", with a spinner) or why it did not
 * work. Not a dialog: the page stays usable.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

#include <gtk/gtk.h>

namespace ambitus {

class Toast {
public:
    explicit Toast(GtkOverlay* overlay);
    ~Toast();

    /// Shows text; busy adds a spinner and keeps it up until the next show or hide
    void show(const std::string& text, bool busy = false);
    void hide();

private:
    GtkWidget* revealer = nullptr;
    GtkWidget* label = nullptr;
    GtkWidget* spinner = nullptr;
    guint timeout = 0;
};

}  // namespace ambitus
