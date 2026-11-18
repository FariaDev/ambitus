/*
 * Ambitus
 *
 * The notebook beside the one in the window (SidePane.h). It draws the pages of its document, which is kept by its
 * tab, and changes nothing in it.
 *
 * @license GNU GPLv2 or later
 */

#include "SidePane.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "ambitus/Drafts.h"
#include "ambitus/RenderWindow.h"
#include "control/Control.h"
#include "control/PdfCache.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "util/StringUtils.h"  // for char_cast
#include "util/i18n.h"
#include "view/DocumentView.h"

namespace ambitus {

namespace {
constexpr double MARGIN = 16;  // around the pages, as in the panel of versions
constexpr double GAP = 14;     // between two pages
constexpr double MIN_ZOOM = 0.2;
constexpr double MAX_ZOOM = 4;
constexpr double DEFAULT_ZOOM = 0.8;
constexpr double BUTTON_ZOOM_STEP = 1.25;
constexpr double WHEEL_ZOOM_STEP = 1.1;
/// the divider is set once the window has its width (a pane narrower than this is not a split)
constexpr int MIN_SPLIT_WIDTH = 100;
constexpr int RATIO_TRIES = 200;

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

GtkWidget* label(const std::string& text, const char* cssClass) {
    GtkWidget* l = gtk_label_new(text.c_str());
    addClass(l, cssClass);
    return l;
}
}  // namespace

SidePane::SidePane(Control* control, GtkWidget* split): control(control), split(split) {
    panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(panel, "amb-side");

    // the header: the columns icon, the name, "Write here" (which swaps the two notebooks) and ✕ (which closes the
    // pane)
    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addClass(head, "amb-side-head");
    GtkWidget* icon = gtk_image_new_from_icon_name("xopp-columns-2", GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
    gtk_box_pack_start(GTK_BOX(head), icon, false, false, 0);
    title = label("", "amb-side-title");
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_pack_start(GTK_BOX(head), title, true, true, 0);

    GtkWidget* swap = gtk_button_new();
    addClass(swap, "amb-side-swap");
    gtk_widget_set_focus_on_click(swap, false);
    GtkWidget* swapBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(swapBox), gtk_image_new_from_icon_name("xopp-arrow-right-left", GTK_ICON_SIZE_BUTTON),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(swapBox), label(_("Write here"), "amb-side-swap-text"), false, false, 0);
    gtk_container_add(GTK_CONTAINER(swap), swapBox);
    g_signal_connect_swapped(
            swap, "clicked",
            G_CALLBACK(+[](SidePane* self) { self->control->activateTab(self->control->getSideTab()); }), this);
    gtk_box_pack_start(GTK_BOX(head), swap, false, false, 0);

    GtkWidget* close = gtk_button_new_from_icon_name("xopp-x", GTK_ICON_SIZE_BUTTON);
    addClass(close, "amb-side-close");
    gtk_widget_set_tooltip_text(close, _("Close"));
    gtk_widget_set_focus_on_click(close, false);
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(+[](SidePane* self) { self->control->closeSideTab(); }),
                             this);
    gtk_box_pack_start(GTK_BOX(head), close, false, false, 0);
    gtk_box_pack_start(GTK_BOX(panel), head, false, false, 0);

    // the pages: the wheel scrolls them, Ctrl+wheel zooms; nothing else reaches them
    stage = gtk_scrolled_window_new(nullptr, nullptr);
    addClass(stage, "amb-side-stage");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(stage), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(stage, true);
    pages = gtk_drawing_area_new();
    addClass(pages, "amb-side-pages");
    gtk_widget_add_events(pages, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(pages, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, SidePane* self) -> gboolean {
                         self->drawPages(cr);
                         return TRUE;
                     }),
                     this);
    g_signal_connect(pages, "size-allocate",
                     G_CALLBACK(+[](GtkWidget*, GdkRectangle*, SidePane* self) { self->applyPendingPage(); }), this);
    g_signal_connect(pages, "scroll-event", G_CALLBACK(+[](GtkWidget*, GdkEventScroll* e, SidePane* self) -> gboolean {
                         if (!(e->state & (GDK_CONTROL_MASK | GDK_META_MASK))) {
                             return FALSE;  // the wheel alone scrolls the pages
                         }
                         double dy = 0;
                         if (e->direction == GDK_SCROLL_UP) {
                             dy = -1;
                         } else if (e->direction == GDK_SCROLL_DOWN) {
                             dy = 1;
                         } else if (e->direction == GDK_SCROLL_SMOOTH) {
                             dy = e->delta_y;
                         }
                         if (dy != 0) {
                             self->zoomBy(dy < 0 ? WHEEL_ZOOM_STEP : 1 / WHEEL_ZOOM_STEP);
                         }
                         return TRUE;
                     }),
                     this);
    gtk_container_add(GTK_CONTAINER(stage), pages);
    gtk_box_pack_start(GTK_BOX(panel), stage, true, true, 0);
    vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(stage));
    scrollId = g_signal_connect_swapped(vadj, "value-changed", G_CALLBACK(+[](SidePane* self) { self->onScroll(); }),
                                        this);

    // the footer: the page, which turns the pages, and the zoom
    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    addClass(foot, "amb-side-foot");
    prev = gtk_button_new_from_icon_name("xopp-chevron-left", GTK_ICON_SIZE_BUTTON);
    addClass(prev, "amb-side-step");
    gtk_widget_set_focus_on_click(prev, false);
    g_signal_connect_swapped(prev, "clicked",
                             G_CALLBACK(+[](SidePane* self) { self->goToPage(self->page == 0 ? 0 : self->page - 1); }),
                             this);
    gtk_box_pack_start(GTK_BOX(foot), prev, false, false, 0);
    pageLabel = label("", "amb-side-page");
    gtk_box_pack_start(GTK_BOX(foot), pageLabel, false, false, 0);
    next = gtk_button_new_from_icon_name("xopp-chevron-right", GTK_ICON_SIZE_BUTTON);
    addClass(next, "amb-side-step");
    gtk_widget_set_focus_on_click(next, false);
    g_signal_connect_swapped(next, "clicked", G_CALLBACK(+[](SidePane* self) { self->goToPage(self->page + 1); }),
                             this);
    gtk_box_pack_start(GTK_BOX(foot), next, false, false, 0);
    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(foot), spacer, true, true, 0);
    GtkWidget* zoomOut = gtk_button_new_with_label("−");
    addClass(zoomOut, "amb-side-zoom");
    gtk_widget_set_focus_on_click(zoomOut, false);
    g_signal_connect_swapped(zoomOut, "clicked",
                             G_CALLBACK(+[](SidePane* self) { self->zoomBy(1 / BUTTON_ZOOM_STEP); }), this);
    gtk_box_pack_start(GTK_BOX(foot), zoomOut, false, false, 0);
    zoomLabel = label("", "amb-side-zoom-value");
    gtk_box_pack_start(GTK_BOX(foot), zoomLabel, false, false, 0);
    GtkWidget* zoomIn = gtk_button_new_with_label("+");
    addClass(zoomIn, "amb-side-zoom");
    gtk_widget_set_focus_on_click(zoomIn, false);
    g_signal_connect_swapped(zoomIn, "clicked", G_CALLBACK(+[](SidePane* self) { self->zoomBy(BUTTON_ZOOM_STEP); }),
                             this);
    gtk_box_pack_start(GTK_BOX(foot), zoomIn, false, false, 0);
    gtk_box_pack_start(GTK_BOX(panel), foot, false, false, 0);

    gtk_widget_show_all(panel);
    gtk_widget_set_no_show_all(panel, true);
    gtk_widget_hide(panel);
    gtk_paned_pack2(GTK_PANED(split), panel, false, false);

    // the divider: the share of the width the pane takes is kept in the settings
    positionId = g_signal_connect(
            split, "notify::position", G_CALLBACK(+[](GtkPaned* paned, GParamSpec*, SidePane* self) {
                if (self->ignorePosition || !gtk_widget_get_visible(self->panel)) {
                    return;
                }
                const int width = gtk_widget_get_allocated_width(GTK_WIDGET(paned));
                if (width < MIN_SPLIT_WIDTH) {
                    return;
                }
                const int left = gtk_paned_get_position(paned);
                self->control->getSettings()->setAmbitusSideRatio(static_cast<double>(width - left) / width);
            }),
            this);
}

bool SidePane::isShown() const { return gtk_widget_get_visible(panel); }

SidePane::~SidePane() {
    if (ratioIdle) {
        g_source_remove(ratioIdle);
    }
    if (positionId) {
        g_signal_handler_disconnect(split, positionId);
    }
    if (scrollId) {
        g_signal_handler_disconnect(vadj, scrollId);
    }
}

void SidePane::show(const SideNotebook& side) {
    if (!side.doc) {
        // no notebook beside: the pane goes, and the pages with it
        doc = nullptr;
        serial = 0;
        cache.reset();
        surfaces.clear();
        tops.clear();
        widths.clear();
        heights.clear();
        totalHeight = 0;
        boundPage = -1;
        pendingPage = -1;
        gtk_widget_hide(panel);
        return;
    }

    if (!gtk_widget_get_visible(panel)) {
        // the divider goes to the share kept in the settings, once the window has its width; the position that GTK
        // gives the pane meanwhile is not a choice of the user
        ignorePosition = true;
        ratioTries = 0;
        if (!ratioIdle) {
            ratioIdle = g_idle_add(
                    +[](gpointer d) -> gboolean {
                        auto* self = static_cast<SidePane*>(d);
                        const int width = gtk_widget_get_allocated_width(self->split);
                        if (width < MIN_SPLIT_WIDTH && self->ratioTries++ < RATIO_TRIES) {
                            return G_SOURCE_CONTINUE;
                        }
                        if (width >= MIN_SPLIT_WIDTH) {
                            const double ratio = self->control->getSettings()->getAmbitusSideRatio();
                            gtk_paned_set_position(GTK_PANED(self->split),
                                                   static_cast<int>(std::lround(width * (1 - ratio))));
                        }
                        self->ignorePosition = false;
                        self->ratioIdle = 0;
                        return G_SOURCE_REMOVE;
                    },
                    this);
        }
        gtk_widget_show(panel);
    }

    if (side.doc != doc || side.serial != serial) {
        // another notebook beside: its pages are drawn afresh, at the default zoom
        doc = side.doc;
        serial = side.serial;
        zoom = DEFAULT_ZOOM;
        doc->lock_shared();
        cache = std::make_unique<PdfCache>(doc->getPdfDocument(), nullptr);
        doc->unlock_shared();
        boundPage = -1;
        page = 0;
        layoutPages();
    }

    std::string name;
    if (side.path.empty()) {
        name = _("Untitled");
    } else {
        const std::string stem = std::string(char_cast(side.path.stem().u8string()));
        name = drafts::isDraft(side.path) ? drafts::tabLabel(stem) : stem;
    }
    gtk_label_set_text(GTK_LABEL(title), name.c_str());

    if (side.page != boundPage) {
        // the window gave another page: the notebook turns there
        boundPage = side.page;
        pendingPage = side.page;
        applyPendingPage();
    }
    updateFooter();
}

void SidePane::layoutPages() {
    tops.clear();
    widths.clear();
    heights.clear();
    totalHeight = 0;
    double widest = 0;
    if (doc) {
        doc->lock_shared();
        double y = MARGIN;
        const size_t count = doc->getPageCount();
        for (size_t i = 0; i < count; i++) {
            PageRef p = doc->getPage(i);
            const double w = p->getWidth() * zoom;
            const double h = p->getHeight() * zoom;
            tops.push_back(y);
            widths.push_back(w);
            heights.push_back(h);
            widest = std::max(widest, w);
            y += h + GAP;
        }
        doc->unlock_shared();
        if (!tops.empty()) {
            totalHeight = y - GAP + MARGIN;
        }
    }
    surfaces.clear();
    gtk_widget_set_size_request(pages, static_cast<int>(std::ceil(widest + 2 * MARGIN)),
                                static_cast<int>(std::ceil(totalHeight)));
    gtk_widget_queue_draw(pages);
}

void SidePane::applyPendingPage() {
    if (pendingPage < 0 || tops.empty() || !doc) {
        return;
    }
    // the scroll is set once the drawing has the height of the pages: before that, the page would be cut short
    if (gtk_widget_get_allocated_height(pages) < static_cast<int>(std::ceil(totalHeight))) {
        return;
    }
    const size_t target = std::min(static_cast<size_t>(pendingPage), tops.size() - 1);
    pendingPage = -1;
    goToPage(target);
}

void SidePane::goToPage(size_t p) {
    if (tops.empty()) {
        return;
    }
    p = std::min(p, tops.size() - 1);
    gtk_adjustment_set_value(vadj, std::max(0.0, tops[p] - 4));
    onScroll();
}

void SidePane::zoomBy(double factor) {
    if (!doc) {
        return;
    }
    const double next = std::clamp(zoom * factor, MIN_ZOOM, MAX_ZOOM);
    if (next == zoom) {
        return;
    }
    // the page on top stays on top at the new zoom
    const size_t keep = page;
    zoom = next;
    layoutPages();
    pendingPage = static_cast<int>(keep);
    applyPendingPage();
    updateFooter();
}

size_t SidePane::pageAtTop() const {
    if (tops.empty()) {
        return 0;
    }
    const double top = gtk_adjustment_get_value(vadj) + 4;
    size_t p = 0;
    for (size_t i = 0; i < tops.size() && tops[i] <= top; i++) {
        p = i;
    }
    return p;
}

void SidePane::onScroll() {
    if (!doc || tops.empty()) {
        return;
    }
    const size_t p = pageAtTop();
    if (p == page) {
        return;
    }
    page = p;
    boundPage = static_cast<int>(p);
    // the page is kept with the notebook: the window reads it when the notebook comes into it
    control->storeSidePage(static_cast<int>(p));
    updateFooter();
}

void SidePane::updateFooter() {
    const size_t count = tops.size();
    gtk_label_set_text(GTK_LABEL(pageLabel), count == 0 ? "" : FS(_F("Page {1} of {2}") % (page + 1) % count).c_str());
    gtk_widget_set_sensitive(prev, page > 0);
    gtk_widget_set_sensitive(next, count > 0 && page + 1 < count);
    gtk_label_set_text(GTK_LABEL(zoomLabel), (std::to_string(std::lround(zoom * 100)) + " %").c_str());
}

cairo_surface_t* SidePane::pageSurface(size_t i) {
    auto it = surfaces.find(i);
    if (it != surfaces.end()) {
        return it->second.get();
    }
    PageRef p = doc->getPage(i);
    const int dpi = gtk_widget_get_scale_factor(pages);
    if (render::isLarge(p->getWidth(), p->getHeight(), zoom, dpi)) {
        return nullptr;  // too large to keep whole: drawn straight to the screen
    }
    xoj::util::CairoSurfaceSPtr surface(
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(std::ceil(p->getWidth() * zoom * dpi)),
                                       static_cast<int>(std::ceil(p->getHeight() * zoom * dpi))),
            xoj::util::adopt);
    cairo_surface_set_device_scale(surface.get(), dpi, dpi);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    cairo_scale(cr.get(), zoom, zoom);
    DocumentView view;
    view.setPdfCache(cache.get());
    view.drawPage(p, cr.get(), true);
    return (surfaces[i] = std::move(surface)).get();
}

void SidePane::drawPages(cairo_t* cr) {
    const int width = gtk_widget_get_allocated_width(pages);
    const int height = gtk_widget_get_allocated_height(pages);
    gtk_render_background(gtk_widget_get_style_context(pages), cr, 0, 0, width, height);
    if (!doc) {
        return;
    }
    double clipTop = 0, clipBottom = height, ignoredX = 0, ignoredX2 = 0;
    cairo_clip_extents(cr, &ignoredX, &clipTop, &ignoredX2, &clipBottom);

    doc->lock_shared();
    std::vector<size_t> visible;
    for (size_t i = 0; i < tops.size(); i++) {
        if (tops[i] + heights[i] < clipTop || tops[i] > clipBottom) {
            continue;
        }
        visible.push_back(i);
        const double w = widths[i];
        const double h = heights[i];
        const double x = std::round((width - w) / 2);
        const double y = std::round(tops[i]);
        // a soft edge: three frames, each fainter than the one inside it
        for (int k = 3; k >= 1; k--) {
            cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
            cairo_rectangle(cr, x - k, y - k + 1, w + 2 * k, h + 2 * k);
            cairo_fill(cr);
        }
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, x - .5, y - .5, w + 1, h + 1);
        cairo_stroke(cr);
        if (cairo_surface_t* s = pageSurface(i)) {
            cairo_set_source_surface(cr, s, x, y);
            cairo_paint(cr);
        } else {
            PageRef p = doc->getPage(i);
            cairo_save(cr);
            cairo_translate(cr, x, y);
            cairo_scale(cr, zoom, zoom);
            cairo_rectangle(cr, 0, 0, p->getWidth(), p->getHeight());
            cairo_clip(cr);
            DocumentView view;
            view.setPdfCache(cache.get());
            view.drawPage(p, cr, true);
            cairo_restore(cr);
        }
    }
    doc->unlock_shared();

    // the pages that scrolled out of sight are drawn again if they come back: only the ones on screen are kept
    for (auto it = surfaces.begin(); it != surfaces.end();) {
        it = std::find(visible.begin(), visible.end(), it->first) == visible.end() ? surfaces.erase(it) : std::next(it);
    }
}

}  // namespace ambitus
