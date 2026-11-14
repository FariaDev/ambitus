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
#include "MainWindow.h"
#include "RadialMenu.h"
#include "TitleBar.h"

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

std::string escapeMarkup(const std::string& text) {
    gchar* e = g_markup_escape_text(text.c_str(), -1);
    std::string out = e;
    g_free(e);
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

// what the favourites need from the palette, defined with it below
void saveFavorite(PaletteState* st, ToolType type);
bool favoritesFull(PaletteState* st);
void registerSaveButton(PaletteState* st, GtkWidget* button);

struct SaveFavorite {
    PaletteState* st;
    ToolType type;
};

/// "Save as favorite", at the end of the options of the pen and of the marker: tool, colour and thickness in use
GtkWidget* favoriteFoot(PaletteState* st, ToolType type) {
    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    addClass(foot, "amb-fav-foot");
    // a disabled button gets no pointer events: its tip hangs from a box around it, which takes the events over
    // (above_child) only while the button is disabled
    GtkWidget* around = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(around), false);
    gtk_container_add(GTK_CONTAINER(around), foot);
    GtkWidget* b = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(b, false);
    gtk_widget_set_halign(b, GTK_ALIGN_START);
    addClass(b, "amb-lnk");
    addClass(b, "amb-fav-save");
    GtkWidget* label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* star = gtk_image_new_from_icon_name("xopp-star", GTK_ICON_SIZE_MENU);
    gtk_image_set_pixel_size(GTK_IMAGE(star), 14);
    gtk_box_pack_start(GTK_BOX(label), star, false, false, 0);
    gtk_box_pack_start(GTK_BOX(label), gtk_label_new(_("Save as favorite")), false, false, 0);
    gtk_container_add(GTK_CONTAINER(b), label);
    gtk_box_pack_start(GTK_BOX(foot), b, false, false, 0);
    g_signal_connect_data(
            b, "clicked", G_CALLBACK(+[](GtkButton*, SaveFavorite* d) { saveFavorite(d->st, d->type); }),
            new SaveFavorite{st, type}, +[](gpointer p, GClosure*) { delete static_cast<SaveFavorite*>(p); },
            GConnectFlags(0));
    g_object_set_data(G_OBJECT(b), "amb-around", around);
    // a disabled button says why (the tip of the palette: it also shows over a popover)
    setOverlayTip(around, [st]() -> std::string {
        return favoritesFull(st) ?
                       escapeMarkup(_("Only 4 favorites fit. Remove one with the right button to save another.")) :
                       "";
    });
    registerSaveButton(st, b);
    return around;
}

/// The content of the popover of a tool, or null for a tool without options
GtkWidget* optionsOf(Control* control, PaletteState* st, ToolType type) {
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
            add(favoriteFoot(st, type));
            break;
        case TOOL_HIGHLIGHTER:
            // Xournal++ has no colours of its own for the highlighter: it uses the ones of the palette
            add(sectionLabel(_("Thickness"), true));
            add(sizeScale("tool-highlighter-size"));
            add(favoriteFoot(st, type));
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
    GtkWidget* favsBox = nullptr;  ///< The favourite pens, before the tools
    GtkWidget* favSep = nullptr;   ///< Between the favourites and the tools: hidden with no favourite
    GtkWidget* toolsBox = nullptr;
    GtkWidget* colorsBox = nullptr;
    GtkWidget* more = nullptr;      ///< The "..." button
    GtkWidget* addColor = nullptr;  ///< The "+" button
    GtkWidget* panel = nullptr;     ///< The popover with every tool
    GtkWidget* count = nullptr;     ///< "N in the palette", in the panel
    std::vector<std::string> tools;
    std::vector<uint32_t> colors;  ///< 0xRRGGBB
    std::vector<PaletteFavorite> favorites;
    std::vector<GtkWidget*> favButtons;   ///< One for each favourite
    bool syncingFavorites = false;        ///< set_active of a toggle button also emits "clicked"
    std::vector<GtkWidget*> saveButtons;  ///< "Save as favorite", in the popovers of the pen and of the marker
    Popovers pops;
    std::vector<std::pair<std::string, GtkWidget*>> pins;  ///< The pin of each row of the panel
    std::vector<GtkWidget*> audioRows;                     ///< Hidden while the audio is off
    std::vector<GtkWidget*> audioButtons;

    // the "Radial menu" tab of the panel
    GtkWidget* stack = nullptr;  ///< "tools" and "radial"
    GtkWidget* tabTools = nullptr;
    GtkWidget* tabRadial = nullptr;
    bool syncingTabs = false;  ///< set_active of a toggle button also emits "clicked"
    GtkWidget* ring = nullptr;
    GtkWidget* radialTitle = nullptr;
    std::vector<std::string> radialSlots;                   ///< The option of each position, as saved
    int radialSel = 0;                                      ///< The position being edited
    int radialHover = -1;                                   ///< The position under the pointer in the ring
    std::vector<std::pair<std::string, GtkWidget*>> chips;  ///< One for each option
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
void saveFavorites(PaletteState* st) {
    st->control->getSettings()->setAmbitusPaletteFavorites(serializePaletteFavorites(st->favorites));
}
void saveRadial(PaletteState* st) {
    st->control->getSettings()->setAmbitusRadialSlots(serializeRadialSlots(st->radialSlots));
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
        // set_active also emits "clicked": the handler must not take it for a touch and apply the action again
        g_object_set_data(G_OBJECT(button), "amb-syncing", GINT_TO_POINTER(1));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), boolState(g, it->action));
        g_object_set_data(G_OBJECT(button), "amb-syncing", nullptr);
    }
}

/// The button of a drawing type or of the recording is on while its action is
GtkWidget* modeButton(Item const& it) {
    GtkWidget* b = gtk_toggle_button_new();
    gtk_widget_set_can_focus(b, false);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    g_object_set_data(G_OBJECT(b), "amb-item", const_cast<Item*>(&it));
    g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton* w, gpointer) {
                         if (g_object_get_data(G_OBJECT(w), "amb-syncing")) {
                             return;
                         }
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
    if (GtkWidget* options = optionsOf(st->control, st, it.tool)) {
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
    st->saveButtons.clear();
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

// ---------------------------------------------------------------------------------------------------------------------
// The "Radial menu" tab: a small ring to pick a position, and every option as a chip
// ---------------------------------------------------------------------------------------------------------------------

constexpr double RING_SIZE = 184;  ///< Side of the drawing area
constexpr double RING_R = 66;      ///< Distance from the centre to the positions
constexpr double RING_ITEM = 34;   ///< Diameter of a position
constexpr double RING_HOLE = 60;   ///< Diameter of the hole in the middle

GdkRGBA ringColor(GtkWidget* w, bool dark, const char* light, const char* darkName) {
    GdkRGBA c;
    if (!gtk_style_context_lookup_color(gtk_widget_get_style_context(w), dark ? darkName : light, &c)) {
        gdk_rgba_parse(&c, dark ? "#c6c6c6" : "#525252");
    }
    return c;
}

void ringCircle(cairo_t* cr, double x, double y, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y, r, 0, 2 * M_PI);
}

gboolean drawRing(GtkWidget* w, cairo_t* cr, PaletteState* st) {
    bool dark = gtk_style_context_has_class(gtk_widget_get_style_context(gtk_widget_get_toplevel(w)), "darkMode");
    GdkRGBA fill = ringColor(w, dark, "amb_layer", "amb_dk_bg");
    GdkRGBA hole = ringColor(w, dark, "amb_bg", "amb_dk_layer");
    GdkRGBA border = ringColor(w, dark, "amb_border", "amb_dk_border");
    GdkRGBA hover = ringColor(w, dark, "amb_hover", "amb_dk_hover");
    GdkRGBA selected = ringColor(w, dark, "amb_selected", "amb_dk_selected");
    GdkRGBA text = ringColor(w, dark, "amb_text", "amb_dk_text");
    GdkRGBA text2 = ringColor(w, dark, "amb_text2", "amb_dk_text2");
    GdkRGBA accent = ringColor(w, dark, "amb_accent", "amb_dk_accent");

    double cx = gtk_widget_get_allocated_width(w) / 2.0, cy = gtk_widget_get_allocated_height(w) / 2.0;
    gdk_cairo_set_source_rgba(cr, &fill);
    ringCircle(cr, cx, cy, RING_SIZE / 2 - 1);
    cairo_fill_preserve(cr);
    gdk_cairo_set_source_rgba(cr, &border);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    gdk_cairo_set_source_rgba(cr, &hole);
    ringCircle(cr, cx, cy, RING_HOLE / 2);
    cairo_fill_preserve(cr);
    gdk_cairo_set_source_rgba(cr, &border);
    cairo_stroke(cr);

    // the name of what sits in the chosen position
    if (const RadialOption* o = findRadialOption(st->radialSlots[static_cast<size_t>(st->radialSel)])) {
        PangoLayout* l = gtk_widget_create_pango_layout(w, _(o->name));
        PangoFontDescription* fd = pango_font_description_new();
        pango_font_description_set_size(fd, static_cast<gint>(8 * PANGO_SCALE));
        pango_layout_set_font_description(l, fd);
        pango_font_description_free(fd);
        pango_layout_set_width(l, static_cast<int>(RING_HOLE - 10) * PANGO_SCALE);
        pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
        int lw, lh;
        pango_layout_get_pixel_size(l, &lw, &lh);
        gdk_cairo_set_source_rgba(cr, &text);
        cairo_move_to(cr, cx - (RING_HOLE - 10) / 2.0, cy - lh / 2.0);
        pango_cairo_show_layout(cr, l);
        g_object_unref(l);
    }

    int scale = gtk_widget_get_scale_factor(w);
    GtkIconTheme* theme = gtk_icon_theme_get_for_screen(gtk_widget_get_screen(w));
    for (size_t i = 0; i < RADIAL_SLOTS; i++) {
        double a = -M_PI / 2 + static_cast<double>(i) * 2 * M_PI / RADIAL_SLOTS;
        double x = cx + std::cos(a) * RING_R, y = cy + std::sin(a) * RING_R;
        bool sel = static_cast<int>(i) == st->radialSel;
        if (sel || static_cast<int>(i) == st->radialHover) {
            gdk_cairo_set_source_rgba(cr, sel ? &selected : &hover);
            ringCircle(cr, x, y, RING_ITEM / 2);
            cairo_fill_preserve(cr);
            if (sel) {
                gdk_cairo_set_source_rgba(cr, &accent);
                cairo_set_line_width(cr, 1.5);
                cairo_stroke(cr);
            } else {
                cairo_new_path(cr);
            }
        }
        const RadialOption* o = findRadialOption(st->radialSlots[i]);
        if (!o) {
            continue;
        }
        if (cairo_surface_t* icon = gtk_icon_theme_load_surface(theme, o->icon, 16, scale, nullptr,
                                                                GTK_ICON_LOOKUP_FORCE_SIZE, nullptr)) {
            gdk_cairo_set_source_rgba(cr, sel ? &text : &text2);
            cairo_mask_surface(cr, icon, std::round(x - 8), std::round(y - 8));
            cairo_surface_destroy(icon);
        }
    }
    return true;
}

/// The position under (x, y) of the ring, -1 for none (RadialMenu::itemAt knows the big ring: bring the point to it)
int ringPositionAt(double x, double y) {
    double k = RadialMenu::RADIUS / RING_R;
    return RadialMenu::itemAt((x - RING_SIZE / 2) * k, (y - RING_SIZE / 2) * k, RADIAL_SLOTS);
}

void updateRadialPanel(PaletteState* st) {
    gtk_widget_queue_draw(st->ring);
    gchar* title = g_strdup_printf(_("Position %d: choose what goes in it"), st->radialSel + 1);
    gtk_label_set_text(GTK_LABEL(st->radialTitle), title);
    g_free(title);
    const std::string& here = st->radialSlots[static_cast<size_t>(st->radialSel)];
    for (auto const& [id, chip]: st->chips) {
        GtkStyleContext* ctx = gtk_widget_get_style_context(chip);
        if (id == here) {
            gtk_style_context_add_class(ctx, "on");
        } else {
            gtk_style_context_remove_class(ctx, "on");
        }
    }
}

struct ChipData {
    PaletteState* st;
    const char* id;
};

GtkWidget* radialChip(PaletteState* st, RadialOption const& o) {
    GtkWidget* b = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(b, false);
    addClass(b, "amb-chip");
    GtkWidget* inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(inner), panelImage(o.icon, 16), false, false, 0);
    GtkWidget* name = gtk_label_new(_(o.name));
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_box_pack_start(GTK_BOX(inner), name, true, true, 0);
    gtk_container_add(GTK_CONTAINER(b), inner);
    g_signal_connect_data(
            b, "clicked", G_CALLBACK(+[](GtkButton*, ChipData* d) {
                d->st->radialSlots[static_cast<size_t>(d->st->radialSel)] = d->id;
                saveRadial(d->st);
                updateRadialPanel(d->st);
            }),
            new ChipData{st, o.id}, +[](gpointer p, GClosure*) { delete static_cast<ChipData*>(p); }, GConnectFlags(0));
    st->chips.emplace_back(o.id, b);
    return b;
}

GtkWidget* buildRadialPage(PaletteState* st) {
    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    st->ring = gtk_drawing_area_new();
    gtk_widget_set_size_request(st->ring, static_cast<int>(RING_SIZE), static_cast<int>(RING_SIZE));
    gtk_widget_set_halign(st->ring, GTK_ALIGN_CENTER);
    gtk_widget_add_events(st->ring, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(st->ring, "draw", G_CALLBACK(drawRing), st);
    g_signal_connect(st->ring, "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton* ev, PaletteState* st) -> gboolean {
                         if (ev->button != GDK_BUTTON_PRIMARY) {
                             return FALSE;
                         }
                         if (int i = ringPositionAt(ev->x, ev->y); i >= 0) {
                             st->radialSel = i;
                             updateRadialPanel(st);
                         }
                         return TRUE;
                     }),
                     st);
    g_signal_connect(st->ring, "motion-notify-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventMotion* ev, PaletteState* st) -> gboolean {
                         int i = ringPositionAt(ev->x, ev->y);
                         if (i != st->radialHover) {
                             st->radialHover = i;
                             gtk_widget_queue_draw(w);
                         }
                         return FALSE;
                     }),
                     st);
    g_signal_connect(st->ring, "leave-notify-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventCrossing*, PaletteState* st) -> gboolean {
                         st->radialHover = -1;
                         gtk_widget_queue_draw(w);
                         return FALSE;
                     }),
                     st);
    gtk_box_pack_start(GTK_BOX(page), st->ring, false, false, 0);

    st->radialTitle = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(st->radialTitle), 0);
    addClass(st->radialTitle, "amb-radial-title");
    gtk_box_pack_start(GTK_BOX(page), st->radialTitle, false, false, 0);

    GtkWidget* flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), true);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 6);
    for (auto const& o: radialOptions()) {
        gtk_container_add(GTK_CONTAINER(flow), radialChip(st, o));
    }
    gtk_box_pack_start(GTK_BOX(page), flow, false, false, 0);
    return page;
}

/// Shows a tab of the panel; the buttons follow it
void showTab(PaletteState* st, bool radial) {
    gtk_stack_set_visible_child_name(GTK_STACK(st->stack), radial ? "radial" : "tools");
    gtk_widget_set_visible(st->count, !radial);
    st->syncingTabs = true;  // set_active also emits "clicked"
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->tabTools), !radial);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->tabRadial), radial);
    st->syncingTabs = false;
}

struct TabData {
    PaletteState* st;
    bool radial;
};

GtkWidget* tabButton(PaletteState* st, const char* label, bool radial) {
    GtkWidget* b = gtk_toggle_button_new_with_label(label);
    gtk_widget_set_can_focus(b, false);
    g_signal_connect_data(
            b, "clicked", G_CALLBACK(+[](GtkButton*, TabData* d) {
                if (!d->st->syncingTabs) {
                    showTab(d->st, d->radial);  // touching the tab already on keeps it on
                }
            }),
            new TabData{st, radial}, +[](gpointer p, GClosure*) { delete static_cast<TabData*>(p); }, GConnectFlags(0));
    return b;
}

void restoreDefaults(PaletteState* st);

GtkWidget* buildPanel(PaletteState* st) {
    GtkWidget* pop = gtk_popover_new(st->box);
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_BOTTOM);
    addClass(pop, "amb-optpop");

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // two tabs: the tools of the palette, and the ring of the right button
    GtkWidget* tabs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(tabs, "amb-seg");
    gtk_widget_set_halign(tabs, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(tabs, 12);
    st->tabTools = tabButton(st, _("Tools"), false);
    st->tabRadial = tabButton(st, _("Radial menu"), true);
    gtk_box_pack_start(GTK_BOX(tabs), st->tabTools, false, false, 0);
    gtk_box_pack_start(GTK_BOX(tabs), st->tabRadial, false, false, 0);
    gtk_box_pack_start(GTK_BOX(body), tabs, false, false, 0);
    st->stack = gtk_stack_new();
    gtk_stack_set_hhomogeneous(GTK_STACK(st->stack), true);
    gtk_stack_set_vhomogeneous(GTK_STACK(st->stack), false);
    gtk_stack_set_interpolate_size(GTK_STACK(st->stack), false);
    gtk_box_pack_start(GTK_BOX(body), st->stack, false, false, 0);

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
    gtk_stack_add_named(GTK_STACK(st->stack), grid, "tools");
    gtk_stack_add_named(GTK_STACK(st->stack), buildRadialPage(st), "radial");

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
    showTab(st, false);
    updateRadialPanel(st);
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

// ---------------------------------------------------------------------------------------------------------------------
// The favourite pens: tool, colour and thickness together
// ---------------------------------------------------------------------------------------------------------------------

const char* favoriteSizeAction(const std::string& tool) {
    return tool == "highlighter" ? "tool-highlighter-size" : "tool-pen-size";
}

/// The thickness names Xournal++ already has, in lower case for the tip
std::string sizeName(int size) {
    const char* name = "";
    switch (size) {
        case TOOL_SIZE_VERY_FINE:
            name = _("Very thin");
            break;
        case TOOL_SIZE_FINE:
            name = _("Thin");
            break;
        case TOOL_SIZE_MEDIUM:
            name = _("Medium");
            break;
        case TOOL_SIZE_THICK:
            name = _("Thick");
            break;
        case TOOL_SIZE_VERY_THICK:
            name = _("Very thick");
            break;
        default:
            break;
    }
    gchar* low = g_utf8_strdown(name, -1);
    std::string out = low;
    g_free(low);
    return out;
}

/// Tool, colour and thickness at once, through the actions of the window
void applyFavorite(GActionGroup* g, PaletteFavorite const& f) {
    Item const* it = findItem(f.tool);
    if (!g || !it) {
        return;
    }
    g_action_group_activate_action(g, "select-tool", makeGVariant(it->tool));
    Color color(static_cast<uint8_t>(f.color >> 16), static_cast<uint8_t>(f.color >> 8), static_cast<uint8_t>(f.color));
    g_action_group_activate_action(g, "tool-color", makeGVariant(color));
    g_action_group_activate_action(g, favoriteSizeAction(f.tool), makeGVariant(static_cast<ToolSize>(f.size)));
}

/// The tool, colour and thickness in use now, as a favourite (an empty tool: the tool in use cannot be a favourite)
PaletteFavorite currentFavorite(GActionGroup* g) {
    PaletteFavorite f;
    GVariant* tool = g_action_group_get_action_state(g, "select-tool");
    if (!tool) {
        return f;
    }
    ToolType type = getGVariantValue<ToolType>(tool);
    g_variant_unref(tool);
    if (type != TOOL_PEN && type != TOOL_HIGHLIGHTER) {
        return f;
    }
    f.tool = type == TOOL_PEN ? "pencil" : "highlighter";
    if (GVariant* color = g_action_group_get_action_state(g, "tool-color")) {
        f.color = static_cast<uint32_t>(getGVariantValue<Color>(color)) & 0xffffffU;
        g_variant_unref(color);
    }
    if (GVariant* size = g_action_group_get_action_state(g, favoriteSizeAction(f.tool))) {
        f.size = static_cast<int>(getGVariantValue<ToolSize>(size));
        g_variant_unref(size);
    }
    return f;
}

/// A favourite is on while the tool, the colour and the thickness in use are its own
void syncFavorites(PaletteState* st) {
    GActionGroup* g = winActions(st->box);
    PaletteFavorite now = g ? currentFavorite(g) : PaletteFavorite{};
    st->syncingFavorites = true;
    for (size_t i = 0; i < st->favButtons.size() && i < st->favorites.size(); i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(st->favButtons[i]),
                                     !now.tool.empty() && now == st->favorites[i]);
    }
    st->syncingFavorites = false;
}

void followFavorites(GtkWidget* box) {
    if (g_object_get_data(G_OBJECT(box), "amb-follows")) {
        return;
    }
    GActionGroup* g = winActions(box);
    if (!g) {
        return;
    }
    g_object_set_data(G_OBJECT(box), "amb-follows", GINT_TO_POINTER(1));
    auto update = +[](GActionGroup*, const gchar*, GVariant*, GtkWidget* b) {
        if (auto* st = static_cast<PaletteState*>(g_object_get_data(G_OBJECT(b), "amb-state"))) {
            syncFavorites(st);
        }
    };
    for (const char* action: {"select-tool", "tool-color", "tool-pen-size", "tool-highlighter-size"}) {
        std::string signal = std::string("action-state-changed::") + action;
        g_signal_connect_object(g, signal.c_str(), G_CALLBACK(update), box, GConnectFlags(0));
    }
}

/// The circle of a favourite: its colour, with the icon of the tool in white (dark, on a light colour)
struct FavoriteLook {
    uint32_t rgb;
    const char* icon;
};

gboolean drawFavorite(GtkWidget* w, cairo_t* cr, FavoriteLook* look) {
    constexpr int ICON = 13;
    double cx = gtk_widget_get_allocated_width(w) / 2.0;
    double cy = gtk_widget_get_allocated_height(w) / 2.0;
    double r = ((look->rgb >> 16) & 0xff) / 255.0, g = ((look->rgb >> 8) & 0xff) / 255.0,
           b = (look->rgb & 0xff) / 255.0;
    GtkWidget* button = gtk_widget_get_parent(w);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_arc(cr, cx, cy, 12, 0, 2 * M_PI);
    GdkRGBA text;
    gtk_style_context_get_color(gtk_widget_get_style_context(button), gtk_widget_get_state_flags(button), &text);
    if (text.green > 0.5 && g < 0.2) {  // a dark colour would vanish on the dark theme: a thin light ring keeps it
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0xf4 / 255.0, 0xf4 / 255.0, 0xf4 / 255.0);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
    } else {
        cairo_fill(cr);
    }
    // the icons of the theme have their colour in the file: use them as a mask and paint it white or dark
    int scale = gtk_widget_get_scale_factor(w);
    GtkIconTheme* theme = gtk_icon_theme_get_for_screen(gtk_widget_get_screen(w));
    if (cairo_surface_t* icon = gtk_icon_theme_load_surface(theme, look->icon, ICON, scale, nullptr,
                                                            GTK_ICON_LOOKUP_FORCE_SIZE, nullptr)) {
        if (paletteColorIsLight(look->rgb)) {
            cairo_set_source_rgb(cr, 0x16 / 255.0, 0x16 / 255.0, 0x16 / 255.0);
        } else {
            cairo_set_source_rgb(cr, 1, 1, 1);
        }
        cairo_mask_surface(cr, icon, std::round(cx - ICON / 2.0), std::round(cy - ICON / 2.0));
        cairo_surface_destroy(icon);
    }
    return true;
}

void enableSaveButton(GtkWidget* button, bool on) {
    gtk_widget_set_sensitive(button, on);
    if (auto* around = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(button), "amb-around"))) {
        gtk_event_box_set_above_child(GTK_EVENT_BOX(around), !on);
    }
}

void rebuildFavorites(PaletteState* st) {
    gtk_container_foreach(GTK_CONTAINER(st->favsBox), destroyChild, nullptr);
    st->favButtons.clear();
    for (size_t i = 0; i < st->favorites.size(); i++) {
        PaletteFavorite const& fav = st->favorites[i];
        Item const* it = findItem(fav.tool);
        if (!it) {
            continue;
        }
        GtkWidget* b = gtk_toggle_button_new();
        gtk_widget_set_can_focus(b, false);
        gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
        addClass(b, "amb-pal-tool");  // marked like the tools
        addClass(b, "amb-pal-fav");
        GtkWidget* area = gtk_drawing_area_new();
        gtk_widget_set_size_request(area, 24, 24);
        g_signal_connect_data(
                area, "draw", G_CALLBACK(drawFavorite), new FavoriteLook{fav.color, it->icon},
                +[](gpointer p, GClosure*) { delete static_cast<FavoriteLook*>(p); }, GConnectFlags(0));
        g_signal_connect_swapped(b, "toggled", G_CALLBACK(gtk_widget_queue_draw), area);
        gtk_container_add(GTK_CONTAINER(b), area);
        gtk_widget_show_all(b);

        // "Pen · Violet · thin", and the number key of its position
        setOverlayTip(b, [name = std::string(_(it->name)), color = colorName(fav.color), size = sizeName(fav.size),
                          key = std::to_string(i + 1)]() {
            return escapeMarkup(name + " · " + color + " · " + size) + "  <span alpha=\"70%\">" + key + "</span>";
        });

        g_object_set_data_full(
                G_OBJECT(b), "amb-fav", new PaletteFavorite(fav),
                +[](gpointer p) { delete static_cast<PaletteFavorite*>(p); });
        g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton* w, PaletteState* st) {
                             if (st->syncingFavorites) {
                                 return;  // the button was only brought in step with the tool, not touched
                             }
                             auto* f = static_cast<PaletteFavorite*>(g_object_get_data(G_OBJECT(w), "amb-fav"));
                             applyFavorite(winActions(GTK_WIDGET(w)), *f);
                             syncFavorites(st);
                         }),
                         st);
        // the right button takes it off, with no question (and does not reach the palette, which opens the panel)
        g_signal_connect(b, "button-press-event",
                         G_CALLBACK(+[](GtkWidget* w, GdkEventButton* ev, PaletteState* st) -> gboolean {
                             if (ev->button != GDK_BUTTON_SECONDARY) {
                                 return FALSE;
                             }
                             auto* f = static_cast<PaletteFavorite*>(g_object_get_data(G_OBJECT(w), "amb-fav"));
                             st->favorites.erase(std::remove(st->favorites.begin(), st->favorites.end(), *f),
                                                 st->favorites.end());
                             saveFavorites(st);
                             // not from inside the event of the button that goes away
                             g_idle_add(
                                     +[](gpointer p) -> gboolean {
                                         rebuildFavorites(static_cast<PaletteState*>(p));
                                         return G_SOURCE_REMOVE;
                                     },
                                     st);
                             return TRUE;
                         }),
                         st);
        gtk_box_pack_start(GTK_BOX(st->favsBox), b, false, false, 0);
        st->favButtons.push_back(b);
    }
    gtk_widget_set_visible(st->favsBox, !st->favorites.empty());
    gtk_widget_set_visible(st->favSep, !st->favorites.empty());
    for (GtkWidget* b: st->saveButtons) {
        enableSaveButton(b, !favoritesFull(st));
    }
    syncFavorites(st);
}

bool favoritesFull(PaletteState* st) { return st->favorites.size() >= PALETTE_MAX_FAVORITES; }

void registerSaveButton(PaletteState* st, GtkWidget* button) {
    st->saveButtons.push_back(button);
    enableSaveButton(button, !favoritesFull(st));
}

void saveFavorite(PaletteState* st, ToolType type) {
    GActionGroup* g = winActions(st->box);
    if (!g || favoritesFull(st)) {
        return;
    }
    PaletteFavorite fav = currentFavorite(g);
    if (fav.tool.empty() || fav.tool != (type == TOOL_PEN ? "pencil" : "highlighter")) {
        return;
    }
    if (std::find(st->favorites.begin(), st->favorites.end(), fav) != st->favorites.end()) {
        return;  // the same one again does not repeat
    }
    st->favorites.push_back(fav);
    saveFavorites(st);
    rebuildFavorites(st);
}

void restoreDefaults(PaletteState* st) {
    st->tools = defaultPaletteTools();
    st->colors = defaultPaletteColors();
    st->favorites.clear();
    st->radialSlots = defaultRadialSlots();
    saveTools(st);
    saveColors(st);
    saveFavorites(st);
    saveRadial(st);
    rebuildTools(st);
    rebuildColors(st);
    rebuildFavorites(st);
    updatePanel(st);
    updateRadialPanel(st);
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

namespace {
std::vector<std::string> splitFields(const std::string& text, char sep) {
    std::vector<std::string> out;
    size_t from = 0;
    while (from <= text.size()) {
        size_t to = text.find(sep, from);
        if (to == std::string::npos) {
            to = text.size();
        }
        std::string item = text.substr(from, to - from);
        size_t a = item.find_first_not_of(" \t\r\n");
        size_t b = item.find_last_not_of(" \t\r\n");
        out.push_back(a == std::string::npos ? "" : item.substr(a, b - a + 1));
        from = to + 1;
    }
    return out;
}
}  // namespace

std::vector<PaletteFavorite> parsePaletteFavorites(const std::string& text) {
    std::vector<PaletteFavorite> favorites;
    for (auto const& entry: splitFields(text, ';')) {
        auto fields = splitFields(entry, ':');
        if (fields.size() != 3 || (fields[0] != "pencil" && fields[0] != "highlighter")) {
            continue;
        }
        std::string hex = !fields[1].empty() && fields[1][0] == '#' ? fields[1].substr(1) : fields[1];
        if (hex.size() != 6 || !std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c); })) {
            continue;
        }
        if (fields[2].size() != 1 || fields[2][0] < '0' || fields[2][0] > '0' + TOOL_SIZE_VERY_THICK) {
            continue;
        }
        PaletteFavorite fav{fields[0], static_cast<uint32_t>(std::stoul(hex, nullptr, 16)), fields[2][0] - '0'};
        if (favorites.size() < PALETTE_MAX_FAVORITES &&
            std::find(favorites.begin(), favorites.end(), fav) == favorites.end()) {
            favorites.push_back(std::move(fav));
        }
    }
    return favorites;
}

std::string serializePaletteFavorites(const std::vector<PaletteFavorite>& favorites) {
    std::string out;
    for (auto const& f: favorites) {
        char buf[16];
        g_snprintf(buf, sizeof(buf), ":%06x:%d", f.color & 0xffffffU, f.size);
        if (!out.empty()) {
            out += ';';
        }
        out += f.tool + buf;
    }
    return out;
}

const std::vector<RadialOption>& radialOptions() {
    static const std::vector<RadialOption> options = {
            {"pencil", RadialKind::TOOL, TOOL_PEN, nullptr, "xopp-tool-pencil", N_("Pen")},
            {"highlighter", RadialKind::TOOL, TOOL_HIGHLIGHTER, nullptr, "xopp-tool-highlighter", N_("Marker")},
            {"eraser", RadialKind::TOOL, TOOL_ERASER, nullptr, "xopp-tool-eraser", N_("Eraser")},
            {"lasso", RadialKind::TOOL, TOOL_SELECT_REGION, nullptr, "xopp-select-lasso", N_("Lasso")},
            {"text", RadialKind::TOOL, TOOL_TEXT, nullptr, "xopp-tool-text", N_("Text")},
            {"latex", RadialKind::TOOL, TOOL_LATEX, nullptr, "xopp-tool-math-tex", N_("LaTeX")},
            {"hand", RadialKind::TOOL, TOOL_HAND, nullptr, "xopp-hand", N_("Hand")},
            {"laser", RadialKind::TOOL, TOOL_LASER_POINTER_PEN, nullptr, "xopp-laser-pointer", N_("Laser pointer")},
            {"shapes", RadialKind::SHAPES, TOOL_PEN, nullptr, "xopp-shape-recognizer", N_("Shape recognizer")},
            {"image", RadialKind::TOOL, TOOL_IMAGE, nullptr, "xopp-tool-image", N_("Image")},
            {"vspace", RadialKind::TOOL, TOOL_VERTICAL_SPACE, nullptr, "xopp-vertical-space", N_("Vertical space")},
            {"undo", RadialKind::ACTION, TOOL_NONE, "undo", "xopp-edit-undo", N_("Undo")},
            {"redo", RadialKind::ACTION, TOOL_NONE, "redo", "xopp-edit-redo", N_("Redo")},
            {"convert", RadialKind::ACTION, TOOL_NONE, "convert-to-text", "xopp-convert-text", N_("Convert to text")},
            {"send", RadialKind::ACTION, TOOL_NONE, "send-page-to-summa", "xopp-send", N_("Send to Summa")},
            {"copy", RadialKind::ACTION, TOOL_NONE, "copy-page-as-image", "xopp-edit-copy", N_("Copy the page")},
    };
    return options;
}

const RadialOption* findRadialOption(const std::string& id) {
    for (auto const& o: radialOptions()) {
        if (id == o.id) {
            return &o;
        }
    }
    return nullptr;
}

std::vector<std::string> defaultRadialSlots() {
    return {"pencil", "highlighter", "eraser", "lasso", "undo", "convert", "send", "hand"};
}

std::vector<std::string> parseRadialSlots(const std::string& text) {
    std::vector<std::string> slots = splitFields(text, ',');
    std::vector<std::string> defaults = defaultRadialSlots();
    if (slots.size() != RADIAL_SLOTS) {
        return defaults;
    }
    for (size_t i = 0; i < slots.size(); i++) {
        if (!findRadialOption(slots[i])) {
            slots[i] = defaults[i];
        }
    }
    return slots;
}

std::string serializeRadialSlots(const std::vector<std::string>& slots) { return joinList(slots); }

bool paletteColorIsLight(uint32_t rgb) {
    double luminance = 0.299 * ((rgb >> 16) & 0xff) + 0.587 * ((rgb >> 8) & 0xff) + 0.114 * (rgb & 0xff);
    return luminance / 255.0 > 0.6;
}

bool callPaletteFavorite(Control* control, size_t index) {
    Settings* settings = control->getSettings();
    if (settings->getEditorMode() != static_cast<int>(EditorMode::STUDIO)) {
        return false;
    }
    auto favorites = parsePaletteFavorites(settings->getAmbitusPaletteFavorites());
    if (favorites.empty()) {
        return false;
    }
    if (index < favorites.size()) {
        applyFavorite(gtk_widget_get_action_group(control->getWindow()->getWindow(), "win"), favorites[index]);
    }
    return true;
}

std::string paletteColorHex(uint32_t rgb) {
    char buf[8];
    g_snprintf(buf, sizeof(buf), "#%06X", rgb & 0xffffffU);
    return buf;
}

std::string paletteColorName(uint32_t rgb) { return colorName(rgb); }

StudioPalette::StudioPalette(Control* control, GtkOverlay* overlay) {
    state = new PaletteState;
    state->control = control;
    state->tools = parsePaletteTools(control->getSettings()->getAmbitusPaletteTools());
    state->colors = parsePaletteColors(control->getSettings()->getAmbitusPaletteColors());
    state->favorites = parsePaletteFavorites(control->getSettings()->getAmbitusPaletteFavorites());
    state->radialSlots = parseRadialSlots(control->getSettings()->getAmbitusRadialSlots());

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
    g_object_set_data(G_OBJECT(box), "amb-state", state);

    // the favourites first; with none, neither they nor their separator show
    state->favsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_start(GTK_BOX(box), state->favsBox, false, false, 0);
    state->favSep = separator();
    gtk_widget_set_no_show_all(state->favsBox, true);  // shown by rebuildFavorites, only with favourites
    gtk_widget_set_no_show_all(state->favSep, true);
    gtk_box_pack_start(GTK_BOX(box), state->favSep, false, false, 0);
    g_signal_connect(box, "hierarchy-changed", G_CALLBACK(+[](GtkWidget* w, GtkWidget*, gpointer) {
                         followFavorites(w);
                         if (auto* st = static_cast<PaletteState*>(g_object_get_data(G_OBJECT(w), "amb-state"))) {
                             syncFavorites(st);
                         }
                     }),
                     nullptr);

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
    rebuildFavorites(state);
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
