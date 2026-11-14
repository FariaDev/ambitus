/*
 * Ambitus
 *
 * The floating bar over a selection (lasso, rectangle or object): convert the handwriting to text, copy, delete, four
 * colours and a "..." with the rest. It sits just above the selection, centred on it, or below it when there is no room
 * above, and always inside the visible area. It goes away while the selection is dragged or resized, and comes back
 * when it is let go. It exists in the Studio and Focus modes, not in the Classic one.
 *
 * It lives in the GtkOverlay of the pages, like the Studio palette. Every button is bound to an action of the window
 * (win.copy, win.delete, win.convert-to-text, win.tool-color...), so Xournal++ does the work. The window tells it when
 * the selection appears, changes or goes away (Control::clipboardCutCopyEnabled); while there is a selection, a frame
 * callback follows it through scroll, zoom, drag and window resize.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>
#include <cstdint>

#include <gtk/gtk.h>

class Control;
class XournalView;

namespace ambitus {

class SelectionBar {
public:
    SelectionBar(Control* control, XournalView* xournal, GtkOverlay* overlay);
    ~SelectionBar();
    SelectionBar(const SelectionBar&) = delete;
    SelectionBar& operator=(const SelectionBar&) = delete;

    /// The selection appeared, changed or went away, or the editor mode changed: shows, updates or hides the bar
    void selectionChanged();

    static constexpr size_t DOTS = 4;     ///< colours on the bar: the first ones of the Studio palette
    static constexpr int GAP = 8;         ///< px between the bar and the selection
    static constexpr int EDGE = 4;        ///< px the bar keeps from the edges of the visible area
    static constexpr int FADE_IN = 180;   ///< ms
    static constexpr int FADE_OUT = 120;  ///< ms
    static constexpr int COPIED_MS = 1200;

private:
    /// Whether the editor mode has the bar (Studio and Focus, and boards)
    bool enabled() const;
    /// The bounding box of the selection in overlay coordinates and the visible area of the pages, in the same ones
    bool selectionRect(GdkRectangle& sel, GdkRectangle& visible) const;
    /// Follows the selection and hides the bar while it is dragged; false when there is nothing to follow (no
    /// selection, or the Classic mode), so the frame callback can stop
    bool update();
    void setShown(bool show);
    /// Fills in what depends on the selection (the "Convert to text" button) and on the settings (the colours)
    void loadContents();
    /// Where the bar goes: alloc gets the position; false for a widget that is not the bar
    bool place(GdkRectangle* alloc);
    void copied();

    GtkWidget* buildMenu();

    static gboolean onChildPosition(GtkOverlay*, GtkWidget* w, GdkRectangle* alloc, SelectionBar* self);
    static gboolean onTick(GtkWidget*, GdkFrameClock*, gpointer self);

    Control* control;
    XournalView* xournal;
    GtkOverlay* overlay;

    GtkWidget* root = nullptr;  ///< The GtkRevealer: the fade
    GtkWidget* bar = nullptr;   ///< The box with the buttons
    GtkWidget* convert = nullptr;
    GtkWidget* convertSep = nullptr;
    GtkWidget* copyIcon = nullptr;
    GtkWidget* more = nullptr;
    GtkWidget* menu = nullptr;  ///< The popover of "..."
    std::array<GtkWidget*, DOTS> dots{};

    gulong positionId = 0;
    guint tickId = 0;
    guint copiedTimer = 0;
    bool shown = false;     ///< The bar is (or is going to be) on screen
    bool below = false;     ///< It is under the selection (else above)
    GdkRectangle last{};    ///< The selection rectangle at the last frame, to see when it moved
    GdkRectangle placed{};  ///< Where the bar was put last, kept while it fades out
};

}  // namespace ambitus
