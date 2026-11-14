/*
 * Ambitus
 *
 * Radial menu: a ring of tools that opens where the pointer is, on the right
 * mouse button (or the second stylus button) when that button has no tool of
 * its own. It has eight positions, each one a tool or an action; the user picks
 * them in the panel of the Studio palette and they are read from the settings
 * every time the ring opens. The name of the item under the pointer shows in
 * the hole, and an action that is disabled is drawn faded and does nothing.
 *
 * It grows in (98.5% to 100%, with a fade, 180 ms) and fades out (120 ms) with the ease-out of the Summa; with
 * gtk-enable-animations off it appears and goes away at once. The exit takes no events (the layer hands them on to the
 * pages) and never holds anything back: focus returns to the page and a chosen action runs when it is chosen.
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

#include "gui/StudioPalette.h"
#include "gui/inputdevices/InputEvents.h"

class Control;
class XournalView;

namespace ambitus {

class PageMenu;

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

    /// The page menu opens on the right button of the mouse or touchpad, in the modes that have it; the stylus
    /// button, and the mouse where the page menu is not available (Classic), keep the ring
    static bool opensPageMenu(InputDeviceClass device, bool pageMenuAvailable) {
        return pageMenuAvailable && device == INPUT_DEVICE_MOUSE;
    }
    void setPageMenu(PageMenu* menu) { pageMenu = menu; }

    /// Index of the item at (dx, dy) from the centre, -1 for none (the hole, outside)
    static int itemAt(double dx, double dy, size_t count);

private:
    struct Item {
        const RadialOption* option;
        std::string label;
    };

    /// From InputContext: a free secondary button opens the ring; while it is held, moves and the release go here
    bool onPagesEvent(const InputEvent& e);
    void hover(double x, double y);
    /// While the ring fades out, the events that reach its layer go on to the pages
    bool relayToPage(GdkEvent* e);
    void activate(int index);
    /// Reads the eight positions from the settings
    void loadItems();
    /// Fades the ring towards `to` (1 in, 0 out) from the opacity it has now; `grow` adds the zoom of the entrance
    void animateTo(double to, gint64 lenUs, bool grow);
    void stopAnimation();
    /// The end of the exit: takes the layer off the screen
    void hideLayer();
    /// Whether the position holds the tool in use (with the shape recognizer, the pen is "shapes")
    bool isActive(size_t index) const;
    /// An action that the window has disabled does nothing; tools are always enabled
    bool isEnabled(size_t index) const;

    static gboolean onDraw(GtkWidget* w, cairo_t* cr, RadialMenu* self);
    static gboolean onButton(GtkWidget* w, GdkEventButton* e, RadialMenu* self);
    static gboolean onMotion(GtkWidget* w, GdkEventMotion* e, RadialMenu* self);
    static gboolean onKey(GtkWidget* w, GdkEventKey* e, RadialMenu* self);

    Control* control;
    XournalView* xournal;
    GtkOverlay* overlay;
    PageMenu* pageMenu = nullptr;  ///< the right button of the mouse opens it instead of the ring
    bool relaying = false;         ///< a press that came during the fade was handed to the pages, its release follows
    bool swallowRelease = false;   ///< the page menu opened on a press; its release is not for the page
    GtkWidget* layer = nullptr;
    std::vector<Item> items;

    bool open = false;
    bool held = false;      ///< the button that opened the ring is still down
    bool moved = false;     ///< the pointer left the hole while held
    double cx = 0, cy = 0;  ///< centre, in overlay coordinates
    int hovered = -1;

    // the animation of the ring: its opacity and zoom now, and the run that is going on (tick == 0: none)
    double opacity = 0;
    double zoom = 1;
    guint tick = 0;
    gint64 animStart = -1;
    gint64 animLen = 0;
    double animFrom = 0;
    double animTo = 0;
    bool animGrow = false;
};

}  // namespace ambitus
