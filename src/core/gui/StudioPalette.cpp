#include "StudioPalette.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
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

#include "AmbitusTip.h"

namespace ambitus {

namespace {
// ---------------------------------------------------------------------------------------------------------------------
// Every tool of the panel. The ids are what the settings save: never change one.
// ---------------------------------------------------------------------------------------------------------------------

enum class Kind {
    TOOL,   ///< A tool of the select-tool action
    MODE,   ///< A drawing type of the pen (a boolean action of the window)
    AUDIO,  ///< The recording: a boolean action of the window, present only while the audio is on
};

struct Item {
    const char* id;
    Kind kind;
    ToolType tool;       ///< TOOL: the tool to select
    const char* action;  ///< MODE and AUDIO: the action of the window, without "win."
    const char* icon;
    const char* name;  ///< In the panel
    const char* tip;   ///< On the palette (null: the name)
    int group;
};

constexpr const char* GROUPS[] = {N_("Write"), N_("Select"), N_("Shapes"), N_("Insert"), N_("Move")};

constexpr Item ITEMS[] = {
        {"pencil", Kind::TOOL, TOOL_PEN, nullptr, "xopp-tool-pencil", N_("Pen"), nullptr, 0},
        {"highlighter", Kind::TOOL, TOOL_HIGHLIGHTER, nullptr, "xopp-tool-highlighter", N_("Marker"), nullptr, 0},
        {"eraser", Kind::TOOL, TOOL_ERASER, nullptr, "xopp-tool-eraser", N_("Eraser"), nullptr, 0},
        {"laser", Kind::TOOL, TOOL_LASER_POINTER_PEN, nullptr, "xopp-laser-pointer", N_("Laser pointer"), nullptr, 0},

        {"lasso", Kind::TOOL, TOOL_SELECT_REGION, nullptr, "xopp-select-lasso", N_("Lasso"), N_("Selection"), 1},
        {"rectsel", Kind::TOOL, TOOL_SELECT_RECT, nullptr, "xopp-select-rect", N_("Rectangle"), nullptr, 1},
        {"objsel", Kind::TOOL, TOOL_SELECT_OBJECT, nullptr, "xopp-object-select", N_("Object"), nullptr, 1},

        {"shapes", Kind::MODE, TOOL_NONE, "tool-draw-shape-recognizer", "xopp-shape-recognizer", N_("Shape recognizer"),
         nullptr, 2},
        {"line", Kind::MODE, TOOL_NONE, "tool-draw-line", "xopp-draw-line", N_("Line"), nullptr, 2},
        {"arrow", Kind::MODE, TOOL_NONE, "tool-draw-arrow", "xopp-draw-arrow", N_("Arrow"), nullptr, 2},
        {"rect", Kind::MODE, TOOL_NONE, "tool-draw-rectangle", "xopp-draw-rect", N_("Rectangle"), nullptr, 2},
        {"ellipse", Kind::MODE, TOOL_NONE, "tool-draw-ellipse", "xopp-draw-ellipse", N_("Ellipse"), nullptr, 2},

        {"text", Kind::TOOL, TOOL_TEXT, nullptr, "xopp-tool-text", N_("Text"), nullptr, 3},
        {"latex", Kind::TOOL, TOOL_LATEX, nullptr, "xopp-tool-math-tex", N_("LaTeX"), nullptr, 3},
        {"image", Kind::TOOL, TOOL_IMAGE, nullptr, "xopp-tool-image", N_("Image"), nullptr, 3},
        {"vspace", Kind::TOOL, TOOL_VERTICAL_SPACE, nullptr, "xopp-vertical-space", N_("Vertical space"), nullptr, 3},
        {"audio", Kind::AUDIO, TOOL_NONE, "audio-record", "xopp-audio-record", N_("Audio"), nullptr, 3},

        {"hand", Kind::TOOL, TOOL_HAND, nullptr, "xopp-hand", N_("Hand"), nullptr, 4},
};

const Item* findItem(const std::string& id) {
    for (auto const& it: ITEMS) {
        if (id == it.id) {
            return &it;
        }
    }
    return nullptr;
}

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

std::string tipMarkup(GtkWidget* w, TipInfo const& info) {
    gchar* name = g_markup_escape_text(info.name.c_str(), -1);
    std::string markup = name;
    g_free(name);
    if (std::string key = shortcutOf(w, info); !key.empty()) {
        gchar* k = g_markup_escape_text(key.c_str(), -1);
        markup += "  <span alpha=\"70%\">";
        markup += k;
        markup += "</span>";
        g_free(k);
    }
    return markup;
}

/// The buttons of the palette lie over the page, where the tooltips of GTK do not show: see AmbitusTip.h
void setTip(GtkWidget* w, const char* name, ToolType keyTool) {
    setOverlayTip(w, [w, info = TipInfo{name, keyTool}]() { return tipMarkup(w, info); });
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

/// Everything the palette knows, owned by its root widget (so the buttons never outlive it)
struct PaletteState {
    Control* control = nullptr;
    GtkWidget* box = nullptr;  ///< The palette itself
    GtkWidget* toolsBox = nullptr;
    GtkWidget* colorsBox = nullptr;
    GtkWidget* more = nullptr;      ///< The "..." button
    GtkWidget* addColor = nullptr;  ///< The "+" button
    GtkWidget* panel = nullptr;     ///< The popover with every tool
    GtkWidget* count = nullptr;     ///< "N in the palette", in the panel
    std::vector<std::string> tools;
    std::vector<uint32_t> colors;  ///< 0xRRGGBB
    Popovers pops;
    std::vector<std::pair<std::string, GtkWidget*>> pins;  ///< The pin of each row of the panel
    std::vector<GtkWidget*> audioRows;                     ///< Hidden while the audio is off
    std::vector<GtkWidget*> audioButtons;
};

namespace {

void destroyChild(GtkWidget* w, gpointer) { gtk_widget_destroy(w); }

bool has(PaletteState* st, const std::string& id) {
    return std::find(st->tools.begin(), st->tools.end(), id) != st->tools.end();
}

void saveTools(PaletteState* st) {
    st->control->getSettings()->setAmbitusPaletteTools(serializePaletteTools(st->tools));
}
void saveColors(PaletteState* st) {
    st->control->getSettings()->setAmbitusPaletteColors(serializePaletteColors(st->colors));
}

/// The audio controller is made after the window: ask when the palette is about to show, not when it is built
void refreshAudio(PaletteState* st) {
    bool on = st->control->getAudioController() != nullptr;
    for (GtkWidget* w: st->audioRows) {
        gtk_widget_set_visible(w, on);
    }
    for (GtkWidget* w: st->audioButtons) {
        gtk_widget_set_visible(w, on);
    }
}

/// The key of a tool, in capital, as the panel shows it
std::string keyLetterOf(ToolType type) {
    for (unsigned int k = 'a'; k <= 'z'; k++) {
        if (auto t = ambitus::toolForKey(k); t && *t == type) {
            return std::string(1, static_cast<char>(g_ascii_toupper(static_cast<gchar>(k))));
        }
    }
    return "";
}

// ---------------------------------------------------------------------------------------------------------------------
// Choosing an item, from the panel or from its button on the palette
// ---------------------------------------------------------------------------------------------------------------------

bool boolState(GActionGroup* g, const char* action) {
    GVariant* st = g_action_group_get_action_state(g, action);
    bool on = st && g_variant_is_of_type(st, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(st);
    if (st) {
        g_variant_unref(st);
    }
    return on;
}

/// A tool is selected. A drawing type belongs to the pen (and the marker): if the tool in use cannot draw shapes, the
/// pen is taken first. `toggle`: a second touch turns the drawing type off (the button of the palette) instead of
/// keeping it
void activateItem(GtkWidget* from, Item const& it, bool toggle) {
    GActionGroup* g = winActions(from);
    if (!g) {
        return;
    }
    switch (it.kind) {
        case Kind::TOOL:
            g_action_group_activate_action(g, "select-tool", makeGVariant(it.tool));
            break;
        case Kind::MODE:
            if (!g_action_group_get_action_enabled(g, it.action)) {
                g_action_group_activate_action(g, "select-tool", makeGVariant(TOOL_PEN));
            }
            if (toggle || !boolState(g, it.action)) {
                g_action_group_activate_action(g, it.action, nullptr);
            }
            break;
        case Kind::AUDIO:
            g_action_group_activate_action(g, it.action, nullptr);
            break;
    }
}

Item const* itemOf(GtkWidget* w) { return static_cast<Item const*>(g_object_get_data(G_OBJECT(w), "amb-item")); }

void syncMode(GtkWidget* button) {
    Item const* it = itemOf(button);
    GActionGroup* g = winActions(button);
    if (it && g) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), boolState(g, it->action));
    }
}

/// The button of a drawing type or of the recording is on while its action is
GtkWidget* modeButton(Item const& it) {
    GtkWidget* b = gtk_toggle_button_new();
    gtk_widget_set_can_focus(b, false);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(b), "amb-item", const_cast<Item*>(&it));
    g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton* w, gpointer) {
                         activateItem(GTK_WIDGET(w), *itemOf(GTK_WIDGET(w)), true);
                         syncMode(GTK_WIDGET(w));
                     }),
                     nullptr);
    g_signal_connect(b, "hierarchy-changed", G_CALLBACK(+[](GtkWidget* w, GtkWidget*, gpointer) {
                         GActionGroup* g = winActions(w);
                         if (!g || g_object_get_data(G_OBJECT(w), "amb-mirrors")) {
                             return;
                         }
                         g_object_set_data(G_OBJECT(w), "amb-mirrors", GINT_TO_POINTER(1));
                         std::string signal = std::string("action-state-changed::") + itemOf(w)->action;
                         g_signal_connect_object(g, signal.c_str(),
                                                 G_CALLBACK(+[](GActionGroup*, const gchar*, GVariant*,
                                                                GtkWidget* btn) { syncMode(btn); }),
                                                 w, GConnectFlags(0));
                         syncMode(w);
                     }),
                     nullptr);
    return b;
}

// ---------------------------------------------------------------------------------------------------------------------
// The tools of the palette
// ---------------------------------------------------------------------------------------------------------------------

void addToolButton(PaletteState* st, Item const& it, bool followSelection) {
    Popovers* popovers = &st->pops;
    GtkWidget* b =
            it.kind == Kind::TOOL ? actionButton("win.select-tool", makeGVariant(it.tool), nullptr) : modeButton(it);
    setTip(b, _(it.tip ? it.tip : it.name), it.kind == Kind::TOOL ? it.tool : TOOL_NONE);
    gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_icon_name(it.icon, GTK_ICON_SIZE_LARGE_TOOLBAR));
    addClass(b, "amb-pal-tool");
    gtk_box_pack_start(GTK_BOX(st->toolsBox), b, false, false, 0);

    if (it.kind == Kind::AUDIO) {
        gtk_widget_set_no_show_all(b, true);
        st->audioButtons.push_back(b);
    }
    if (it.kind != Kind::TOOL) {
        return;
    }

    // touching the tool that is already active opens its options, below the button
    GtkWidget* pop = nullptr;
    if (GtkWidget* options = optionsOf(st->control, it.tool)) {
        pop = gtk_popover_new(b);
        gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_BOTTOM);
        addClass(pop, "amb-optpop");
        gtk_container_add(GTK_CONTAINER(pop), options);
        popovers->all.push_back(pop);
    }
    auto* tb = popovers->buttons.emplace_back(std::make_unique<ToolButton>(ToolButton{popovers, pop})).get();
    g_signal_connect(b, "toggled", G_CALLBACK(onToolToggled), tb);

    if (it.tool == TOOL_SELECT_REGION && followSelection) {
        g_signal_connect(b, "hierarchy-changed",
                         G_CALLBACK(+[](GtkWidget* w, GtkWidget*, gpointer) { followSelectionTool(w); }), nullptr);
    }
}

void rebuildTools(PaletteState* st) {
    // the popovers first: they hang from the buttons that are about to go
    for (GtkWidget* p: st->pops.all) {
        gtk_popover_popdown(GTK_POPOVER(p));
        gtk_widget_destroy(p);
    }
    gtk_container_foreach(GTK_CONTAINER(st->toolsBox), destroyChild, nullptr);
    st->pops.all.clear();
    st->pops.buttons.clear();
    st->audioButtons.clear();

    // with the rectangle or the object pinned the lasso stays the lasso; alone it shows the selection tool used last
    bool alone = !has(st, "rectsel") && !has(st, "objsel");
    for (auto const& id: st->tools) {
        if (Item const* it = findItem(id)) {
            addToolButton(st, *it, alone);
        }
    }
    gtk_widget_show_all(st->toolsBox);
    refreshAudio(st);
}

// ---------------------------------------------------------------------------------------------------------------------
// The panel with every tool
// ---------------------------------------------------------------------------------------------------------------------

void updatePanel(PaletteState* st) {
    for (auto const& [id, pin]: st->pins) {
        bool on = std::find(st->tools.begin(), st->tools.end(), id) != st->tools.end();
        gtk_image_set_from_icon_name(GTK_IMAGE(gtk_bin_get_child(GTK_BIN(pin))), on ? "xopp-pin-filled" : "xopp-pin",
                                     GTK_ICON_SIZE_MENU);
        GtkStyleContext* ctx = gtk_widget_get_style_context(pin);
        if (on) {
            gtk_style_context_add_class(ctx, "on");
        } else {
            gtk_style_context_remove_class(ctx, "on");
        }
        gtk_widget_set_tooltip_text(pin, on ? _("Remove from the palette") : _("Pin to the palette"));
    }
    gchar* text = g_strdup_printf(_("%d in the palette"), static_cast<int>(st->tools.size()));
    gtk_label_set_text(GTK_LABEL(st->count), text);
    g_free(text);
}

void togglePin(PaletteState* st, const char* id) {
    auto at = std::find(st->tools.begin(), st->tools.end(), id);
    if (at != st->tools.end()) {
        if (st->tools.size() <= 1) {
            return;  // the palette never goes without a tool
        }
        st->tools.erase(at);
    } else {
        st->tools.emplace_back(id);
    }
    saveTools(st);
    rebuildTools(st);
    updatePanel(st);
}

GtkWidget* panelImage(const char* icon, int pixels) {
    GtkWidget* img = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
    gtk_image_set_pixel_size(GTK_IMAGE(img), pixels);
    return img;
}

struct RowData {
    PaletteState* st;
    Item const* it;
};

GtkWidget* panelRow(PaletteState* st, Item const& it) {
    auto* data = new RowData{st, &it};
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    addClass(row, "amb-pick-row");
    g_object_set_data_full(G_OBJECT(row), "amb-row", data, +[](gpointer p) { delete static_cast<RowData*>(p); });

    GtkWidget* pick = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(pick), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(pick, false);
    addClass(pick, "amb-pick");
    GtkWidget* inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(inner), panelImage(it.icon, 18), false, false, 0);
    GtkWidget* name = gtk_label_new(_(it.name));
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_box_pack_start(GTK_BOX(inner), name, true, true, 0);
    if (it.kind == Kind::TOOL) {
        if (std::string key = keyLetterOf(it.tool); !key.empty()) {
            GtkWidget* k = gtk_label_new(key.c_str());
            addClass(k, "amb-pick-key");
            gtk_box_pack_end(GTK_BOX(inner), k, false, false, 0);
        }
    }
    gtk_container_add(GTK_CONTAINER(pick), inner);
    gtk_box_pack_start(GTK_BOX(row), pick, true, true, 0);
    g_signal_connect(pick, "clicked", G_CALLBACK(+[](GtkButton* w, RowData* d) {
                         activateItem(GTK_WIDGET(w), *d->it, false);
                         gtk_popover_popdown(GTK_POPOVER(d->st->panel));
                     }),
                     data);

    GtkWidget* pin = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(pin), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(pin, false);
    gtk_widget_set_valign(pin, GTK_ALIGN_CENTER);
    addClass(pin, "amb-pin");
    gtk_container_add(GTK_CONTAINER(pin), panelImage("xopp-pin", 16));
    gtk_box_pack_end(GTK_BOX(row), pin, false, false, 0);
    g_signal_connect(pin, "clicked", G_CALLBACK(+[](GtkButton*, RowData* d) { togglePin(d->st, d->it->id); }), data);
    st->pins.emplace_back(it.id, pin);

    if (it.kind == Kind::AUDIO) {
        gtk_widget_set_no_show_all(row, true);
        st->audioRows.push_back(row);
    }
    return row;
}

void restoreDefaults(PaletteState* st);

GtkWidget* buildPanel(PaletteState* st) {
    GtkWidget* pop = gtk_popover_new(st->box);
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_BOTTOM);
    addClass(pop, "amb-optpop");

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), true);
    for (int g = 0; g < static_cast<int>(std::size(GROUPS)); g++) {
        GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_valign(col, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(col), sectionLabel(_(GROUPS[g]), true), false, false, 0);
        for (auto const& it: ITEMS) {
            if (it.group == g) {
                gtk_box_pack_start(GTK_BOX(col), panelRow(st, it), false, false, 0);
            }
        }
        gtk_grid_attach(GTK_GRID(grid), col, g % 3, g / 3, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(body), grid, false, false, 0);

    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addClass(foot, "amb-pick-foot");
    st->count = gtk_label_new("");
    addClass(st->count, "amb-pick-count");
    gtk_box_pack_start(GTK_BOX(foot), st->count, false, false, 0);
    GtkWidget* restore = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(restore), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(restore, false);
    addClass(restore, "amb-lnk");
    GtkWidget* label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(label), panelImage("xopp-undo-2", 14), false, false, 0);
    gtk_box_pack_start(GTK_BOX(label), gtk_label_new(_("Restore the default")), false, false, 0);
    gtk_container_add(GTK_CONTAINER(restore), label);
    gtk_box_pack_end(GTK_BOX(foot), restore, false, false, 0);
    g_signal_connect(restore, "clicked", G_CALLBACK(+[](GtkButton*, PaletteState* st) { restoreDefaults(st); }), st);
    gtk_box_pack_start(GTK_BOX(body), foot, false, false, 0);

    gtk_container_add(GTK_CONTAINER(pop), body);
    gtk_widget_show_all(body);
    g_signal_connect(pop, "show", G_CALLBACK(+[](GtkWidget*, PaletteState* st) { refreshAudio(st); }), st);
    // the "..." button stays down while the panel is open
    g_signal_connect(pop, "notify::visible", G_CALLBACK(+[](GObject* o, GParamSpec*, PaletteState* st) {
                         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->more),
                                                      gtk_widget_get_visible(GTK_WIDGET(o)));
                     }),
                     st);
    return pop;
}

// ---------------------------------------------------------------------------------------------------------------------
// The colours of the palette
// ---------------------------------------------------------------------------------------------------------------------

/// The colours of the Summa have a name; any other one is known by its hex
std::string colorName(uint32_t rgb) {
    switch (rgb) {
        case 0x161616:
            return C_("Color", "Black");
        case 0x8044ca:
            return C_("Color", "Violet");
        case 0x2548c7:
            return C_("Color", "Blue");
        case 0xc2410c:
            return C_("Color", "Orange");
        case 0x0f7b55:
            return C_("Color", "Green");
        default:
            return paletteColorHex(rgb);
    }
}

void rebuildColors(PaletteState* st) {
    gtk_container_foreach(GTK_CONTAINER(st->colorsBox), destroyChild, nullptr);
    for (uint32_t c: st->colors) {
        Dot dot{((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0, (c & 0xff) / 255.0, 14, true};
        Color color(static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c));
        GtkWidget* b = dotButton("win.tool-color", makeGVariant(color), nullptr, dot, "amb-pal-dot");
        setTip(b, colorName(c).c_str(), TOOL_NONE);
        g_object_set_data(G_OBJECT(b), "amb-rgb", GUINT_TO_POINTER(c));
        // the right button takes the colour off the palette (the last one stays)
        g_signal_connect(
                b, "button-press-event",
                G_CALLBACK(+[](GtkWidget* w, GdkEventButton* ev, PaletteState* st) -> gboolean {
                    if (ev->button != GDK_BUTTON_SECONDARY) {
                        return FALSE;
                    }
                    auto rgb = static_cast<uint32_t>(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "amb-rgb")));
                    if (st->colors.size() > 1) {
                        st->colors.erase(std::remove(st->colors.begin(), st->colors.end(), rgb), st->colors.end());
                        saveColors(st);
                        // not from inside the event of the button that goes away
                        g_idle_add(
                                +[](gpointer p) -> gboolean {
                                    rebuildColors(static_cast<PaletteState*>(p));
                                    return G_SOURCE_REMOVE;
                                },
                                st);
                    }
                    return TRUE;
                }),
                st);
        gtk_box_pack_start(GTK_BOX(st->colorsBox), b, false, false, 0);
    }
    gtk_widget_show_all(st->colorsBox);
    gtk_widget_set_visible(st->addColor, st->colors.size() < PALETTE_MAX_COLORS);
}

void addColor(PaletteState* st, uint32_t rgb) {
    if (std::find(st->colors.begin(), st->colors.end(), rgb) == st->colors.end() &&
        st->colors.size() < PALETTE_MAX_COLORS) {
        st->colors.push_back(rgb);
        saveColors(st);
        rebuildColors(st);
    }
    if (GActionGroup* g = winActions(st->box)) {
        Color color(static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb));
        g_action_group_activate_action(g, "tool-color", makeGVariant(color));
    }
}

void askColor(PaletteState* st) {
    GtkWidget* top = gtk_widget_get_toplevel(st->box);
    GtkWidget* dialog =
            gtk_color_chooser_dialog_new(_("Another color"), GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : nullptr);
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dialog), false);
    g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* d, int response, PaletteState* st) {
                         if (response == GTK_RESPONSE_OK) {
                             GdkRGBA c;
                             gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(d), &c);
                             auto byte = [](double v) { return static_cast<uint32_t>(std::lround(v * 255.0)) & 0xff; };
                             addColor(st, (byte(c.red) << 16) | (byte(c.green) << 8) | byte(c.blue));
                         }
                         gtk_widget_destroy(GTK_WIDGET(d));
                     }),
                     st);
    gtk_window_present(GTK_WINDOW(dialog));
}

void restoreDefaults(PaletteState* st) {
    st->tools = defaultPaletteTools();
    st->colors = defaultPaletteColors();
    saveTools(st);
    saveColors(st);
    rebuildTools(st);
    rebuildColors(st);
    updatePanel(st);
}

GtkWidget* paletteButton(const char* icon, const char* tip, GtkWidget* (*make)()) {
    GtkWidget* b = make();
    gtk_widget_set_can_focus(b, false);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    gtk_button_set_image(GTK_BUTTON(b), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_LARGE_TOOLBAR));
    setOverlayTipText(b, tip);
    return b;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// What the settings keep
// ---------------------------------------------------------------------------------------------------------------------

namespace {
std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> out;
    size_t from = 0;
    while (from <= text.size()) {
        size_t to = text.find(',', from);
        if (to == std::string::npos) {
            to = text.size();
        }
        std::string item = text.substr(from, to - from);
        size_t a = item.find_first_not_of(" \t\r\n");
        size_t b = item.find_last_not_of(" \t\r\n");
        if (a != std::string::npos) {
            out.push_back(item.substr(a, b - a + 1));
        }
        from = to + 1;
    }
    return out;
}

std::string joinList(const std::vector<std::string>& items) {
    std::string out;
    for (auto const& i: items) {
        if (!out.empty()) {
            out += ',';
        }
        out += i;
    }
    return out;
}
}  // namespace

std::vector<std::string> defaultPaletteTools() { return {"pencil", "highlighter", "eraser", "lasso", "text", "latex"}; }

std::vector<uint32_t> defaultPaletteColors() { return {0x161616, 0x8044ca, 0x2548c7, 0xc2410c, 0x0f7b55}; }

std::vector<std::string> parsePaletteTools(const std::string& text) {
    std::vector<std::string> tools;
    for (auto& id: splitList(text)) {
        if (findItem(id) && std::find(tools.begin(), tools.end(), id) == tools.end()) {
            tools.push_back(std::move(id));
        }
    }
    return tools.empty() ? defaultPaletteTools() : tools;
}

std::string serializePaletteTools(const std::vector<std::string>& tools) { return joinList(tools); }

std::vector<uint32_t> parsePaletteColors(const std::string& text) {
    std::vector<uint32_t> colors;
    for (auto const& item: splitList(text)) {
        std::string hex = !item.empty() && item[0] == '#' ? item.substr(1) : item;
        if (hex.size() != 6 || !std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c); })) {
            continue;
        }
        auto rgb = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
        if (colors.size() < PALETTE_MAX_COLORS && std::find(colors.begin(), colors.end(), rgb) == colors.end()) {
            colors.push_back(rgb);
        }
    }
    return colors.empty() ? defaultPaletteColors() : colors;
}

std::string serializePaletteColors(const std::vector<uint32_t>& colors) {
    std::vector<std::string> hex;
    for (uint32_t c: colors) {
        char buf[8];
        g_snprintf(buf, sizeof(buf), "%06x", c & 0xffffffU);
        hex.emplace_back(buf);
    }
    return joinList(hex);
}

std::string paletteColorHex(uint32_t rgb) {
    char buf[8];
    g_snprintf(buf, sizeof(buf), "#%06X", rgb & 0xffffffU);
    return buf;
}

StudioPalette::StudioPalette(Control* control, GtkOverlay* overlay) {
    state = new PaletteState;
    state->control = control;
    state->tools = parsePaletteTools(control->getSettings()->getAmbitusPaletteTools());
    state->colors = parsePaletteColors(control->getSettings()->getAmbitusPaletteColors());

    // the event box catches the right button over the gaps of the palette too
    root = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(root), false);
    g_object_set_data_full(
            G_OBJECT(root), "amb-palette", state, +[](gpointer p) { delete static_cast<PaletteState*>(p); });
    gtk_widget_set_halign(root, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(root, GTK_ALIGN_START);
    gtk_widget_set_margin_top(root, 12);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(box, "amb-palette");
    gtk_container_add(GTK_CONTAINER(root), box);
    state->box = box;

    state->toolsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_start(GTK_BOX(box), state->toolsBox, false, false, 0);

    state->more = paletteButton("xopp-ellipsis", _("All tools"), gtk_toggle_button_new);
    addClass(state->more, "amb-pal-more");
    gtk_box_pack_start(GTK_BOX(box), state->more, false, false, 0);

    gtk_box_pack_start(GTK_BOX(box), separator(), false, false, 0);

    state->colorsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_start(GTK_BOX(box), state->colorsBox, false, false, 0);
    state->addColor = paletteButton("xopp-plus", _("Another color"), gtk_button_new);
    gtk_box_pack_start(GTK_BOX(box), state->addColor, false, false, 0);
    g_signal_connect(state->addColor, "clicked", G_CALLBACK(+[](GtkButton*, PaletteState* st) { askColor(st); }),
                     state);

    state->panel = buildPanel(state);
    g_signal_connect(state->more, "toggled", G_CALLBACK(+[](GtkToggleButton* b, PaletteState* st) {
                         if (gtk_toggle_button_get_active(b)) {
                             gtk_popover_popup(GTK_POPOVER(st->panel));
                         } else {
                             gtk_popover_popdown(GTK_POPOVER(st->panel));
                         }
                     }),
                     state);
    gtk_widget_add_events(root, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(root, "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton* ev, PaletteState* st) -> gboolean {
                         if (ev->button != GDK_BUTTON_SECONDARY) {
                             return FALSE;
                         }
                         gtk_popover_popup(GTK_POPOVER(st->panel));
                         return TRUE;
                     }),
                     state);

    rebuildTools(state);
    rebuildColors(state);
    updatePanel(state);

    gtk_widget_show_all(root);
    gtk_widget_set_no_show_all(root, true);
    gtk_widget_hide(root);
    gtk_overlay_add_overlay(overlay, root);
}

void StudioPalette::setVisible(bool visible) {
    gtk_widget_set_visible(root, visible);
    if (visible) {
        refreshAudio(state);
    }
}

}  // namespace ambitus
