#include "BoardNavigator.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "ambitus/Board.h"
#include "control/Control.h"
#include "control/zoom/ZoomControl.h"
#include "gui/PageView.h"
#include "gui/XournalView.h"
#include "gui/scroll/ScrollHandling.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "util/Util.h"
#include "util/i18n.h"

#include "AmbitusTip.h"

namespace ambitus {

namespace {
void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

GdkRGBA lookup(GtkWidget* w, const char* name, const char* fallback) {
    GdkRGBA c;
    if (!gtk_style_context_lookup_color(gtk_widget_get_style_context(w), name, &c)) {
        gdk_rgba_parse(&c, fallback);
    }
    return c;
}

void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min({r, w / 2, h / 2});
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

GtkWidget* pillButton(const char* icon, const char* tooltip) {
    GtkWidget* b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
    setOverlayTipText(b, tooltip);
    gtk_widget_set_can_focus(b, false);
    return b;
}
}  // namespace

BoardNavigator::BoardNavigator(Control* control, XournalView* xournal, GtkOverlay* overlay):
        control(control), xournal(xournal) {
    map = gtk_drawing_area_new();
    addClass(map, "amb-minimap");
    gtk_widget_set_size_request(map, MAP_WIDTH, MAP_HEIGHT);
    gtk_widget_set_halign(map, GTK_ALIGN_END);
    gtk_widget_set_valign(map, GTK_ALIGN_END);
    gtk_widget_set_margin_end(map, 16);
    gtk_widget_set_margin_bottom(map, 64);
    gtk_widget_add_events(map, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect(map, "draw", G_CALLBACK(onDrawMap), this);
    g_signal_connect(map, "button-press-event", G_CALLBACK(onMapButton), this);
    g_signal_connect(map, "button-release-event", G_CALLBACK(onMapButton), this);
    g_signal_connect(map, "motion-notify-event", G_CALLBACK(onMapMotion), this);
    gtk_widget_set_no_show_all(map, true);
    gtk_overlay_add_overlay(overlay, map);

    pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(pill, "amb-board-pill");
    gtk_widget_set_halign(pill, GTK_ALIGN_END);
    gtk_widget_set_valign(pill, GTK_ALIGN_END);
    gtk_widget_set_margin_end(pill, 16);
    gtk_widget_set_margin_bottom(pill, 16);
    GtkWidget* out = pillButton("list-remove-symbolic", _("Zoom out"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(out), "win.zoom-out");
    percent = gtk_label_new("100%");
    addClass(percent, "amb-board-percent");
    GtkWidget* in = pillButton("list-add-symbolic", _("Zoom in"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(in), "win.zoom-in");
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    GtkWidget* content = gtk_button_new_with_label(_("Go to the content"));
    gtk_widget_set_can_focus(content, false);
    addClass(content, "amb-board-content");
    g_signal_connect_swapped(content, "clicked", G_CALLBACK(+[](BoardNavigator* self) { self->goToContent(); }), this);
    for (GtkWidget* w: {out, percent, in, sep, content}) {
        gtk_box_pack_start(GTK_BOX(pill), w, false, false, 0);
    }
    gtk_widget_show_all(pill);
    gtk_widget_set_no_show_all(pill, true);
    gtk_widget_hide(pill);
    gtk_overlay_add_overlay(overlay, pill);

    auto* scroll = xournal->getScrollHandling();
    GtkAdjustment* adj[2] = {scroll->getHorizontal(), scroll->getVertical()};
    for (int i = 0; i < 2; i++) {
        scrollIds[2 * i] = g_signal_connect_swapped(adj[i], "value-changed", G_CALLBACK(gtk_widget_queue_draw), map);
        scrollIds[2 * i + 1] = g_signal_connect_swapped(adj[i], "changed", G_CALLBACK(gtk_widget_queue_draw), map);
    }
    control->getZoomControl()->addZoomListener(this);
}

BoardNavigator::~BoardNavigator() {
    control->getZoomControl()->removeZoomListener(this);
    auto* scroll = xournal->getScrollHandling();
    GtkAdjustment* adj[2] = {scroll->getHorizontal(), scroll->getVertical()};
    for (int i = 0; i < 2; i++) {
        g_signal_handler_disconnect(adj[i], scrollIds[2 * i]);
        g_signal_handler_disconnect(adj[i], scrollIds[2 * i + 1]);
    }
}

void BoardNavigator::setVisible(bool v) {
    visible = v;
    gtk_widget_set_visible(map, v);
    gtk_widget_set_visible(pill, v);
    if (v) {
        updateContent();
        zoomChanged();
    }
}

void BoardNavigator::contentChanged() {
    if (visible) {
        updateContent();
    }
}

void BoardNavigator::zoomChanged() {
    std::string t = std::to_string(static_cast<int>(std::lround(control->getZoomControl()->getZoomReal() * 100))) + "%";
    gtk_label_set_text(GTK_LABEL(percent), t.c_str());
    gtk_widget_queue_draw(map);
}

void BoardNavigator::updateContent() {
    blocks.clear();
    Document* doc = control->getDocument();
    doc->lock_shared();
    if (doc->getPageCount() > 0) {
        PageRef page = doc->getPage(0);
        pageWidth = page->getWidth();
        pageHeight = page->getHeight();
        for (const Layer* layer: page->getLayersView()) {
            for (const Element* e: layer->getElementsView()) {
                blocks.emplace_back(e->getBoundingBox());
            }
        }
    }
    doc->unlock_shared();
    gtk_widget_queue_draw(map);
}

auto BoardNavigator::fit() const -> MapFit {
    if (pageWidth <= 0 || pageHeight <= 0) {
        return {1, 0, 0};
    }
    double s = std::min(MAP_WIDTH / pageWidth, MAP_HEIGHT / pageHeight);
    return {s, (MAP_WIDTH - pageWidth * s) / 2, (MAP_HEIGHT - pageHeight * s) / 2};
}

Range BoardNavigator::shownRange() const {
    XojPageView* view = xournal->getViewFor(0);
    if (!view) {
        return Range();
    }
    auto* scroll = xournal->getScrollHandling();
    GtkAdjustment* h = scroll->getHorizontal();
    GtkAdjustment* v = scroll->getVertical();
    double zoom = xournal->getZoom();
    auto pos = view->getPixelPosition();
    double x = (gtk_adjustment_get_value(h) - pos.x) / zoom;
    double y = (gtk_adjustment_get_value(v) - pos.y) / zoom;
    return Range(x, y, x + gtk_adjustment_get_page_size(h) / zoom, y + gtk_adjustment_get_page_size(v) / zoom);
}

void BoardNavigator::centreOn(double x, double y) {
    XojPageView* view = xournal->getViewFor(0);
    if (!view) {
        return;
    }
    auto* scroll = xournal->getScrollHandling();
    GtkAdjustment* h = scroll->getHorizontal();
    GtkAdjustment* v = scroll->getVertical();
    double zoom = xournal->getZoom();
    auto pos = view->getPixelPosition();
    gtk_adjustment_set_value(h, pos.x + x * zoom - gtk_adjustment_get_page_size(h) / 2);
    gtk_adjustment_set_value(v, pos.y + y * zoom - gtk_adjustment_get_page_size(v) / 2);
}

void BoardNavigator::goToContent() {
    Document* doc = control->getDocument();
    doc->lock_shared();
    Range content = doc->getPageCount() > 0 ? board::contentBounds(*doc->getPage(0)) : Range();
    doc->unlock_shared();
    if (!content.isValid()) {
        centreOn(0, 0);
        return;
    }
    auto* scroll = xournal->getScrollHandling();
    ZoomControl* zoom = control->getZoomControl();
    double z = board::zoomToFit(content, gtk_adjustment_get_page_size(scroll->getHorizontal()),
                                gtk_adjustment_get_page_size(scroll->getVertical()), zoom->getZoom100Value());
    zoom->setZoomFitMode(false);
    zoom->setZoom(z);
    // the layout follows the new zoom in the next main loop iteration
    double cx = (content.minX + content.maxX) / 2, cy = (content.minY + content.maxY) / 2;
    Util::execInUiThread([this, cx, cy]() { centreOn(cx, cy); });
}

gboolean BoardNavigator::onDrawMap(GtkWidget* w, cairo_t* cr, BoardNavigator* self) {
    bool dark = gtk_style_context_has_class(gtk_widget_get_style_context(gtk_widget_get_toplevel(w)), "darkMode");
    GdkRGBA bg = lookup(w, dark ? "amb_dk_layer" : "amb_bg", "#ffffff");
    GdkRGBA layer = lookup(w, dark ? "amb_dk_layer2" : "amb_layer", "#f4f4f4");
    GdkRGBA border = lookup(w, dark ? "amb_dk_border" : "amb_border", "#e0e0e0");
    GdkRGBA text = lookup(w, dark ? "amb_dk_text" : "amb_text", "#161616");
    GdkRGBA accent = lookup(w, dark ? "amb_dk_accent" : "amb_accent", "#303030");

    // card
    roundedRect(cr, 0.5, 0.5, MAP_WIDTH - 1, MAP_HEIGHT - 1, 8);
    gdk_cairo_set_source_rgba(cr, &bg);
    cairo_fill_preserve(cr);
    gdk_cairo_set_source_rgba(cr, &border);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    roundedRect(cr, 0, 0, MAP_WIDTH, MAP_HEIGHT, 8);
    cairo_clip(cr);

    MapFit f = self->fit();
    // the page
    gdk_cairo_set_source_rgba(cr, &layer);
    cairo_rectangle(cr, f.x0, f.y0, self->pageWidth * f.scale, self->pageHeight * f.scale);
    cairo_fill(cr);

    // the content, as blocks
    cairo_set_source_rgba(cr, text.red, text.green, text.blue, 0.22);
    for (const Range& b: self->blocks) {
        double w = std::max(b.getWidth() * f.scale, 1.5), h = std::max(b.getHeight() * f.scale, 1.5);
        roundedRect(cr, f.x0 + b.minX * f.scale, f.y0 + b.minY * f.scale, w, h, 1.5);
        cairo_fill(cr);
    }

    // the part on screen
    Range s = self->shownRange();
    if (s.isValid()) {
        gdk_cairo_set_source_rgba(cr, &accent);
        cairo_set_line_width(cr, 1.5);
        roundedRect(cr, f.x0 + s.minX * f.scale, f.y0 + s.minY * f.scale, s.getWidth() * f.scale,
                    s.getHeight() * f.scale, 2);
        cairo_stroke(cr);
    }
    return true;
}

gboolean BoardNavigator::onMapButton(GtkWidget*, GdkEventButton* e, BoardNavigator* self) {
    if (e->button != 1) {
        return false;
    }
    self->dragging = e->type == GDK_BUTTON_PRESS;
    if (self->dragging) {
        MapFit f = self->fit();
        self->centreOn((e->x - f.x0) / f.scale, (e->y - f.y0) / f.scale);
    }
    return true;
}

gboolean BoardNavigator::onMapMotion(GtkWidget*, GdkEventMotion* e, BoardNavigator* self) {
    if (self->dragging) {
        MapFit f = self->fit();
        self->centreOn((e->x - f.x0) / f.scale, (e->y - f.y0) / f.scale);
    }
    return true;
}

}  // namespace ambitus
