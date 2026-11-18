/*
 * Ambitus
 *
 * The notebook beside the one in the window: in the right part of the editor, read only. It scrolls, turns its pages
 * and zooms with its own footer (Ctrl+scroll too); ink never reaches it, as its header keeps it apart from the
 * editor: "Write here" swaps the two notebooks, ✕ closes the pane and the notebook stays a tab.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <map>
#include <memory>
#include <vector>

#include <gtk/gtk.h>

#include "ambitus/Tabs.h"
#include "util/raii/CairoWrappers.h"

class Control;
class PdfCache;

namespace ambitus {

class SidePane {
public:
    /// split: the GtkPaned of the editor; the pane becomes its second child, hidden until a notebook is beside
    SidePane(Control* control, GtkWidget* split);
    ~SidePane();

    /// Shows the notebook beside (nothing when `side.doc` is null). Its pages and zoom stay while it stays beside.
    void show(const SideNotebook& side);
    /// Whether the pane is on screen, that is a notebook is beside
    bool isShown() const;

private:
    /// The pages of the notebook at the zoom of the pane: where each one starts, and the size of the drawing
    void layoutPages();
    /// Scrolls to the page once the pages have their size; until then it waits
    void applyPendingPage();
    void goToPage(size_t page);
    void zoomBy(double factor);
    /// The page whose top is at the top of the pane
    size_t pageAtTop() const;
    /// The page turned by the scroll (the wheel, the bar, the buttons): it is kept for the window
    void onScroll();
    void updateFooter();
    cairo_surface_t* pageSurface(size_t i);
    void drawPages(cairo_t* cr);

    Control* control = nullptr;
    GtkWidget* split = nullptr;
    GtkWidget* panel = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* stage = nullptr;
    GtkWidget* pages = nullptr;
    GtkWidget* prev = nullptr;
    GtkWidget* next = nullptr;
    GtkWidget* pageLabel = nullptr;
    GtkWidget* zoomLabel = nullptr;
    GtkAdjustment* vadj = nullptr;

    gulong positionId = 0;
    gulong scrollId = 0;
    guint ratioIdle = 0;
    int ratioTries = 0;
    /// the divider moves by GTK while the pane comes in: that position is not kept as the choice of the user
    bool ignorePosition = false;

    // the notebook beside, as the window gave it: owned by its tab, so it is only looked at while it is beside
    Document* doc = nullptr;
    unsigned serial = 0;
    std::unique_ptr<PdfCache> cache;
    int boundPage = -1;  ///< the page the window last gave: a change of it turns the notebook there
    int pendingPage = -1;
    size_t page = 0;  ///< the page at the top of the pane
    double zoom = 0.8;
    std::vector<double> tops;
    std::vector<double> widths;
    std::vector<double> heights;
    double totalHeight = 0;
    std::map<size_t, xoj::util::CairoSurfaceSPtr> surfaces;  ///< the pages on screen, drawn at the current zoom
};

}  // namespace ambitus
