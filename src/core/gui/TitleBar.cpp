#include "TitleBar.h"

#include "util/i18n.h"

namespace ambitus {

namespace {
void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

GtkWidget* iconButton(const char* icon, const char* action, const char* tooltip) {
    GtkWidget* b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(b), action);
    gtk_widget_set_tooltip_text(b, tooltip);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    addClass(b, "amb-tb-icon");
    return b;
}
}  // namespace

TitleBar::TitleBar(GtkWindow* window, GMenuModel* menu, std::function<void(EditorMode)> onMode):
        window(window), onMode(std::move(onMode)) {
    bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(bar), true);
    addClass(bar, "amb-titlebar");

    // left: the mark and the library
    GtkWidget* brand = gtk_label_new("∂");
    addClass(brand, "amb-brand");
    gtk_widget_set_tooltip_text(brand, "Ambitus");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), brand);

    GtkWidget* library = gtk_button_new_with_label(_("Library"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(library), "win.library");
    gtk_widget_set_valign(library, GTK_ALIGN_CENTER);
    addClass(library, "amb-tb-library");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), library);

    // middle: the notebook
    title = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
    addClass(title, "amb-tb-title");
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(bar), title);
    g_signal_connect_swapped(window, "notify::title", G_CALLBACK(+[](TitleBar* self) { self->updateTitle(); }), this);

    // right (packed from the right edge): menu, undo, modes
    GtkWidget* more = gtk_menu_button_new();
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(more), menu);
    gtk_menu_button_set_use_popover(GTK_MENU_BUTTON(more), true);
    gtk_button_set_image(GTK_BUTTON(more), gtk_image_new_from_icon_name("view-more-horizontal-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(more, _("Menu"));
    gtk_widget_set_valign(more, GTK_ALIGN_CENTER);
    addClass(more, "amb-tb-icon");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), more);

    undo = iconButton("edit-undo-symbolic", "win.undo", _("Undo"));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), undo);

    modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(modes, "amb-modes");
    gtk_widget_set_valign(modes, GTK_ALIGN_CENTER);
    const char* names[3] = {_("Classic"), _("Studio"), _("Focus")};
    const char* tips[3] = {_("All the toolbars"), _("A floating palette above the page"), _("Only the page")};
    GSList* group = nullptr;
    for (int i = 0; i < 3; i++) {
        GtkWidget* b = gtk_radio_button_new_with_label(group, names[i]);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(b));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), false);  // a button, not a radio dot
        gtk_widget_set_tooltip_text(b, tips[i]);
        g_object_set_data(G_OBJECT(b), "amb-mode", GINT_TO_POINTER(i));
        g_signal_connect(b, "toggled", G_CALLBACK(+[](GtkToggleButton* t, TitleBar* self) {
                             if (self->updating || !gtk_toggle_button_get_active(t)) {
                                 return;
                             }
                             int m = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(t), "amb-mode"));
                             self->onMode(static_cast<EditorMode>(m));
                         }),
                         this);
        gtk_box_pack_start(GTK_BOX(modes), b, false, false, 0);
        modeButtons[i] = b;
    }
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), modes);

    gtk_widget_show_all(bar);
    updateTitle();
}

void TitleBar::setEditorShown(bool editor) {
    editorShown = editor;
    gtk_widget_set_visible(modes, editor && modesShown);
    gtk_widget_set_visible(undo, editor);
    updateTitle();
}

void TitleBar::setModesShown(bool shown) {
    modesShown = shown;
    gtk_widget_set_visible(modes, editorShown && modesShown);
}

void TitleBar::setMode(EditorMode mode) {
    updating = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(modeButtons[static_cast<int>(mode)]), true);
    updating = false;
}

void TitleBar::updateTitle() {
    if (!editorShown) {
        gtk_label_set_text(GTK_LABEL(title), _("Library"));
        return;
    }
    // the window title is "<name>.xopp - Ambitus": the bar shows only the name
    std::string t = gtk_window_get_title(window) ? gtk_window_get_title(window) : "";
    const std::string suffix = " - Ambitus";
    if (t.size() >= suffix.size() && t.compare(t.size() - suffix.size(), suffix.size(), suffix) == 0) {
        t.erase(t.size() - suffix.size());
    }
    const std::string ext = ".xopp";
    if (t.size() >= ext.size() && t.compare(t.size() - ext.size(), ext.size(), ext) == 0) {
        t.erase(t.size() - ext.size());
    }
    gtk_label_set_text(GTK_LABEL(title), t.c_str());
}

}  // namespace ambitus
