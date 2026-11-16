/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "SubjectPicker.h"

#include <algorithm>
#include <utility>

#include "util/i18n.h"

namespace ambitus {

namespace {
constexpr int DOT_SIZE = 10;

void addClass(GtkWidget* w, const char* cls) { gtk_style_context_add_class(gtk_widget_get_style_context(w), cls); }

void clickOf(GtkWidget* w, std::function<void()> f) {
    auto* fn = new std::function<void()>(std::move(f));
    g_signal_connect_data(
            w, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) { (*static_cast<std::function<void()>*>(d))(); }), fn,
            [](gpointer d, GClosure*) { delete static_cast<std::function<void()>*>(d); }, GConnectFlags(0));
}

/// The square of the colour of the subject, as the library draws it beside a course
GtkWidget* dotFor(const std::string& color) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, DOT_SIZE, DOT_SIZE);
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    g_object_set_data_full(G_OBJECT(area), "amb-color", g_strdup(color.c_str()), g_free);
    g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer) -> gboolean {
                         GdkRGBA c;
                         auto* hex = static_cast<const char*>(g_object_get_data(G_OBJECT(a), "amb-color"));
                         if (!hex || !*hex || !gdk_rgba_parse(&c, hex)) {
                             gdk_rgba_parse(&c, "#8d8d8d");
                         }
                         const double w = gtk_widget_get_allocated_width(a);
                         const double h = gtk_widget_get_allocated_height(a);
                         const double r = 2;
                         cairo_new_sub_path(cr);
                         cairo_arc(cr, w - r, r, r, -G_PI / 2, 0);
                         cairo_arc(cr, w - r, h - r, r, 0, G_PI / 2);
                         cairo_arc(cr, r, h - r, r, G_PI / 2, G_PI);
                         cairo_arc(cr, r, r, r, G_PI, 3 * G_PI / 2);
                         cairo_close_path(cr);
                         gdk_cairo_set_source_rgba(cr, &c);
                         cairo_fill(cr);
                         return FALSE;
                     }),
                     nullptr);
    return area;
}

void setDot(GtkWidget* area, const std::string& color) {
    g_object_set_data_full(G_OBJECT(area), "amb-color", g_strdup(color.c_str()), g_free);
    gtk_widget_queue_draw(area);
}
}  // namespace

SubjectPicker::SubjectPicker() {
    fieldWidget = gtk_button_new();
    addClass(fieldWidget, "amb-dlg-field");
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    dot = dotFor("");
    gtk_box_pack_start(GTK_BOX(row), dot, false, false, 0);
    name = gtk_label_new(_("No subject"));
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    addClass(name, "amb-dlg-field-name");
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(row), name, true, true, 0);
    GtkWidget* chevron = gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(chevron), 16);
    gtk_box_pack_end(GTK_BOX(row), chevron, false, false, 0);
    gtk_container_add(GTK_CONTAINER(fieldWidget), row);
    clickOf(fieldWidget, [this] {
        if (popover) {
            gtk_widget_set_size_request(popover, -1, -1);
            gtk_popover_popup(GTK_POPOVER(popover));
        }
    });
    gtk_widget_set_sensitive(fieldWidget, false);
}

void SubjectPicker::setSubjects(std::vector<library::Subject> list) {
    subjects = std::move(list);
    if (popover) {
        gtk_widget_destroy(popover);
        popover = nullptr;
    }
    itemButtons.clear();
    itemChecks.clear();
    gtk_widget_set_sensitive(fieldWidget, !subjects.empty());
    if (subjects.empty()) {
        subject = 0;
        showSubject(0);
        return;
    }
    buildPopover();
    showSubject(0);
}

void SubjectPicker::buildPopover() {
    popover = gtk_popover_new(fieldWidget);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    addClass(popover, "amb-selpop");
    addClass(popover, "amb-dlg-pop");
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), true);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 240);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    for (size_t i = 0; i < subjects.size(); i++) {
        GtkWidget* b = gtk_button_new();
        gtk_widget_set_can_focus(b, false);
        addClass(b, "amb-selpop-item");
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(row), dotFor(subjects[i].color), false, false, 0);
        GtkWidget* l = gtk_label_new(subjects[i].name.c_str());
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
        GtkWidget* check = gtk_image_new_from_icon_name("xopp-check", GTK_ICON_SIZE_MENU);
        gtk_image_set_pixel_size(GTK_IMAGE(check), 14);
        addClass(check, "amb-selpop-check");
        gtk_widget_set_no_show_all(check, true);
        gtk_box_pack_end(GTK_BOX(row), check, false, false, 0);
        gtk_container_add(GTK_CONTAINER(b), row);
        clickOf(b, [this, i] {
            if (popover) {
                gtk_popover_popdown(GTK_POPOVER(popover));
            }
            choose(i);
            if (onPicked) {
                onPicked();
            }
        });
        gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
        itemButtons.push_back(b);
        itemChecks.push_back(check);
    }
    gtk_container_add(GTK_CONTAINER(scroll), box);
    gtk_container_add(GTK_CONTAINER(popover), scroll);
    gtk_widget_show_all(scroll);
}

void SubjectPicker::showSubject(size_t index) {
    subject = std::min(index, subjects.empty() ? size_t(0) : subjects.size() - 1);
    if (subjects.empty()) {
        gtk_label_set_text(GTK_LABEL(name), _("No subject"));
        setDot(dot, "");
    } else {
        gtk_label_set_text(GTK_LABEL(name), subjects[subject].name.c_str());
        setDot(dot, subjects[subject].color);
    }
    for (size_t i = 0; i < itemButtons.size(); i++) {
        GtkStyleContext* ctx = gtk_widget_get_style_context(itemButtons[i]);
        if (i == subject) {
            gtk_style_context_add_class(ctx, "amb-selpop-active");
            gtk_widget_show(itemChecks[i]);
        } else {
            gtk_style_context_remove_class(ctx, "amb-selpop-active");
            gtk_widget_hide(itemChecks[i]);
        }
    }
}

void SubjectPicker::choose(size_t index) {
    if (subjects.empty()) {
        return;
    }
    showSubject(index);
    if (onChanged) {
        onChanged();
    }
}

void SubjectPicker::popdown() {
    if (popover) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
}

}  // namespace ambitus
