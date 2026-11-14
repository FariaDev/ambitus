/*
 * Ambitus
 *
 * When the palette of the Studio shows up in the Focus mode: it comes down when the pointer reaches the top of the
 * page, stays while the pointer is on the strip or on the palette (or a panel opened from it) and goes away 1.5 s after
 * the pointer leaves. This is the state machine alone, with the times; the palette feeds it the pointer and draws the
 * level.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <glib.h>

#include "AmbitusMotion.h"

namespace ambitus {

class FocusReveal {
public:
    /// The height of the strip at the top of the page that calls the palette, in pixels
    static constexpr int ZONE_HEIGHT = 48;
    /// How long the palette stays after the pointer leaves
    static constexpr gint64 GRACE_US = 1500 * 1000;

    /// Back to hidden, at once
    void reset() {
        phase = Phase::HIDDEN;
        shown = 0;
        leaveAt = -1;
        last = -1;
    }

    /// 0 (hidden) to 1 (all there): how far the entrance (BASE_US) or the exit (FAST_US) has gone, in time. The palette
    /// draws it through the ease-out
    double level() const { return shown; }
    /// Whether the level is moving
    bool animating() const { return phase == Phase::SHOWING || phase == Phase::HIDING; }
    bool hidden() const { return phase == Phase::HIDDEN; }

    /// `now` in microseconds, always growing. `hot`: the pointer is on the strip, on the palette, or a panel of the
    /// palette is open. `busy`: a button is pressed (a tool is drawing): it does not make the palette show, but it does
    /// not take it away either. `animate`: false when the system wants less movement, and then it only appears and
    /// disappears. Returns the level
    double update(gint64 now, bool hot, bool busy, bool animate) {
        const gint64 dt = last < 0 ? 0 : now - last;
        last = now;

        if (hot) {
            leaveAt = -1;
        } else if (leaveAt < 0) {
            leaveAt = now;
        }

        switch (phase) {
            case Phase::HIDDEN:
            case Phase::HIDING:
                if (hot && !busy) {
                    phase = Phase::SHOWING;
                }
                break;
            case Phase::SHOWING:
            case Phase::SHOWN:
                if (leaveAt >= 0 && now - leaveAt >= GRACE_US) {
                    phase = Phase::HIDING;
                }
                break;
        }

        if (phase == Phase::SHOWING) {
            shown = animate ? shown + static_cast<double>(dt) / motion::BASE_US : 1;
            if (shown >= 1) {
                shown = 1;
                phase = Phase::SHOWN;
            }
        } else if (phase == Phase::HIDING) {
            shown = animate ? shown - static_cast<double>(dt) / motion::FAST_US : 0;
            if (shown <= 0) {
                reset();
            }
        }
        return shown;
    }

private:
    enum class Phase { HIDDEN, SHOWING, SHOWN, HIDING };
    Phase phase = Phase::HIDDEN;
    double shown = 0;
    gint64 leaveAt = -1;  ///< When the pointer left, or -1 while it is there
    gint64 last = -1;
};

}  // namespace ambitus
