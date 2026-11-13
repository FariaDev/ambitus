#include "Toast.h"

namespace ambitus {

namespace {
constexpr guint SHOWN_MS = 4500;
}

Toast::Toast(GtkOverlay* overlay) {
    revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_widget_set_halign(revealer, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(revealer, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(revealer, 24);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "amb-toast");
    spinner = gtk_spinner_new();
    label = gtk_label_new(nullptr);
    gtk_label_set_line_wrap(GTK_LABEL(label), true);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_container_add(GTK_CONTAINER(box), spinner);
    gtk_container_add(GTK_CONTAINER(box), label);
    gtk_container_add(GTK_CONTAINER(revealer), box);
    gtk_widget_show_all(revealer);
    gtk_widget_hide(spinner);
    gtk_widget_set_no_show_all(spinner, true);

    gtk_overlay_add_overlay(overlay, revealer);
    gtk_overlay_set_overlay_pass_through(overlay, revealer, true);
}

Toast::~Toast() {
    if (timeout) {
        g_source_remove(timeout);
    }
}

void Toast::show(const std::string& text, bool busy) {
    gtk_label_set_text(GTK_LABEL(label), text.c_str());
    gtk_widget_set_visible(spinner, busy);
    if (busy) {
        gtk_spinner_start(GTK_SPINNER(spinner));
    } else {
        gtk_spinner_stop(GTK_SPINNER(spinner));
    }
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
    if (timeout) {
        g_source_remove(timeout);
        timeout = 0;
    }
    if (!busy) {
        timeout = g_timeout_add(
                SHOWN_MS,
                +[](gpointer d) -> gboolean {
                    auto* self = static_cast<Toast*>(d);
                    self->timeout = 0;
                    self->hide();
                    return G_SOURCE_REMOVE;
                },
                this);
    }
}

void Toast::hide() {
    gtk_spinner_stop(GTK_SPINNER(spinner));
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), false);
}

}  // namespace ambitus
