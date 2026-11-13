/*
 * Ambitus
 *
 * Radial menu: a ring of tools that opens where the pointer is, on the right
 * mouse button (or the second stylus button) when that button has no tool of
 * its own. Pen, highlighter, eraser, lasso, text, shapes, LaTeX and undo.
 *
 * Two ways to use it: press, move to an item and release; or click, then click
 * an item. A click outside or Esc closes it.
 *
 * The ring is drawn on a layer of the GtkOverlay above the pages; while the
 * button that opened it is down, the events still go to the pages widget, so
 * InputContext hands them over (InputContext::ambitusPreHandler).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "gui/inputdevices/InputEvents.h"

class Control;
class XournalView;

namespace ambitus {

class RadialMenu {
public:
    RadialMenu(Control* control, XournalView* xournal, GtkOverlay* overlay);
    ~RadialMenu();
    RadialMenu(const RadialMenu&) = delete;
    RadialMenu& operator=(const RadialMenu&) = delete;

    bool isOpen() const { return open; }
    /// Opens the ring centred at (x, y), in overlay coordinates
    void openAt(double x, double y);
    void close();

    static constexpr double SIZE = 208;   ///< diameter of the ring
    static constexpr double HOLE = 76;    ///< diameter of the hole in the middle
    static constexpr double RADIUS = 82;  ///< distance from the centre to the items
    static constexpr double ITEM = 40;    ///< diameter of an item

    /// Index of the item at (dx, dy) from the centre, -1 for none (the hole, outside)
    static int itemAt(double dx, double dy, size_t count);

private:
    struct Item {
        const char* icon;
        std::string label;
    };

    /// From InputContext: a free secondary button opens the ring; while it is held, moves and the release go here
    bool onPagesEvent(const InputEvent& e);
    void hover(double x, double y);
    void activate(int index);
    int activeItem() const;

    static gboolean onDraw(GtkWidget* w, cairo_t* cr, RadialMenu* self);
    static gboolean onButton(GtkWidget* w, GdkEventButton* e, RadialMenu* self);
    static gboolean onMotion(GtkWidget* w, GdkEventMotion* e, RadialMenu* self);
    static gboolean onKey(GtkWidget* w, GdkEventKey* e, RadialMenu* self);

    Control* control;
    XournalView* xournal;
    GtkOverlay* overlay;
    GtkWidget* layer = nullptr;
    std::vector<Item> items;

    bool open = false;
    bool held = false;      ///< the button that opened the ring is still down
    bool moved = false;     ///< the pointer left the hole while held
    double cx = 0, cy = 0;  ///< centre, in overlay coordinates
    int hovered = -1;
};

}  // namespace ambitus
