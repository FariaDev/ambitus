#include "SelectionBar.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "control/Control.h"
#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "gui/scroll/ScrollHandling.h"
#include "model/Element.h"
#include "model/Stroke.h"
#include "util/Color.h"
#include "util/GVariantTemplate.h"
#include "util/ShortcutModifier.h"
#include "util/i18n.h"

#include "AmbitusTip.h"
#include "StudioPalette.h"
#include "TitleBar.h"

namespace ambitus {

namespace {
/// The bar has a shadow, which the revealer would cut off at its edge: the box keeps this margin for it
constexpr int SHADOW = 8;
constexpr int ICON_PX = 18;

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

GtkWidget* separator() {
    GtkWidget* s = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    addClass(s, "amb-selbar-sep");
    return s;
}

GtkWidget* icon(const char* name) {
    GtkWidget* img = gtk_image_new_from_icon_name(name, GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(img), ICON_PX);
    return img;
}

/// A flat button bound to an action of the window; the buttons never take the keyboard focus from the page
GtkWidget* actionButton(const char* action, GVariant* target) {
    GtkWidget* b = gtk_button_new();
    gtk_widget_set_can_focus(b, false);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    // the target first: an action with a parameter warns when it is bound to a button that has none yet
    if (target) {
        gtk_actionable_set_action_target_value(GTK_ACTIONABLE(b), target);
    }
    gtk_actionable_set_action_name(GTK_ACTIONABLE(b), action);
    return b;
}

/// The tip of a button of the bar: the name and, dimmed, its shortcut. The tips of GTK do not show over the pages
void tip(GtkWidget* b, const char* name, const char* key) {
    setOverlayTip(b, [n = std::string(name), k = std::string(key)]() {
        gchar* name = g_markup_escape_text(n.c_str(), -1);
        std::string markup = name;
        g_free(name);
        if (!k.empty()) {
            markup += "  <span alpha=\"70%\">" + k + "</span>";
        }
        return markup;
    });
}

/// The colour of the dot on its button: a thin ring in the text colour keeps the dark inks visible on the dark theme
gboolean drawDot(GtkWidget* w, cairo_t* cr, gpointer) {
    GtkWidget* button = gtk_widget_get_parent(w);
    auto rgb = static_cast<uint32_t>(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "amb-rgb")));
    double cx = gtk_widget_get_allocated_width(w) / 2.0;
    double cy = gtk_widget_get_allocated_height(w) / 2.0;
    cairo_arc(cr, cx, cy, 6, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, ((rgb >> 16) & 0xff) / 255.0, ((rgb >> 8) & 0xff) / 255.0, (rgb & 0xff) / 255.0);
    cairo_fill_preserve(cr);
    GdkRGBA fg;
    gtk_style_context_get_color(gtk_widget_get_style_context(button), gtk_widget_get_state_flags(button), &fg);
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.55);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    return true;
}

bool intersects(const GdkRectangle& a, const GdkRectangle& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

bool sameRect(const GdkRectangle& a, const GdkRectangle& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool animationsOn(GtkWidget* w) {
    gboolean on = true;
    g_object_get(gtk_widget_get_settings(w), "gtk-enable-animations", &on, nullptr);
    return on;
}
}  // namespace

SelectionBar::SelectionBar(Control* control, XournalView* xournal, GtkOverlay* overlay):
        control(control), xournal(xournal), overlay(overlay) {
    // the fade: a crossfade, as GTK3 does not move a widget by CSS
    root = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(root), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_halign(root, GTK_ALIGN_START);
    gtk_widget_set_valign(root, GTK_ALIGN_START);

    bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(bar, "amb-selbar");
    gtk_widget_set_margin_start(bar, SHADOW);
    gtk_widget_set_margin_end(bar, SHADOW);
    gtk_widget_set_margin_top(bar, SHADOW);
    gtk_widget_set_margin_bottom(bar, SHADOW);
    gtk_container_add(GTK_CONTAINER(root), bar);

    // convert to text: only when the selection has handwriting
    convert = actionButton("win.convert-to-text", nullptr);
    addClass(convert, "amb-selbar-text");
    GtkWidget* label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(label), icon("xopp-convert-text"), false, false, 0);
    gtk_box_pack_start(GTK_BOX(label), gtk_label_new(_("Convert to text")), false, false, 0);
    gtk_container_add(GTK_CONTAINER(convert), label);
    gtk_box_pack_start(GTK_BOX(bar), convert, false, false, 0);
    convertSep = separator();
    gtk_box_pack_start(GTK_BOX(bar), convertSep, false, false, 0);

    // copy: the icon turns into a check for a moment
    GtkWidget* copy = actionButton("win.copy", nullptr);
    copyIcon = icon("xopp-edit-copy");
    gtk_container_add(GTK_CONTAINER(copy), copyIcon);
    tip(copy, _("Copy"), shortcutLabel("<Primary>c").c_str());
    g_signal_connect_swapped(copy, "clicked", G_CALLBACK(+[](SelectionBar* self) { self->copied(); }), this);
    gtk_box_pack_start(GTK_BOX(bar), copy, false, false, 0);

    GtkWidget* del = actionButton("win.delete", nullptr);
    gtk_container_add(GTK_CONTAINER(del), icon("xopp-trash"));
    tip(del, C_("Selection bar", "Delete"), "Del");
    gtk_box_pack_start(GTK_BOX(bar), del, false, false, 0);

    gtk_box_pack_start(GTK_BOX(bar), separator(), false, false, 0);

    // the first colours of the palette; each one recolours what is selected (win.tool-color does it)
    for (auto& d: dots) {
        d = actionButton("win.tool-color", makeGVariant(Color(0x16, 0x16, 0x16)));  // the colour comes in loadContents
        addClass(d, "amb-selbar-dot");
        GtkWidget* area = gtk_drawing_area_new();
        gtk_widget_set_size_request(area, 14, 14);
        g_signal_connect(area, "draw", G_CALLBACK(drawDot), nullptr);
        gtk_container_add(GTK_CONTAINER(d), area);
        GtkWidget* b = d;
        setOverlayTip(b, [b]() {
            auto rgb = static_cast<uint32_t>(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(b), "amb-rgb")));
            gchar* name = g_markup_escape_text(paletteColorName(rgb).c_str(), -1);
            std::string markup = name;
            g_free(name);
            return markup;
        });
        gtk_box_pack_start(GTK_BOX(bar), d, false, false, 0);
    }

    gtk_box_pack_start(GTK_BOX(bar), separator(), false, false, 0);

    more = gtk_button_new();
    gtk_widget_set_can_focus(more, false);
    gtk_widget_set_valign(more, GTK_ALIGN_CENTER);
    addClass(more, "amb-selbar-more");
    gtk_container_add(GTK_CONTAINER(more), icon("xopp-ellipsis"));
    tip(more, C_("Selection bar", "More"), "");
    gtk_box_pack_start(GTK_BOX(bar), more, false, false, 0);
    menu = buildMenu();
    g_signal_connect_swapped(more, "clicked", G_CALLBACK(+[](SelectionBar* self) {
                                 gtk_popover_set_position(GTK_POPOVER(self->menu),
                                                          self->below ? GTK_POS_BOTTOM : GTK_POS_TOP);
                                 gtk_popover_popup(GTK_POPOVER(self->menu));
                             }),
                             this);

    g_signal_connect_swapped(root, "notify::child-revealed", G_CALLBACK(+[](SelectionBar* self) {
                                 auto* r = GTK_REVEALER(self->root);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     gtk_widget_hide(self->root);
                                 }
                             }),
                             this);

    gtk_widget_show_all(root);
    gtk_widget_set_no_show_all(root, true);
    gtk_widget_hide(root);
    gtk_overlay_add_overlay(overlay, root);
    positionId = g_signal_connect(overlay, "get-child-position", G_CALLBACK(onChildPosition), this);
}

SelectionBar::~SelectionBar() {
    if (tickId) {
        gtk_widget_remove_tick_callback(GTK_WIDGET(overlay), tickId);
    }
    if (copiedTimer) {
        g_source_remove(copiedTimer);
    }
    g_signal_handler_disconnect(overlay, positionId);
    gtk_container_remove(GTK_CONTAINER(overlay), root);
}

GtkWidget* SelectionBar::buildMenu() {
    GtkWidget* pop = gtk_popover_new(more);
    addClass(pop, "amb-selpop");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(pop), box);

    auto item = [&](const char* iconName, const char* text, const char* action, GVariant* target) {
        GtkWidget* b = actionButton(action, target);
        addClass(b, "amb-selpop-item");
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(row), icon(iconName), false, false, 0);
        GtkWidget* l = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
        gtk_container_add(GTK_CONTAINER(b), row);
        g_signal_connect_swapped(b, "clicked", G_CALLBACK(gtk_popover_popdown), pop);
        gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    };
    item("xopp-send", _("Send to Summa"), "win.send-page-to-summa", nullptr);
    // no "copy as image" here: Xournal++ only copies the whole page, and doing it ends the selection
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    addClass(sep, "amb-selpop-sep");
    gtk_box_pack_start(GTK_BOX(box), sep, false, false, 0);
    // arrange-selection-order takes the EditSelection::OrderChange as a uint64: 0 is to the front, 3 is to the back
    item("xopp-bring-to-front", C_("Selection bar", "Bring to front"), "win.arrange-selection-order",
         g_variant_new_uint64(0));
    item("xopp-send-to-back", C_("Selection bar", "Send to back"), "win.arrange-selection-order",
         g_variant_new_uint64(3));
    gtk_widget_show_all(box);
    return pop;
}

bool SelectionBar::enabled() const {
    // a board is always in the Studio layout, whatever the setting says
    if (control->isBoardOpen()) {
        return true;
    }
    return control->getSettings()->getEditorMode() != static_cast<int>(EditorMode::CLASSIC);
}

bool SelectionBar::selectionRect(GdkRectangle& sel, GdkRectangle& visible) const {
    EditSelection* s = xournal->getSelection();
    GtkWidget* pages = xournal->getWidget();
    int ox = 0, oy = 0;
    if (!s || !gtk_widget_translate_coordinates(pages, GTK_WIDGET(overlay), 0, 0, &ox, &oy)) {
        return false;
    }
    // the box is in the coordinates of the layout of the pages: the scroll takes it to the widget
    auto box = s->getBoundingBoxInView();
    auto scroll = xournal->getScrollHandling()->getPosition();
    int x0 = static_cast<int>(std::floor(box.x - scroll.x)) + ox;
    int y0 = static_cast<int>(std::floor(box.y - scroll.y)) + oy;
    int x1 = static_cast<int>(std::ceil(box.x + box.width - scroll.x)) + ox;
    int y1 = static_cast<int>(std::ceil(box.y + box.height - scroll.y)) + oy;
    sel = {x0, y0, x1 - x0, y1 - y0};
    visible = {ox, oy, gtk_widget_get_allocated_width(pages), gtk_widget_get_allocated_height(pages)};
    return true;
}

void SelectionBar::loadContents() {
    EditSelection* s = xournal->getSelection();
    bool ink = false;
    if (s) {
        for (const Element* e: s->getElementsView()) {
            if (e->getType() == ELEMENT_STROKE && static_cast<const Stroke*>(e)->getToolType() == StrokeTool::PEN) {
                ink = true;
                break;
            }
        }
    }
    gtk_widget_set_visible(convert, ink);
    gtk_widget_set_visible(convertSep, ink);

    auto colors = parsePaletteColors(control->getSettings()->getAmbitusPaletteColors());
    for (size_t i = 0; i < DOTS; i++) {
        bool has = i < colors.size();
        gtk_widget_set_visible(dots[i], has);
        if (has) {
            uint32_t c = colors[i];
            g_object_set_data(G_OBJECT(dots[i]), "amb-rgb", GUINT_TO_POINTER(c));
            Color color(static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c));
            gtk_actionable_set_action_target_value(GTK_ACTIONABLE(dots[i]), makeGVariant(color));
            gtk_widget_queue_draw(dots[i]);
        }
    }
}

void SelectionBar::setShown(bool show) {
    if (show == shown) {
        return;
    }
    shown = show;
    gtk_revealer_set_transition_duration(GTK_REVEALER(root), animationsOn(root) ? (show ? FADE_IN : FADE_OUT) : 0);
    if (show) {
        gtk_widget_show(root);
        gtk_widget_queue_resize(root);
    } else {
        gtk_popover_popdown(GTK_POPOVER(menu));
    }
    gtk_revealer_set_reveal_child(GTK_REVEALER(root), show);
}

bool SelectionBar::update() {
    EditSelection* s = xournal->getSelection();
    if (!s || !enabled()) {
        setShown(false);
        return false;
    }
    GdkRectangle rect, visible;
    if (!selectionRect(rect, visible)) {
        setShown(false);
        return true;
    }
    // gone with the drag and the resize, back on release; and gone while the selection is out of sight
    setShown(!s->isMoving() && intersects(rect, visible));
    if (shown && !sameRect(rect, last)) {
        gtk_widget_queue_resize(root);
    }
    last = rect;
    return true;
}

void SelectionBar::selectionChanged() {
    if (xournal->getSelection() && enabled()) {
        loadContents();
    }
    bool keep = update();
    if (keep && !tickId) {
        tickId = gtk_widget_add_tick_callback(GTK_WIDGET(overlay), onTick, this, nullptr);
    } else if (!keep && tickId) {
        gtk_widget_remove_tick_callback(GTK_WIDGET(overlay), tickId);
        tickId = 0;
    }
}

gboolean SelectionBar::onTick(GtkWidget*, GdkFrameClock*, gpointer data) {
    auto* self = static_cast<SelectionBar*>(data);
    if (self->update()) {
        return G_SOURCE_CONTINUE;
    }
    self->tickId = 0;
    return G_SOURCE_REMOVE;
}

bool SelectionBar::place(GdkRectangle* alloc) {
    GdkRectangle rect, visible;
    int w = 0, h = 0;
    gtk_widget_get_preferred_width(bar, nullptr, &w);
    gtk_widget_get_preferred_height(bar, nullptr, &h);
    // w and h include the margin for the shadow; the bar itself is what must fit
    int bw = w - 2 * SHADOW, bh = h - 2 * SHADOW;
    if (selectionRect(rect, visible)) {
        int x = rect.x + rect.width / 2 - bw / 2;
        int y = rect.y - GAP - bh;
        below = false;
        if (y < visible.y + EDGE) {  // no room above
            y = rect.y + rect.height + GAP;
            below = true;
        }
        // always inside the visible area
        int xMax = visible.x + visible.width - bw - EDGE;
        int yMax = visible.y + visible.height - bh - EDGE;
        x = std::max(visible.x + EDGE, std::min(x, xMax));
        y = std::max(visible.y + EDGE, std::min(y, yMax));
        placed = {x - SHADOW, y - SHADOW, w, h};
    }
    *alloc = placed;
    alloc->width = w;
    alloc->height = h;
    return true;
}

gboolean SelectionBar::onChildPosition(GtkOverlay*, GtkWidget* w, GdkRectangle* alloc, SelectionBar* self) {
    if (w != self->root) {
        return false;
    }
    return self->place(alloc);
}

void SelectionBar::copied() {
    gtk_image_set_from_icon_name(GTK_IMAGE(copyIcon), "xopp-check", GTK_ICON_SIZE_BUTTON);
    if (copiedTimer) {
        g_source_remove(copiedTimer);
    }
    copiedTimer = g_timeout_add(
            COPIED_MS,
            +[](gpointer p) -> gboolean {
                auto* self = static_cast<SelectionBar*>(p);
                self->copiedTimer = 0;
                gtk_image_set_from_icon_name(GTK_IMAGE(self->copyIcon), "xopp-edit-copy", GTK_ICON_SIZE_BUTTON);
                return G_SOURCE_REMOVE;
            },
            this);
}

}  // namespace ambitus
