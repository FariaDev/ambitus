#include "RadialMenu.h"

#include <algorithm>
#include <cmath>

#include "control/Control.h"
#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/ButtonConfig.h"
#include "control/settings/Settings.h"
#include "gui/XournalView.h"
#include "gui/inputdevices/InputContext.h"
#include "util/GVariantTemplate.h"
#include "util/i18n.h"

namespace ambitus {

namespace {
enum Index { PEN, HIGHLIGHTER, ERASER, LASSO, TEXT, SHAPES, LATEX, UNDO };

GdkRGBA lookup(GtkWidget* w, const char* name, const char* fallback) {
    GdkRGBA c;
    if (!gtk_style_context_lookup_color(gtk_widget_get_style_context(w), name, &c)) {
        gdk_rgba_parse(&c, fallback);
    }
    return c;
}

void circle(cairo_t* cr, double x, double y, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y, r, 0, 2 * M_PI);
}

/// Draws the icon as a mask in the current source colour, so it follows light, dark and the accent
void drawIcon(cairo_t* cr, GtkWidget* w, const char* name, double x, double y, int size) {
    int scale = gtk_widget_get_scale_factor(w);
    GtkIconInfo* info = gtk_icon_theme_lookup_icon_for_scale(gtk_icon_theme_get_default(), name, size, scale,
                                                             GTK_ICON_LOOKUP_FORCE_SIZE);
    if (!info) {
        return;
    }
    GdkPixbuf* pb = gtk_icon_info_load_icon(info, nullptr);
    g_object_unref(info);
    if (!pb) {
        return;
    }
    cairo_surface_t* s = gdk_cairo_surface_create_from_pixbuf(pb, scale, nullptr);
    cairo_mask_surface(cr, s, x - size / 2.0, y - size / 2.0);
    cairo_surface_destroy(s);
    g_object_unref(pb);
}
}  // namespace

int RadialMenu::itemAt(double dx, double dy, size_t count) {
    double d = std::hypot(dx, dy);
    if (count == 0 || d < HOLE / 2 || d > SIZE / 2 + 24) {
        return -1;
    }
    // item 0 is at the top, the others follow clockwise
    double a = std::atan2(dy, dx) + M_PI / 2;
    double step = 2 * M_PI / static_cast<double>(count);
    int i = static_cast<int>(std::floor(a / step + 0.5));
    return ((i % static_cast<int>(count)) + static_cast<int>(count)) % static_cast<int>(count);
}

RadialMenu::RadialMenu(Control* control, XournalView* xournal, GtkOverlay* overlay):
        control(control), xournal(xournal), overlay(overlay) {
    items = {{"xopp-tool-pencil", _("Pen")},
             {"xopp-tool-highlighter", _("Highlighter")},
             {"xopp-tool-eraser", _("Eraser")},
             {"xopp-select-lasso", _("Select Region")},
             {"xopp-tool-text", _("Text")},
             {"xopp-shape-recognizer", _("Shape Recognizer")},
             {"xopp-tool-math-tex", _("Add/Edit TeX")},
             {"xopp-edit-undo", _("Undo")}};

    layer = gtk_drawing_area_new();
    gtk_widget_add_events(
            layer, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK);
    gtk_widget_set_can_focus(layer, true);
    g_signal_connect(layer, "draw", G_CALLBACK(onDraw), this);
    g_signal_connect(layer, "button-press-event", G_CALLBACK(onButton), this);
    g_signal_connect(layer, "button-release-event", G_CALLBACK(onButton), this);
    g_signal_connect(layer, "motion-notify-event", G_CALLBACK(onMotion), this);
    g_signal_connect(layer, "key-press-event", G_CALLBACK(onKey), this);
    gtk_widget_set_no_show_all(layer, true);
    gtk_overlay_add_overlay(overlay, layer);

    InputContext::ambitusPreHandler = [this](const InputEvent& e) { return onPagesEvent(e); };
}

RadialMenu::~RadialMenu() { InputContext::ambitusPreHandler = nullptr; }

bool RadialMenu::onPagesEvent(const InputEvent& e) {
    // overlay coordinates of the event (InputEvent::absolute is in the coordinates of the pages widget)
    auto toOverlay = [this](const InputEvent& e, double& x, double& y) {
        int ox = 0, oy = 0;
        gtk_widget_translate_coordinates(xournal->getWidget(), GTK_WIDGET(overlay), 0, 0, &ox, &oy);
        x = e.absolute.x + ox;
        y = e.absolute.y + oy;
    };

    if (held) {
        double x, y;
        toOverlay(e, x, y);
        if (e.type == MOTION_EVENT) {
            hover(x, y);
            if (std::hypot(x - cx, y - cy) > HOLE / 2) {
                moved = true;
            }
        } else if (e.type == BUTTON_RELEASE_EVENT) {
            held = false;
            hover(x, y);
            if (hovered >= 0) {
                activate(hovered);  // press, move, release
            } else if (moved) {
                close();  // released in the hole after moving around, or outside: nothing chosen
            }
            // a click without moving leaves the ring open for a second click
        }
        return true;
    }

    if (e.type != BUTTON_PRESS_EVENT || e.button != 3) {
        return false;
    }
    Button b;
    if (e.deviceClass == INPUT_DEVICE_MOUSE) {
        b = BUTTON_MOUSE_RIGHT;
    } else if (e.deviceClass == INPUT_DEVICE_PEN) {
        b = BUTTON_STYLUS_TWO;
    } else {
        return false;
    }
    ButtonConfig* cfg = control->getSettings()->getButtonConfig(b);
    if (!cfg || cfg->getAction() != TOOL_NONE) {
        return false;  // the button has a tool of its own
    }
    double x, y;
    toOverlay(e, x, y);
    openAt(x, y);
    held = true;
    moved = false;
    return true;
}

void RadialMenu::openAt(double x, double y) {
    // keep the ring, and the label above it, inside the window
    double w = gtk_widget_get_allocated_width(GTK_WIDGET(overlay));
    double h = gtk_widget_get_allocated_height(GTK_WIDGET(overlay));
    double r = SIZE / 2 + 4;
    cx = std::clamp(x, r, std::max(r, w - r));
    cy = std::clamp(y, r + 30, std::max(r + 30, h - r));
    open = true;
    hovered = -1;
    gtk_widget_show(layer);
    gtk_widget_grab_focus(layer);
    gtk_widget_queue_draw(layer);
}

void RadialMenu::close() {
    open = false;
    held = false;
    hovered = -1;
    gtk_widget_hide(layer);
    gtk_widget_grab_focus(xournal->getWidget());
}

void RadialMenu::hover(double x, double y) {
    int i = itemAt(x - cx, y - cy, items.size());
    if (i != hovered) {
        hovered = i;
        gtk_widget_queue_draw(layer);
    }
}

int RadialMenu::activeItem() const {
    ToolHandler* th = control->getToolHandler();
    switch (th->getToolType()) {
        case TOOL_PEN:
            return th->getDrawingType() == DRAWING_TYPE_SHAPE_RECOGNIZER ? SHAPES : PEN;
        case TOOL_HIGHLIGHTER:
            return HIGHLIGHTER;
        case TOOL_ERASER:
            return ERASER;
        case TOOL_SELECT_REGION:
            return LASSO;
        case TOOL_TEXT:
            return TEXT;
        case TOOL_LATEX:
            return LATEX;
        default:
            return -1;
    }
}

void RadialMenu::activate(int index) {
    close();
    auto* win = G_ACTION_GROUP(gtk_widget_get_toplevel(GTK_WIDGET(overlay)));
    auto tool = [win](ToolType t) { g_action_group_activate_action(win, "select-tool", makeGVariant(t)); };
    bool shapes = control->getToolHandler()->getDrawingType() == DRAWING_TYPE_SHAPE_RECOGNIZER;
    switch (index) {
        case PEN:
            tool(TOOL_PEN);
            if (shapes) {
                g_action_group_change_action_state(win, "tool-draw-shape-recognizer", g_variant_new_boolean(false));
            }
            break;
        case HIGHLIGHTER:
            tool(TOOL_HIGHLIGHTER);
            break;
        case ERASER:
            tool(TOOL_ERASER);
            break;
        case LASSO:
            tool(TOOL_SELECT_REGION);
            break;
        case TEXT:
            tool(TOOL_TEXT);
            break;
        case SHAPES:
            tool(TOOL_PEN);
            g_action_group_change_action_state(win, "tool-draw-shape-recognizer", g_variant_new_boolean(true));
            break;
        case LATEX:
            tool(TOOL_LATEX);
            break;
        case UNDO:
            g_action_group_activate_action(win, "undo", nullptr);
            break;
        default:
            break;
    }
}

gboolean RadialMenu::onDraw(GtkWidget* w, cairo_t* cr, RadialMenu* self) {
    if (!self->open) {
        return false;
    }
    bool dark = gtk_style_context_has_class(gtk_widget_get_style_context(gtk_widget_get_toplevel(w)), "darkMode");
    GdkRGBA bg = lookup(w, dark ? "amb_dk_layer" : "amb_bg", "#ffffff");
    GdkRGBA layer = lookup(w, dark ? "amb_dk_layer2" : "amb_layer", "#f4f4f4");
    GdkRGBA hover = lookup(w, dark ? "amb_dk_hover" : "amb_hover", "#e8e8e8");
    GdkRGBA border = lookup(w, dark ? "amb_dk_border" : "amb_border", "#e0e0e0");
    GdkRGBA text = lookup(w, dark ? "amb_dk_text" : "amb_text", "#161616");
    GdkRGBA text2 = lookup(w, dark ? "amb_dk_text2" : "amb_text2", "#525252");
    GdkRGBA accent = lookup(w, dark ? "amb_dk_accent" : "amb_accent", "#303030");
    GdkRGBA onAccent = lookup(w, dark ? "amb_dk_on_accent" : "amb_on_accent", "#ffffff");

    double cx = self->cx, cy = self->cy;
    double R = SIZE / 2;

    // soft shadow, then the ring with a hairline
    for (int i = 6; i >= 1; i--) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.025);
        circle(cr, cx, cy + 3, R + i);
        cairo_fill(cr);
    }
    gdk_cairo_set_source_rgba(cr, &bg);
    circle(cr, cx, cy, R);
    cairo_fill_preserve(cr);
    gdk_cairo_set_source_rgba(cr, &border);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    // the hole with the mark
    gdk_cairo_set_source_rgba(cr, &layer);
    circle(cr, cx, cy, HOLE / 2);
    cairo_fill_preserve(cr);
    gdk_cairo_set_source_rgba(cr, &border);
    cairo_stroke(cr);
    PangoLayout* mark = gtk_widget_create_pango_layout(w, "∂");
    PangoFontDescription* fd = pango_font_description_from_string("Serif Italic 22");
    pango_layout_set_font_description(mark, fd);
    int mw, mh;
    pango_layout_get_pixel_size(mark, &mw, &mh);
    gdk_cairo_set_source_rgba(cr, &text2);
    cairo_move_to(cr, cx - mw / 2.0, cy - mh / 2.0);
    pango_cairo_show_layout(cr, mark);
    pango_font_description_free(fd);
    g_object_unref(mark);

    int active = self->activeItem();
    size_t n = self->items.size();
    for (size_t i = 0; i < n; i++) {
        double a = -M_PI / 2 + static_cast<double>(i) * 2 * M_PI / static_cast<double>(n);
        double x = cx + std::cos(a) * RADIUS, y = cy + std::sin(a) * RADIUS;
        bool on = static_cast<int>(i) == active;
        bool hov = static_cast<int>(i) == self->hovered;
        if (on || hov) {
            gdk_cairo_set_source_rgba(cr, on ? &accent : &hover);
            circle(cr, x, y, ITEM / 2);
            cairo_fill(cr);
        }
        gdk_cairo_set_source_rgba(cr, on ? &onAccent : (hov ? &text : &text2));
        drawIcon(cr, w, self->items[i].icon, x, y, 18);
    }

    // label of the item under the pointer (or of the active one), above the ring
    int shown = self->hovered >= 0 ? self->hovered : active;
    if (shown >= 0) {
        PangoLayout* l = gtk_widget_create_pango_layout(w, self->items[static_cast<size_t>(shown)].label.c_str());
        int lw, lh;
        pango_layout_get_pixel_size(l, &lw, &lh);
        double bw = lw + 16, bh = lh + 6;
        double bx = cx - bw / 2, by = cy - R - 30;
        gdk_cairo_set_source_rgba(cr, &text);
        double rr = 4;
        cairo_new_sub_path(cr);
        cairo_arc(cr, bx + bw - rr, by + rr, rr, -M_PI / 2, 0);
        cairo_arc(cr, bx + bw - rr, by + bh - rr, rr, 0, M_PI / 2);
        cairo_arc(cr, bx + rr, by + bh - rr, rr, M_PI / 2, M_PI);
        cairo_arc(cr, bx + rr, by + rr, rr, M_PI, 3 * M_PI / 2);
        cairo_close_path(cr);
        cairo_fill(cr);
        gdk_cairo_set_source_rgba(cr, &bg);
        cairo_move_to(cr, bx + 8, by + 3);
        pango_cairo_show_layout(cr, l);
        g_object_unref(l);
    }
    return true;
}

gboolean RadialMenu::onButton(GtkWidget*, GdkEventButton* e, RadialMenu* self) {
    if (!self->open) {
        return false;
    }
    self->hover(e->x, e->y);
    if (e->type == GDK_BUTTON_PRESS) {
        if (std::hypot(e->x - self->cx, e->y - self->cy) > SIZE / 2 + 24) {
            self->close();  // a click outside
        }
        return true;
    }
    if (e->type == GDK_BUTTON_RELEASE && self->hovered >= 0) {
        self->activate(self->hovered);
    }
    return true;
}

gboolean RadialMenu::onMotion(GtkWidget*, GdkEventMotion* e, RadialMenu* self) {
    if (self->open) {
        self->hover(e->x, e->y);
    }
    return true;
}

gboolean RadialMenu::onKey(GtkWidget*, GdkEventKey* e, RadialMenu* self) {
    if (e->keyval == GDK_KEY_Escape) {
        self->close();
        return true;
    }
    return false;
}

}  // namespace ambitus
