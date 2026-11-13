#include "StudioPalette.h"

#include <cmath>

#include "ambitus/ToolKeys.h"
#include "control/ToolEnums.h"
#include "util/Color.h"
#include "util/GVariantTemplate.h"
#include "util/GtkUtil.h"
#include "util/i18n.h"

namespace ambitus {

namespace {
void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

GtkWidget* actionButton(const char* action, GVariant* target, const char* tooltip) {
    GtkWidget* b = gtk_toggle_button_new();
    gtk_actionable_set_action_name(GTK_ACTIONABLE(b), action);
    gtk_actionable_set_action_target_value(GTK_ACTIONABLE(b), target);
    gtk_widget_set_tooltip_text(b, tooltip);
    gtk_widget_set_can_focus(b, false);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    xoj::util::gtk::setToggleButtonUnreleasable(GTK_TOGGLE_BUTTON(b));
    return b;
}

GtkWidget* separator() {
    GtkWidget* s = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    addClass(s, "amb-pal-sep");
    return s;
}

/// A dot drawn in the button: a colour swatch, or a size sample in the text colour
struct Dot {
    double r, g, b;
    double diameter;
    bool swatch;
};

gboolean drawDot(GtkWidget* w, cairo_t* cr, Dot* dot) {
    double cx = gtk_widget_get_allocated_width(w) / 2.0;
    double cy = gtk_widget_get_allocated_height(w) / 2.0;
    GtkWidget* button = gtk_widget_get_parent(w);
    bool on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    if (dot->swatch) {
        if (on) {  // ring around the chosen colour
            GdkRGBA fg;
            gtk_style_context_get_color(gtk_widget_get_style_context(button), gtk_widget_get_state_flags(button), &fg);
            gdk_cairo_set_source_rgba(cr, &fg);
            cairo_set_line_width(cr, 1.5);
            cairo_arc(cr, cx, cy, dot->diameter / 2 + 3, 0, 2 * M_PI);
            cairo_stroke(cr);
        }
        cairo_set_source_rgb(cr, dot->r, dot->g, dot->b);
    } else {
        GdkRGBA fg;
        gtk_style_context_get_color(gtk_widget_get_style_context(button), gtk_widget_get_state_flags(button), &fg);
        gdk_cairo_set_source_rgba(cr, &fg);
    }
    cairo_arc(cr, cx, cy, dot->diameter / 2, 0, 2 * M_PI);
    cairo_fill(cr);
    return true;
}

GtkWidget* dotButton(const char* action, GVariant* target, const char* tooltip, Dot dot, const char* cls) {
    GtkWidget* b = actionButton(action, target, tooltip);
    addClass(b, cls);
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 22, 22);
    auto* d = new Dot(dot);
    g_signal_connect_data(
            area, "draw", G_CALLBACK(drawDot), d, +[](gpointer p, GClosure*) { delete static_cast<Dot*>(p); },
            GConnectFlags(0));
    // redraw the ring when the state changes
    g_signal_connect_swapped(b, "toggled", G_CALLBACK(gtk_widget_queue_draw), area);
    gtk_container_add(GTK_CONTAINER(b), area);
    return b;
}
}  // namespace

StudioPalette::StudioPalette(GtkOverlay* overlay) {
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(box, "amb-palette");
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_margin_top(box, 12);

    struct Tool {
        ToolType type;
        const char* icon;
        const char* tip;
    };
    const Tool tools[] = {
            {TOOL_PEN, "xopp-tool-pencil", _("Pen")},
            {TOOL_HIGHLIGHTER, "xopp-tool-highlighter", _("Highlighter")},
            {TOOL_ERASER, "xopp-tool-eraser", _("Eraser")},
            {TOOL_SELECT_REGION, "xopp-select-lasso", _("Select Region")},
            {TOOL_TEXT, "xopp-tool-text", _("Text")},
            {TOOL_LATEX, "xopp-tool-math-tex", _("Add/Edit TeX")},
    };
    for (auto const& t: tools) {
        GtkWidget* b = actionButton("win.select-tool", makeGVariant(t.type), withToolKey(t.tip, t.type).c_str());
        gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_icon_name(t.icon, GTK_ICON_SIZE_LARGE_TOOLBAR));
        addClass(b, "amb-pal-tool");
        gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    }

    gtk_box_pack_start(GTK_BOX(box), separator(), false, false, 0);

    // the colours of the prototype: ink, blue, violet, magenta, green
    const guint32 colors[] = {0x1d2433, 0x2d5fdc, 0x8044ca, 0xbd1c67, 0x028053};
    for (guint32 c: colors) {
        Dot dot{((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0, (c & 0xff) / 255.0, 14, true};
        Color color(static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c));
        gtk_box_pack_start(GTK_BOX(box), dotButton("win.tool-color", makeGVariant(color), nullptr, dot, "amb-pal-dot"),
                           false, false, 0);
    }

    gtk_box_pack_start(GTK_BOX(box), separator(), false, false, 0);

    struct Size {
        ToolSize size;
        double diameter;
        const char* tip;
    };
    const Size sizes[] = {
            {TOOL_SIZE_FINE, 3, _("Fine")}, {TOOL_SIZE_MEDIUM, 6, _("Medium")}, {TOOL_SIZE_THICK, 10, _("Thick")}};
    for (auto const& s: sizes) {
        gtk_box_pack_start(
                GTK_BOX(box),
                dotButton("win.tool-size", makeGVariant(s.size), s.tip, Dot{0, 0, 0, s.diameter, false}, "amb-pal-dot"),
                false, false, 0);
    }

    gtk_widget_show_all(box);
    gtk_widget_set_no_show_all(box, true);
    gtk_widget_hide(box);
    gtk_overlay_add_overlay(overlay, box);
}

void StudioPalette::setVisible(bool visible) { gtk_widget_set_visible(box, visible); }

}  // namespace ambitus
