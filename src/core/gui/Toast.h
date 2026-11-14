/*
 * Ambitus
 *
 * The notices at the bottom of the page area, centered, over the page: the
 * same three as Summa. A strip (a short text that goes away by itself, or what
 * an action is doing, with a spinner), a card with an action and the seconds
 * left to use it ("Undo"), and an error card that stays until it is closed and
 * keeps the technical detail out of the text: a button copies it. Not dialogs:
 * the page stays usable. Saving by itself never shows one.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <gtk/gtk.h>

namespace ambitus {

namespace toast {
/// How long the strip stays (ms)
constexpr int64_t STRIP_MS = 4000;
/// How long the card with an action stays, as the "Undo" of Summa (ms)
constexpr int64_t ACTION_MS = 25000;
/// The count of the card is repainted at this pace (ms), as in Summa
constexpr int TICK_MS = 500;
/// The fade in and out (ms), the motion tokens of Summa
constexpr int FADE_IN_MS = 180;
constexpr int FADE_OUT_MS = 120;

/// Whole seconds left of the count, never below zero: the number the card shows ("25s"), as in Summa
constexpr int secondsLeft(int64_t remainingMs) { return remainingMs <= 0 ? 0 : static_cast<int>(remainingMs / 1000); }
}  // namespace toast

class Toast {
public:
    /// area: the widget of the pages; the notice is centered at the bottom of it, over the page and above the status
    /// bar
    Toast(GtkOverlay* overlay, GtkWidget* area);
    ~Toast();

    /// The strip: shows text and goes away by itself; busy adds a spinner and keeps it up until the next show or hide
    void show(const std::string& text, bool busy = false);
    /// The card with an action and the count: clicking the action closes the card and calls callback
    void showAction(const std::string& text, const std::string& actionLabel, std::function<void()> callback);
    /// The error card: stays until closed; the detail is not in the text, a button copies it to the clipboard
    void showError(const std::string& text, const std::string& detail);
    void hide();

private:
    enum class Kind { Strip, Action, Error };
    void present(Kind kind, const std::string& text);
    void startClock(int64_t durationMs, bool counting);
    void stopClock();
    void tick();
    static gboolean onChildPosition(GtkOverlay* overlay, GtkWidget* w, GdkRectangle* alloc, Toast* self);

    GtkOverlay* overlay = nullptr;
    GtkWidget* area = nullptr;
    gulong positionId = 0;
    GtkWidget* revealer = nullptr;
    GtkWidget* strip = nullptr;
    GtkWidget* stripLabel = nullptr;
    GtkWidget* spinner = nullptr;
    GtkWidget* card = nullptr;
    GtkWidget* cardLabel = nullptr;
    GtkWidget* actionButton = nullptr;
    GtkWidget* copyButton = nullptr;
    GtkWidget* count = nullptr;
    guint timer = 0;
    int64_t deadline = 0;  ///< monotonic, in microseconds
    bool counting = false;
    std::function<void()> onAction;
    std::string detail;
};

}  // namespace ambitus
