#include "PageGrowHandle.h"

#include <algorithm>
#include <cmath>

#include "control/Control.h"
#include "gui/MainWindow.h"
#include "gui/PageView.h"
#include "gui/XournalView.h"
#include "gui/scroll/ScrollHandling.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "util/i18n.h"

namespace ambitus {

namespace {
constexpr int EDGE_INSIDE = 28;  ///< px inside the page where the handle already shows
constexpr int PILL_GAP = 8;      ///< px between the strip and the pill
constexpr int ZONE_AFTER = 120;  ///< px after the strip where the handle stays

XojPageView* findView(XournalView* xournal, const PageRef& page) {
    if (!page) {
        return nullptr;
    }
    for (auto const& v: xournal->getViewPages()) {
        if (v->getPage() == page) {
            return v.get();
        }
    }
    return nullptr;
}
}  // namespace

PageGrowHandle::PageGrowHandle(Control* control, XournalView* xournal, GtkOverlay* overlay):
        control(control), xournal(xournal), overlay(overlay) {
    strip.reset(gtk_drawing_area_new(), xoj::util::adopt);
    gtk_style_context_add_class(gtk_widget_get_style_context(strip.get()), "amb-grow-strip");
    gtk_widget_set_no_show_all(strip.get(), true);
    g_signal_connect(strip.get(), "draw", G_CALLBACK(onDrawStrip), this);

    pill.reset(gtk_button_new(), xoj::util::adopt);
    gtk_style_context_add_class(gtk_widget_get_style_context(pill.get()), "amb-grow-pill");
    gtk_widget_set_can_focus(pill.get(), false);
    gtk_widget_set_tooltip_text(pill.get(), _("Grow the page by 5 cm"));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    pillIcon = gtk_image_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(box), pillIcon, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("+5 cm"), false, false, 0);
    gtk_container_add(GTK_CONTAINER(pill.get()), box);
    gtk_widget_show_all(box);
    gtk_widget_set_no_show_all(pill.get(), true);
    g_signal_connect_swapped(pill.get(), "clicked", G_CALLBACK(+[](PageGrowHandle* self) { self->grow(); }), this);

    newPagePill.reset(gtk_button_new_with_label(_("+ New page")), xoj::util::adopt);
    gtk_style_context_add_class(gtk_widget_get_style_context(newPagePill.get()), "amb-grow-pill");
    gtk_widget_set_can_focus(newPagePill.get(), false);
    gtk_widget_set_tooltip_text(newPagePill.get(), _("Add a page after the last one"));
    gtk_widget_set_no_show_all(newPagePill.get(), true);
    g_signal_connect_swapped(newPagePill.get(), "clicked",
                             G_CALLBACK(+[](PageGrowHandle* self) { self->control->getWindow()->appendPageAtEnd(); }),
                             this);

    gtk_overlay_add_overlay(overlay, strip.get());
    gtk_overlay_set_overlay_pass_through(overlay, strip.get(), true);
    gtk_overlay_add_overlay(overlay, pill.get());
    gtk_overlay_add_overlay(overlay, newPagePill.get());
    positionId = g_signal_connect(overlay, "get-child-position", G_CALLBACK(onChildPosition), this);

    // Xournal++ handles the input of the pages in an "event" handler that stops the emission,
    // so the pointer is watched with an emission hook, before any handler runs.
    hookId = g_signal_add_emission_hook(g_signal_lookup("event", GTK_TYPE_WIDGET), 0, onEvent, this, nullptr);

    auto* scroll = xournal->getScrollHandling();
    GtkAdjustment* adj[2] = {scroll->getHorizontal(), scroll->getVertical()};
    for (int i = 0; i < 2; i++) {
        scrollIds[i] = g_signal_connect_swapped(adj[i], "value-changed",
                                                G_CALLBACK(+[](PageGrowHandle* self) { self->place(); }), this);
    }
}

PageGrowHandle::~PageGrowHandle() {
    g_signal_remove_emission_hook(g_signal_lookup("event", GTK_TYPE_WIDGET), hookId);
    auto* scroll = xournal->getScrollHandling();
    g_signal_handler_disconnect(scroll->getHorizontal(), scrollIds[0]);
    g_signal_handler_disconnect(scroll->getVertical(), scrollIds[1]);
    g_signal_handler_disconnect(overlay, positionId);
    gtk_container_remove(GTK_CONTAINER(overlay), strip.get());
    gtk_container_remove(GTK_CONTAINER(overlay), pill.get());
    gtk_container_remove(GTK_CONTAINER(overlay), newPagePill.get());
}

int PageGrowHandle::visibleWidth() const {
    const int width = gtk_widget_get_allocated_width(xournal->getWidget());
    // Ambitus: the part on screen ends at the divider when a notebook is beside: nothing here goes over it
    const auto visible = static_cast<int>(gtk_adjustment_get_page_size(xournal->getScrollHandling()->getHorizontal()));
    return visible > 0 ? std::min(width, visible) : width;
}

void PageGrowHandle::hide() {
    edge = Edge::NONE;
    page.reset();
    gtk_widget_hide(strip.get());
    gtk_widget_hide(pill.get());
    gtk_widget_hide(newPagePill.get());
}

gboolean PageGrowHandle::onEvent(GSignalInvocationHint*, guint n, const GValue* params, gpointer data) {
    auto* self = static_cast<PageGrowHandle*>(data);
    if (n < 2 || g_value_get_object(&params[0]) != G_OBJECT(self->xournal->getWidget())) {
        return true;
    }
    auto* event = static_cast<GdkEvent*>(g_value_get_boxed(&params[1]));
    switch (event->type) {
        case GDK_MOTION_NOTIFY: {
            auto state = event->motion.state;
            bool pressed = state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK);
            self->pointerAt(event->motion.x, event->motion.y, pressed);
            break;
        }
        case GDK_BUTTON_PRESS:
        case GDK_TOUCH_BEGIN:
            self->hide();
            break;
        default:
            break;
    }
    return true;  // keep the hook
}

void PageGrowHandle::pointerAt(double x, double y, bool pressed) {
    if (pressed || control->isBoardOpen()) {  // a board grows by itself
        hide();
        return;
    }
    auto scroll = xournal->getScrollHandling()->getPosition();
    double lx = x + scroll.x;
    double ly = y + scroll.y;
    double g = Control::GROW_STEP * xournal->getZoom();

    Edge found = Edge::NONE;
    PageRef foundPage;
    double best = 1e9;
    for (auto const& v: xournal->getViewPages()) {
        auto p = v->getPixelPosition();
        double w = v->getDisplayWidthDouble();
        double h = v->getDisplayHeightDouble();
        double right = p.x + w;
        double bottom = p.y + h;
        if (ly >= p.y && ly <= bottom && lx >= right - EDGE_INSIDE && lx <= right + g + ZONE_AFTER) {
            double d = std::abs(lx - right);
            if (d < best) {
                best = d;
                found = Edge::RIGHT;
                foundPage = v->getPage();
            }
        }
        // Below the page there is usually the next one, so the bottom zone stays close to the edge
        if (lx >= p.x && lx <= right && ly >= bottom - EDGE_INSIDE && ly <= bottom + EDGE_INSIDE / 2) {
            double d = std::abs(ly - bottom);
            if (d < best) {
                best = d;
                found = Edge::BOTTOM;
                foundPage = v->getPage();
            }
        }
    }
    if (found == Edge::NONE || !foundPage || foundPage->getBackgroundType().isPdfPage()) {
        hide();
        return;
    }
    bool changed = found != edge || foundPage != page;
    edge = found;
    page = foundPage;
    pointerX = x;
    pointerY = y;
    if (changed) {
        gtk_image_set_from_icon_name(GTK_IMAGE(pillIcon), edge == Edge::RIGHT ? "go-next-symbolic" : "go-down-symbolic",
                                     GTK_ICON_SIZE_MENU);
    }
    place();
}

void PageGrowHandle::place() {
    if (edge == Edge::NONE) {
        return;
    }
    XojPageView* view = findView(xournal, page);
    if (!view) {
        hide();
        return;
    }
    auto scroll = xournal->getScrollHandling()->getPosition();
    auto p = view->getPixelPosition();
    double px = p.x - scroll.x;
    double py = p.y - scroll.y;
    double w = view->getDisplayWidthDouble();
    double h = view->getDisplayHeightDouble();
    double g = Control::GROW_STEP * xournal->getZoom();
    int vw = visibleWidth();
    int vh = gtk_widget_get_allocated_height(xournal->getWidget());

    GtkRequisition nat;
    gtk_widget_get_preferred_size(pill.get(), nullptr, &nat);

    double sx = 0, sy = 0, sw = 0, sh = 0, pillX = 0, pillY = 0;
    if (edge == Edge::RIGHT) {
        sx = px + w, sy = py, sw = g, sh = h;
        pillX = sx + sw + PILL_GAP;
        double top = std::max(py, 0.0) + PILL_GAP;
        double low = std::min(py + h, static_cast<double>(vh)) - nat.height - PILL_GAP;
        pillY = std::clamp(pointerY - nat.height / 2.0, top, std::max(top, low));
    } else {
        sx = px, sy = py + h, sw = w, sh = g;
        pillX = px + w + PILL_GAP;
        pillY = sy + (sh - nat.height) / 2.0;
    }

    // the strip never covers another page: it stops in the gap before it
    for (auto const& other: xournal->getViewPages()) {
        if (other.get() == view) {
            continue;
        }
        auto o = other->getPixelPosition();
        double ox = o.x - scroll.x, oy = o.y - scroll.y;
        double ow = other->getDisplayWidthDouble(), oh = other->getDisplayHeightDouble();
        if (edge == Edge::BOTTOM && oy >= py + h && ox < sx + sw && ox + ow > sx) {
            sh = std::min(sh, oy - sy - 2);
        } else if (edge == Edge::RIGHT && ox >= px + w && oy < sy + sh && oy + oh > sy) {
            sw = std::min(sw, ox - sx - 2);
        }
    }
    if (edge == Edge::BOTTOM) {
        pillY = sy + (std::max(sh, 0.0) - nat.height) / 2.0;
        // at the end of the notebook the strip is under the view: the pill stays in the view, not over the bottom bar
        pillY = std::clamp(pillY, static_cast<double>(PILL_GAP),
                           std::max(static_cast<double>(PILL_GAP), static_cast<double>(vh - nat.height - PILL_GAP)));
    }

    // the strip is clipped to the visible area
    double x0 = std::max(sx, 0.0), y0 = std::max(sy, 0.0);
    double x1 = std::min(sx + sw, static_cast<double>(vw)), y1 = std::min(sy + sh, static_cast<double>(vh));
    const bool stripVisible = x1 - x0 >= 1 && y1 - y0 >= 1;
    if (!stripVisible && edge == Edge::RIGHT) {
        hide();
        return;
    }
    if (stripVisible) {
        stripRect = {static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)),
                     static_cast<int>(std::ceil(x1 - x0)), static_cast<int>(std::ceil(y1 - y0))};
    }
    // keep the pill inside the window: when there is no room after the strip it sits on it
    pillX = std::min(pillX, static_cast<double>(vw - nat.width - PILL_GAP));
    pillRect = {static_cast<int>(std::round(pillX)), static_cast<int>(std::round(pillY)), nat.width, nat.height};

    // the last page: the page after it is offered left of the pill, on the same row
    bool last = false;
    {
        Document* doc = control->getDocument();
        doc->lock_shared();
        last = doc->getPageCount() > 0 && doc->indexOf(page) + 1 == doc->getPageCount();
        doc->unlock_shared();
    }
    if (edge == Edge::BOTTOM && last) {
        GtkRequisition newNat;
        gtk_widget_get_preferred_size(newPagePill.get(), nullptr, &newNat);
        double nx = std::max(0.0, static_cast<double>(pillRect.x - PILL_GAP - newNat.width));
        newPageRect = {static_cast<int>(std::round(nx)), pillRect.y, newNat.width, newNat.height};
        gtk_widget_show(newPagePill.get());
        gtk_widget_queue_resize(newPagePill.get());
    } else {
        gtk_widget_hide(newPagePill.get());
    }

    if (stripVisible) {
        gtk_widget_show(strip.get());
        gtk_widget_queue_resize(strip.get());
        gtk_widget_queue_draw(strip.get());
    } else {
        gtk_widget_hide(strip.get());
    }
    gtk_widget_show(pill.get());
    gtk_widget_queue_resize(pill.get());
}

void PageGrowHandle::grow() {
    if (edge == Edge::NONE || !findView(xournal, page)) {
        hide();
        return;
    }
    if (edge == Edge::RIGHT) {
        control->growPage(page, Control::GROW_STEP, 0);
    } else {
        control->growPage(page, 0, Control::GROW_STEP);
    }
    // the layout is recomputed after the size change: place the handle on the new edge then
    g_idle_add_full(
            G_PRIORITY_LOW,
            +[](gpointer s) -> gboolean {
                static_cast<PageGrowHandle*>(s)->place();
                return G_SOURCE_REMOVE;
            },
            this, nullptr);
}

gboolean PageGrowHandle::onChildPosition(GtkOverlay*, GtkWidget* w, GdkRectangle* alloc, PageGrowHandle* self) {
    GdkRectangle r;
    if (w == self->strip.get()) {
        r = self->stripRect;
    } else if (w == self->pill.get()) {
        r = self->pillRect;
    } else if (w == self->newPagePill.get()) {
        r = self->newPageRect;
    } else {
        return false;
    }
    // never hand GTK an empty allocation (the first query can come before place())
    GtkRequisition nat;
    gtk_widget_get_preferred_size(w, nullptr, &nat);
    r.width = std::max(r.width, std::max(nat.width, 1));
    r.height = std::max(r.height, std::max(nat.height, 1));
    GtkWidget* from = self->xournal->getWidget();
    gtk_widget_translate_coordinates(from, GTK_WIDGET(self->overlay), r.x, r.y, &r.x, &r.y);
    *alloc = r;
    return true;
}

gboolean PageGrowHandle::onDrawStrip(GtkWidget* w, cairo_t* cr, PageGrowHandle* self) {
    int width = gtk_widget_get_allocated_width(w);
    int height = gtk_widget_get_allocated_height(w);
    GdkRGBA c;
    gtk_style_context_get_color(gtk_widget_get_style_context(w), gtk_widget_get_state_flags(w), &c);

    // stripes at 135°, 6 px on, 6 px off, anchored to the page so they do not slide while scrolling
    auto scroll = self->xournal->getScrollHandling()->getPosition();
    double ox = self->stripRect.x + scroll.x;
    double oy = self->stripRect.y + scroll.y;
    double phase = std::fmod(ox + oy, 12.0);
    cairo_set_source_rgba(cr, c.red, c.green, c.blue, 0.14);
    for (double k = -height - phase; k < width + height; k += 12) {
        cairo_move_to(cr, k, 0);
        cairo_line_to(cr, k + 6, 0);
        cairo_line_to(cr, k + 6 - height, height);
        cairo_line_to(cr, k - height, height);
        cairo_close_path(cr);
    }
    cairo_fill(cr);

    // dashed outline on the sides away from the page
    double dash[] = {4, 3};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, c.red, c.green, c.blue, 0.55);
    if (self->edge == Edge::RIGHT) {
        cairo_move_to(cr, 0, 0.5);
        cairo_line_to(cr, width - 0.5, 0.5);
        cairo_line_to(cr, width - 0.5, height - 0.5);
        cairo_line_to(cr, 0, height - 0.5);
    } else {
        cairo_move_to(cr, 0.5, 0);
        cairo_line_to(cr, 0.5, height - 0.5);
        cairo_line_to(cr, width - 0.5, height - 0.5);
        cairo_line_to(cr, width - 0.5, 0);
    }
    cairo_stroke(cr);
    return true;
}

}  // namespace ambitus
