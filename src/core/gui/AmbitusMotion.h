/*
 * Ambitus
 *
 * The motion tokens of the Summa (docs/design/r6): the durations, the ease-out curve and the zoom of what appears.
 * The pieces that are drawn by hand (the radial menu, the hold-to-straighten fade, the tips) take them from here, so
 * they all move alike and all obey gtk-enable-animations.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <glib.h>
#include <gtk/gtk.h>

namespace ambitus::motion {

constexpr gint64 FAST_US = 120 * 1000;  ///< Fast, and every exit
constexpr gint64 BASE_US = 180 * 1000;  ///< Entrances
constexpr gint64 SLOW_US = 320 * 1000;
constexpr double ZOOM_IN = .985;  ///< What grows in starts at this scale

/// cubic-bezier(.22,.61,.36,1), the "ease-out" of the Summa for what appears or goes away
inline double easeOut(double x) {
    constexpr double x1 = .22, y1 = .61, x2 = .36, y2 = 1.;
    auto bez = [](double t, double a, double b) {
        return 3 * a * t * (1 - t) * (1 - t) + 3 * b * t * t * (1 - t) + t * t * t;
    };
    double lo = 0, hi = 1;
    for (int i = 0; i < 30; i++) {
        double mid = (lo + hi) / 2;
        (bez(mid, x1, x2) < x ? lo : hi) = mid;
    }
    return bez((lo + hi) / 2, y1, y2);
}

/// The system asked for less movement (GNOME: Accessibility, Reduce animation): nothing animates
inline bool reducedMotion(GtkWidget* w) {
    gboolean animations = true;
    g_object_get(gtk_widget_get_settings(w), "gtk-enable-animations", &animations, nullptr);
    return !animations;
}

}  // namespace ambitus::motion
