/*
 * Ambitus
 *
 * Navigation of a board, in the corner of the view: a minimap with the content
 * and the part on screen (click or drag to go there), and a pill with the zoom
 * (minus, percent, plus) and "Go to the content", which zooms and scrolls so
 * everything written fits on screen.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>

#include <gtk/gtk.h>

#include "control/zoom/ZoomListener.h"
#include "util/Range.h"

class Control;
class XournalView;

namespace ambitus {

class BoardNavigator: public ZoomListener {
public:
    BoardNavigator(Control* control, XournalView* xournal, GtkOverlay* overlay);
    ~BoardNavigator() override;
    BoardNavigator(const BoardNavigator&) = delete;
    BoardNavigator& operator=(const BoardNavigator&) = delete;

    void setVisible(bool visible);
    /// Content changed: the minimap is redrawn from the document
    void contentChanged();

    void zoomChanged() override;

    static constexpr int MAP_WIDTH = 200;
    static constexpr int MAP_HEIGHT = 124;

private:
    /// Scale and offset that fit the page in the minimap
    struct MapFit {
        double scale, x0, y0;
    };
    MapFit fit() const;
    /// The part of the page on screen, in page coordinates
    Range shownRange() const;
    /// Centres the view on (x, y), in page coordinates
    void centreOn(double x, double y);
    void goToContent();
    void updateContent();

    static gboolean onDrawMap(GtkWidget* w, cairo_t* cr, BoardNavigator* self);
    static gboolean onMapButton(GtkWidget* w, GdkEventButton* e, BoardNavigator* self);
    static gboolean onMapMotion(GtkWidget* w, GdkEventMotion* e, BoardNavigator* self);

    Control* control;
    XournalView* xournal;
    GtkWidget* map = nullptr;
    GtkWidget* pill = nullptr;
    GtkWidget* percent = nullptr;
    gulong scrollIds[4] = {};
    bool visible = false;
    bool dragging = false;

    double pageWidth = 0, pageHeight = 0;
    std::vector<Range> blocks;  ///< bounding boxes of the content, in page coordinates
};

}  // namespace ambitus
