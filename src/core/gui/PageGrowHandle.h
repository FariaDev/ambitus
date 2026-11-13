/*
 * Ambitus
 *
 * Grow handle: when the pointer comes near the right or bottom edge of a page,
 * a striped strip previews the 2 cm the page would gain and a "+2 cm" pill
 * makes it grow. Both live in the GtkOverlay above the pages, so the page
 * rendering of Xournal++ is left alone.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>

#include "model/PageRef.h"  // for PageRef
#include "util/raii/GObjectSPtr.h"

class Control;
class XournalView;
class XojPageView;

namespace ambitus {

class PageGrowHandle {
public:
    PageGrowHandle(Control* control, XournalView* xournal, GtkOverlay* overlay);
    ~PageGrowHandle();
    PageGrowHandle(const PageGrowHandle&) = delete;
    PageGrowHandle& operator=(const PageGrowHandle&) = delete;

    void hide();

private:
    enum class Edge { NONE, RIGHT, BOTTOM };

    /// Pointer moved over the pages, in widget coordinates; pressed: a button or the pen is down
    void pointerAt(double x, double y, bool pressed);
    /// Recomputes where the strip and the pill go for the current page and edge
    void place();
    void grow();

    static gboolean onEvent(GSignalInvocationHint*, guint n, const GValue* params, gpointer self);
    static gboolean onChildPosition(GtkOverlay*, GtkWidget* w, GdkRectangle* alloc, PageGrowHandle* self);
    static gboolean onDrawStrip(GtkWidget* w, cairo_t* cr, PageGrowHandle* self);

    Control* control;
    XournalView* xournal;
    GtkOverlay* overlay;
    xoj::util::WidgetSPtr strip;
    xoj::util::WidgetSPtr pill;
    GtkWidget* pillIcon = nullptr;
    gulong hookId = 0;
    gulong positionId = 0;
    gulong scrollIds[2] = {0, 0};

    Edge edge = Edge::NONE;
    PageRef page;
    double pointerY = 0;  ///< where the pill follows along the edge (x or y, depending on the edge)
    double pointerX = 0;
    GdkRectangle stripRect{};  ///< in scrolled-window coordinates
    GdkRectangle pillRect{};
};

}  // namespace ambitus
