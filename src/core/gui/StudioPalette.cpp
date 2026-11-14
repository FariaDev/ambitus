#include "StudioPalette.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ambitus/ToolKeys.h"
#include "control/Control.h"
#include "control/ToolEnums.h"
#include "control/settings/Settings.h"
#include "control/tools/StrokeStabilizerEnum.h"
#include "model/Font.h"
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
        // the dark ink would vanish on the dark theme: a thin light ring keeps it visible
        GdkRGBA text;
        gtk_style_context_get_color(gtk_widget_get_style_context(button), gtk_widget_get_state_flags(button), &text);
        if (text.green > 0.5 && dot->g < 0.2) {
            cairo_arc(cr, cx, cy, dot->diameter / 2, 0, 2 * M_PI);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, 0xf4 / 255.0, 0xf4 / 255.0, 0xf4 / 255.0);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            return true;
        }
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

// ---------------------------------------------------------------------------------------------------------------------
// Tooltips: the name and, dimmed, the shortest shortcut the app has for it
// ---------------------------------------------------------------------------------------------------------------------

struct TipInfo {
    std::string name;
    ToolType keyTool;  ///< The tool whose single key is shown (TOOL_NONE: none)
};

/// The shortcut of a button: the single key of its tool (ToolKeys), else the first accelerator the application
/// registered for the button's action, else nothing
std::string shortcutOf(GtkWidget* w, TipInfo const& info) {
    if (info.keyTool != TOOL_NONE) {
        for (unsigned int k = 'a'; k <= 'z'; k++) {
            if (auto t = ambitus::toolForKey(k); t && *t == info.keyTool) {
                return std::string(1, static_cast<char>(g_ascii_toupper(static_cast<gchar>(k))));
            }
        }
    }
    GtkWidget* top = gtk_widget_get_toplevel(w);
    const char* action = gtk_actionable_get_action_name(GTK_ACTIONABLE(w));
    if (!GTK_IS_WINDOW(top) || !action) {
        return "";
    }
    GtkApplication* app = gtk_window_get_application(GTK_WINDOW(top));
    if (!app) {
        return "";
    }
    GVariant* target = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(w));
    gchar* detailed = g_action_print_detailed_name(action, target);
    gchar** accels = gtk_application_get_accels_for_action(app, detailed);
    std::string out;
    if (accels && accels[0]) {
        guint key = 0;
        GdkModifierType mods = GdkModifierType(0);
        gtk_accelerator_parse(accels[0], &key, &mods);
        if (key != 0) {
            gchar* label = gtk_accelerator_get_label(key, mods);
            out = label;
            g_free(label);
        }
    }
    g_strfreev(accels);
    g_free(detailed);
    return out;
}

gboolean onQueryTooltip(GtkWidget* w, gint, gint, gboolean, GtkTooltip* tooltip, TipInfo* info) {
    gchar* name = g_markup_escape_text(info->name.c_str(), -1);
    std::string markup = name;
    g_free(name);
    if (std::string key = shortcutOf(w, *info); !key.empty()) {
        gchar* k = g_markup_escape_text(key.c_str(), -1);
        markup += "  <span alpha=\"70%\">";
        markup += k;
        markup += "</span>";
        g_free(k);
    }
    gtk_tooltip_set_markup(tooltip, markup.c_str());
    return true;
}

void setTip(GtkWidget* w, const char* name, ToolType keyTool) {
    gtk_widget_set_tooltip_text(w, name);  // the plain name, should the handler below not answer
    gtk_widget_set_has_tooltip(w, true);
    g_signal_connect_data(
            w, "query-tooltip", G_CALLBACK(onQueryTooltip), new TipInfo{name, keyTool},
            +[](gpointer p, GClosure*) { delete static_cast<TipInfo*>(p); }, GConnectFlags(0));
}

// ---------------------------------------------------------------------------------------------------------------------
// The popover of a tool: its options, each one wired to a setting or an action Xournal++ already has
// ---------------------------------------------------------------------------------------------------------------------

GtkWidget* sectionLabel(const char* text, bool first) {
    gchar* up = g_utf8_strup(text, -1);
    GtkWidget* l = gtk_label_new(up);
    g_free(up);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    addClass(l, "amb-opt-label");
    gtk_widget_set_margin_top(l, first ? 0 : 10);
    gtk_widget_set_margin_bottom(l, 6);
    return l;
}

struct SegItem {
    const char* label;
    const char* action;
    GVariant* target;
};

/// Linked buttons, one bound to each value of a stateful action
GtkWidget* segmented(std::initializer_list<SegItem> items) {
    GtkWidget* seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(seg, "amb-seg");
    gtk_widget_set_halign(seg, GTK_ALIGN_START);
    for (auto const& it: items) {
        GtkWidget* b = actionButton(it.action, it.target, nullptr);
        gtk_button_set_label(GTK_BUTTON(b), it.label);
        gtk_box_pack_start(GTK_BOX(seg), b, false, false, 0);
    }
    return seg;
}

/// The action group of the window ("win."), seen from any widget inside it
GActionGroup* winActions(GtkWidget* w) { return gtk_widget_get_action_group(w, "win"); }

struct SizeScale {
    const char* action;  ///< Without "win.": tool-pen-size, tool-highlighter-size
    bool syncing = false;
};

/// The five thicknesses Xournal++ has for a tool, from very fine to very thick, on a scale
GtkWidget* sizeScale(const char* action) {
    GtkWidget* s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, TOOL_SIZE_VERY_FINE, TOOL_SIZE_VERY_THICK, 1);
    gtk_scale_set_draw_value(GTK_SCALE(s), false);
    gtk_scale_set_digits(GTK_SCALE(s), 0);
    gtk_range_set_round_digits(GTK_RANGE(s), 0);
    for (int i = TOOL_SIZE_VERY_FINE; i <= TOOL_SIZE_VERY_THICK; i++) {
        gtk_scale_add_mark(GTK_SCALE(s), i, GTK_POS_BOTTOM, nullptr);
    }
    gtk_widget_set_can_focus(s, false);
    auto* data = new SizeScale{action};
    g_signal_connect_data(s, "map", G_CALLBACK(+[](GtkWidget* w, SizeScale* d) {
                              GActionGroup* g = winActions(w);
                              if (!g) {
                                  return;
                              }
                              GVariant* st = g_action_group_get_action_state(g, d->action);
                              if (st) {
                                  d->syncing = true;
                                  gtk_range_set_value(GTK_RANGE(w),
                                                      static_cast<double>(getGVariantValue<ToolSize>(st)));
                                  d->syncing = false;
                                  g_variant_unref(st);
                              }
                          }),
                          data, nullptr, GConnectFlags(0));
    g_signal_connect_data(
            s, "value-changed", G_CALLBACK(+[](GtkRange* r, SizeScale* d) {
                if (d->syncing) {
                    return;
                }
                if (GActionGroup* g = winActions(GTK_WIDGET(r))) {
                    auto size = static_cast<ToolSize>(static_cast<int>(std::lround(gtk_range_get_value(r))));
                    g_action_group_activate_action(g, d->action, makeGVariant(size));
                }
            }),
            data, +[](gpointer p, GClosure*) { delete static_cast<SizeScale*>(p); }, GConnectFlags(0));
    return s;
}

/// A label on the left, a switch on the right
GtkWidget* switchRow(const char* text, GtkWidget* sw) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    addClass(row, "amb-opt-row");
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(row), sw, false, false, 0);
    gtk_widget_set_margin_top(row, 8);
    return row;
}

/// A switch kept in step with a setting: read when the popover opens, written when it is flipped
GtkWidget* settingSwitch(Control* control, bool (*get)(Settings*), void (*set)(Settings*, bool)) {
    struct Data {
        Control* control;
        bool (*get)(Settings*);
        void (*set)(Settings*, bool);
        bool syncing = false;
    };
    GtkWidget* sw = gtk_switch_new();
    auto* d = new Data{control, get, set};
    g_signal_connect_data(sw, "map", G_CALLBACK(+[](GtkWidget* w, Data* d) {
                              d->syncing = true;
                              gtk_switch_set_active(GTK_SWITCH(w), d->get(d->control->getSettings()));
                              d->syncing = false;
                          }),
                          d, nullptr, GConnectFlags(0));
    g_signal_connect_data(
            sw, "notify::active", G_CALLBACK(+[](GObject* w, GParamSpec*, Data* d) {
                if (!d->syncing) {
                    d->set(d->control->getSettings(), gtk_switch_get_active(GTK_SWITCH(w)));
                }
            }),
            d, +[](gpointer p, GClosure*) { delete static_cast<Data*>(p); }, GConnectFlags(0));
    return sw;
}

bool strokeIsSmoothed(Settings* s) {
    return s->getStabilizerAveragingMethod() != StrokeStabilizer::AveragingMethod::NONE ||
           s->getStabilizerPreprocessor() != StrokeStabilizer::Preprocessor::NONE;
}
void smoothStroke(Settings* s, bool on) {
    if (on) {
        if (!strokeIsSmoothed(s)) {
            s->setStabilizerAveragingMethod(StrokeStabilizer::AveragingMethod::ARITHMETIC);
        }
    } else {
        s->setStabilizerAveragingMethod(StrokeStabilizer::AveragingMethod::NONE);
        s->setStabilizerPreprocessor(StrokeStabilizer::Preprocessor::NONE);
    }
}

GtkWidget* fontButton(Control* control) {
    GtkWidget* fb = gtk_font_button_new();
    gtk_font_button_set_show_size(GTK_FONT_BUTTON(fb), true);
    gtk_widget_set_halign(fb, GTK_ALIGN_FILL);
    g_signal_connect_data(fb, "map", G_CALLBACK(+[](GtkWidget* w, Control* c) {
                              gtk_font_chooser_set_font(GTK_FONT_CHOOSER(w),
                                                        c->getSettings()->getFont().asString().c_str());
                          }),
                          control, nullptr, GConnectFlags(0));
    g_signal_connect(fb, "font-set", G_CALLBACK(+[](GtkFontButton* w, gpointer) {
                         if (GActionGroup* g = winActions(GTK_WIDGET(w))) {
                             const gchar* font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(w));
                             g_action_group_activate_action(g, "font", g_variant_new_string(font ? font : ""));
                         }
                     }),
                     nullptr);
    // the chooser is a window of its own: do not leave the popover grabbing the pointer behind it
    g_signal_connect(fb, "clicked", G_CALLBACK(+[](GtkButton* w, gpointer) {
                         if (GtkWidget* p = gtk_widget_get_ancestor(GTK_WIDGET(w), GTK_TYPE_POPOVER)) {
                             gtk_popover_popdown(GTK_POPOVER(p));
                         }
                     }),
                     nullptr);
    return fb;
}

/// The content of the popover of a tool, or null for a tool without options
GtkWidget* optionsOf(Control* control, ToolType type) {
    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(body, 264, -1);
    auto add = [&](GtkWidget* w) { gtk_box_pack_start(GTK_BOX(body), w, false, false, 0); };
    switch (type) {
        case TOOL_PEN:
            add(sectionLabel(_("Thickness"), true));
            add(sizeScale("tool-pen-size"));
            add(sectionLabel(_("Stroke"), false));
            add(segmented({{_("Solid"), "win.tool-pen-line-style", g_variant_new_string("plain")},
                           {_("Dashed"), "win.tool-pen-line-style", g_variant_new_string("dash")},
                           {_("Dotted"), "win.tool-pen-line-style", g_variant_new_string("dot")}}));
            add(switchRow(_("Pressure changes the thickness"),
                          settingSwitch(
                                  control, +[](Settings* s) { return s->isPressureSensitivity(); },
                                  +[](Settings* s, bool on) { s->setPressureSensitivity(on); })));
            add(switchRow(_("Smooth the stroke"), settingSwitch(control, strokeIsSmoothed, smoothStroke)));
            break;
        case TOOL_HIGHLIGHTER:
            // Xournal++ has no colours of its own for the highlighter: it uses the ones of the palette
            add(sectionLabel(_("Thickness"), true));
            add(sizeScale("tool-highlighter-size"));
            break;
        case TOOL_ERASER:
            add(sectionLabel(_("Erases"), true));
            add(segmented({{_("The whole stroke"), "win.tool-eraser-type", makeGVariant(ERASER_TYPE_DELETE_STROKE)},
                           {_("Only where it passes"), "win.tool-eraser-type", makeGVariant(ERASER_TYPE_DEFAULT)}}));
            add(sectionLabel(_("Size"), false));
            add(segmented({{C_("eraser size", "Fine"), "win.tool-eraser-size", makeGVariant(TOOL_SIZE_FINE)},
                           {C_("eraser size", "Medium"), "win.tool-eraser-size", makeGVariant(TOOL_SIZE_MEDIUM)},
                           {C_("eraser size", "Thick"), "win.tool-eraser-size", makeGVariant(TOOL_SIZE_THICK)}}));
            break;
        case TOOL_SELECT_REGION:
            add(sectionLabel(_("Select by"), true));
            add(segmented({{_("Lasso"), "win.select-tool", makeGVariant(TOOL_SELECT_REGION)},
                           {_("Rectangle"), "win.select-tool", makeGVariant(TOOL_SELECT_RECT)},
                           {_("Object"), "win.select-tool", makeGVariant(TOOL_SELECT_OBJECT)}}));
            break;
        case TOOL_TEXT:
            add(sectionLabel(_("Font"), true));
            add(fontButton(control));
            break;
        default:
            gtk_widget_destroy(body);
            return nullptr;
    }
    gtk_widget_show_all(body);
    return body;
}

struct Popovers;

struct ToolButton {
    Popovers* pops;
    GtkWidget* popover;  ///< Null for a tool without options
};

/// What the buttons of the tools share: the popovers, to close the others when the tool changes
struct Popovers {
    std::vector<GtkWidget*> all;
    std::vector<std::unique_ptr<ToolButton>> buttons;  ///< Live as long as the palette
};

void onToolToggled(GtkToggleButton* b, ToolButton* tb) {
    if (gtk_toggle_button_get_active(b)) {
        // another tool was chosen: its options are not the ones on display
        for (GtkWidget* p: tb->pops->all) {
            if (p != tb->popover) {
                gtk_popover_popdown(GTK_POPOVER(p));
            }
        }
        return;
    }
    // Released: either the user touched the tool that is already active, or another tool took its place
    GActionGroup* g = winActions(GTK_WIDGET(b));
    GVariant* state = g ? g_action_group_get_action_state(g, "select-tool") : nullptr;
    GVariant* target = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(b));
    bool stillActive = state && target && g_variant_equal(state, target);
    if (state) {
        g_variant_unref(state);
    }
    if (!stillActive) {
        return;
    }
    gtk_toggle_button_set_active(b, true);  // the active tool stays on
    if (!tb->popover) {
        return;
    }
    if (gtk_widget_get_visible(tb->popover)) {
        gtk_popover_popdown(GTK_POPOVER(tb->popover));
    } else {
        gtk_popover_popup(GTK_POPOVER(tb->popover));
    }
}

const char* selectionIcon(ToolType t) {
    switch (t) {
        case TOOL_SELECT_RECT:
            return "xopp-select-rect";
        case TOOL_SELECT_OBJECT:
            return "xopp-object-select";
        default:
            return "xopp-select-lasso";
    }
}

/// The button of the selection tools shows (and selects) the one chosen last: lasso, rectangle or object
void followSelectionTool(GtkWidget* button) {
    if (g_object_get_data(G_OBJECT(button), "amb-follows")) {
        return;
    }
    GActionGroup* g = winActions(button);
    if (!g) {
        return;
    }
    g_object_set_data(G_OBJECT(button), "amb-follows", GINT_TO_POINTER(1));
    auto update = +[](GActionGroup*, const gchar*, GVariant* value, GtkWidget* btn) {
        ToolType t = getGVariantValue<ToolType>(value);
        if (t != TOOL_SELECT_REGION && t != TOOL_SELECT_RECT && t != TOOL_SELECT_OBJECT) {
            return;
        }
        gtk_actionable_set_action_target_value(GTK_ACTIONABLE(btn), makeGVariant(t));
        gtk_image_set_from_icon_name(GTK_IMAGE(gtk_button_get_image(GTK_BUTTON(btn))), selectionIcon(t),
                                     GTK_ICON_SIZE_LARGE_TOOLBAR);
    };
    g_signal_connect_object(g, "action-state-changed::select-tool", G_CALLBACK(update), button, GConnectFlags(0));
    GVariant* st = g_action_group_get_action_state(g, "select-tool");
    if (st) {
        update(g, "select-tool", st, button);
        g_variant_unref(st);
    }
}
}  // namespace

StudioPalette::StudioPalette(Control* control, GtkOverlay* overlay) {
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
            {TOOL_PEN, "xopp-tool-pencil", _("Pen")},       {TOOL_HIGHLIGHTER, "xopp-tool-highlighter", _("Marker")},
            {TOOL_ERASER, "xopp-tool-eraser", _("Eraser")}, {TOOL_SELECT_REGION, "xopp-select-lasso", _("Selection")},
            {TOOL_TEXT, "xopp-tool-text", _("Text")},       {TOOL_LATEX, "xopp-tool-math-tex", _("LaTeX")},
    };
    auto* popovers = new Popovers;
    g_object_set_data_full(
            G_OBJECT(box), "amb-popovers", popovers, +[](gpointer p) { delete static_cast<Popovers*>(p); });
    for (auto const& t: tools) {
        GtkWidget* b = actionButton("win.select-tool", makeGVariant(t.type), nullptr);
        setTip(b, t.tip, t.type);
        gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_icon_name(t.icon, GTK_ICON_SIZE_LARGE_TOOLBAR));
        addClass(b, "amb-pal-tool");
        gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);

        // touching the tool that is already active opens its options, below the button
        GtkWidget* pop = nullptr;
        if (GtkWidget* options = optionsOf(control, t.type)) {
            pop = gtk_popover_new(b);
            gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_BOTTOM);
            addClass(pop, "amb-optpop");
            gtk_container_add(GTK_CONTAINER(pop), options);
            popovers->all.push_back(pop);
        }
        auto* tb = popovers->buttons.emplace_back(std::make_unique<ToolButton>(ToolButton{popovers, pop})).get();
        g_signal_connect(b, "toggled", G_CALLBACK(onToolToggled), tb);

        if (t.type == TOOL_SELECT_REGION) {
            g_signal_connect(b, "hierarchy-changed",
                             G_CALLBACK(+[](GtkWidget* w, GtkWidget*, gpointer) { followSelectionTool(w); }), nullptr);
        }
    }

    gtk_box_pack_start(GTK_BOX(box), separator(), false, false, 0);

    // the colours of the Summa: ink, violet, blue, orange, green
    struct Ink {
        guint32 rgb;
        const char* name;
    };
    const Ink colors[] = {{0x161616, _("Black")},
                          {0x8044ca, _("Violet")},
                          {0x2548c7, _("Blue")},
                          {0xc2410c, _("Orange")},
                          {0x0f7b55, _("Green")}};
    for (auto const& ink: colors) {
        guint32 c = ink.rgb;
        Dot dot{((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0, (c & 0xff) / 255.0, 14, true};
        Color color(static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c));
        GtkWidget* b = dotButton("win.tool-color", makeGVariant(color), nullptr, dot, "amb-pal-dot");
        setTip(b, ink.name, TOOL_NONE);
        gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    }

    gtk_widget_show_all(box);
    gtk_widget_set_no_show_all(box, true);
    gtk_widget_hide(box);
    gtk_overlay_add_overlay(overlay, box);
}

void StudioPalette::setVisible(bool visible) { gtk_widget_set_visible(box, visible); }

}  // namespace ambitus
