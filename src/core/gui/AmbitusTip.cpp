#include "AmbitusTip.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ambitus {

namespace {

constexpr guint SHOW_DELAY_MS = 500;    ///< The pointer has to stay on the button this long
constexpr gint64 WARM_US = 600 * 1000;  ///< A tip closed less than this ago makes the next one come at once
constexpr gint64 FADE_IN_US = 180 * 1000;
constexpr gint64 FADE_OUT_US = 120 * 1000;
constexpr int GAP = 8;  ///< Between the button and the tip

/// cubic-bezier(.22,.61,.36,1), the "ease-out" of the Summa for what appears or goes away
double easeOut(double x) {
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

/// The tip that is shown, or about to be: there is only one at a time, so one window and one state serve all buttons
struct Shared {
    GtkWidget* win = nullptr;
    GtkWidget* label = nullptr;
    GtkWidget* owner = nullptr;  ///< The button whose tip is shown, or waits for its delay
    guint timer = 0;
    guint tick = 0;
    bool shown = false;   ///< The window is on screen (it can be fading out)
    gint64 closedAt = 0;  ///< Monotonic time of the last tip that closed; 0: none yet
    // the running fade
    gint64 fadeStart = -1;
    gint64 fadeLen = 0;
    double fadeFrom = 0;
    double fadeTo = 0;
};

Shared& shared() {
    static Shared s;
    return s;
}

/// The system asked for less movement (GNOME: Accessibility, Reduce animation)
bool reducedMotion(GtkWidget* w) {
    gboolean animations = true;
    g_object_get(gtk_widget_get_settings(w), "gtk-enable-animations", &animations, nullptr);
    return !animations;
}

void stopFade() {
    Shared& s = shared();
    if (s.tick) {
        gtk_widget_remove_tick_callback(s.win, s.tick);
        s.tick = 0;
    }
}

void hideWindow() {
    Shared& s = shared();
    stopFade();
    if (s.win) {
        gtk_widget_hide(s.win);
        gtk_widget_set_opacity(s.win, 0);
    }
    s.shown = false;
}

void startFade(double to, gint64 len) {
    Shared& s = shared();
    stopFade();
    s.fadeFrom = gtk_widget_get_opacity(s.win);
    s.fadeTo = to;
    s.fadeLen = len;
    s.fadeStart = -1;
    s.tick = gtk_widget_add_tick_callback(
            s.win,
            +[](GtkWidget* w, GdkFrameClock* clock, gpointer) -> gboolean {
                Shared& s = shared();
                gint64 now = gdk_frame_clock_get_frame_time(clock);
                if (s.fadeStart < 0) {
                    s.fadeStart = now;
                }
                double t = std::clamp(static_cast<double>(now - s.fadeStart) / static_cast<double>(s.fadeLen), 0., 1.);
                gtk_widget_set_opacity(w, s.fadeFrom + (s.fadeTo - s.fadeFrom) * easeOut(t));
                if (t < 1) {
                    return G_SOURCE_CONTINUE;
                }
                s.tick = 0;
                if (s.fadeTo == 0) {
                    hideWindow();
                }
                return G_SOURCE_REMOVE;
            },
            nullptr, nullptr);
}

void cancelTimer() {
    Shared& s = shared();
    if (s.timer) {
        g_source_remove(s.timer);
        s.timer = 0;
    }
}

/// Takes the tip away: at once, or with the fade
void dismiss(bool animate) {
    Shared& s = shared();
    cancelTimer();
    s.owner = nullptr;
    if (!s.shown) {
        return;
    }
    s.closedAt = g_get_monotonic_time();
    if (animate && !reducedMotion(s.win)) {
        startFade(0, FADE_OUT_US);
    } else {
        hideWindow();
    }
}

void ensureWindow(GtkWidget* ref) {
    Shared& s = shared();
    if (s.win) {
        return;
    }
    // A popup, like the window of the tooltips of GTK: no decoration, no focus, above the canvas
    s.win = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_type_hint(GTK_WINDOW(s.win), GDK_WINDOW_TYPE_HINT_TOOLTIP);
    gtk_window_set_accept_focus(GTK_WINDOW(s.win), false);
    gtk_window_set_focus_on_map(GTK_WINDOW(s.win), false);
    gtk_window_set_resizable(GTK_WINDOW(s.win), false);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(s.win), true);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(s.win), true);
    gtk_widget_set_app_paintable(s.win, true);
    // the corners are only round where the screen can blend them
    GdkScreen* screen = gtk_widget_get_screen(ref);
    gtk_window_set_screen(GTK_WINDOW(s.win), screen);
    if (gdk_screen_is_composited(screen)) {
        if (GdkVisual* rgba = gdk_screen_get_rgba_visual(screen)) {
            gtk_widget_set_visual(s.win, rgba);
        }
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(s.win), "amb-tip");

    s.label = gtk_label_new(nullptr);
    gtk_label_set_use_markup(GTK_LABEL(s.label), true);
    gtk_container_add(GTK_CONTAINER(s.win), s.label);
    gtk_widget_show(s.label);
    // the pointer goes through the tip, to whatever lies under it
    g_signal_connect(s.win, "realize", G_CALLBACK(+[](GtkWidget* w, gpointer) {
                         gdk_window_set_pass_through(gtk_widget_get_window(w), true);
                     }),
                     nullptr);
}

/// Where the tip goes: centred above the button, GAP px from it; below it if the button is in the upper half of the
/// window or there is no room above; always inside the window of the button
void place(GtkWidget* button, GtkWidget* top) {
    Shared& s = shared();
    GtkRequisition size;
    gtk_widget_get_preferred_size(s.win, nullptr, &size);

    int bx = 0, by = 0;
    gtk_widget_translate_coordinates(button, top, 0, 0, &bx, &by);
    int ox = 0, oy = 0;
    gdk_window_get_origin(gtk_widget_get_window(top), &ox, &oy);
    int bw = gtk_widget_get_allocated_width(button);
    int bh = gtk_widget_get_allocated_height(button);
    int tw = gtk_widget_get_allocated_width(top);
    int th = gtk_widget_get_allocated_height(top);

    int x = ox + bx + (bw - size.width) / 2;
    int y = oy + by - GAP - size.height;
    // in the upper half of the window (the palette, at the top of the pages) the tip goes below: above, it would
    // cover the tabs
    if (y < oy || by + bh / 2 < th / 2) {
        y = oy + by + bh + GAP;
    }
    x = std::clamp(x, ox, std::max(ox, ox + tw - size.width));
    y = std::clamp(y, oy, std::max(oy, oy + th - size.height));
    gtk_window_move(GTK_WINDOW(s.win), x, y);
}

struct Tip {
    std::function<std::string()> markup;
};

void show(GtkWidget* button) {
    Shared& s = shared();
    s.timer = 0;
    auto* tip = static_cast<Tip*>(g_object_get_data(G_OBJECT(button), "amb-tip"));
    GtkWidget* top = gtk_widget_get_toplevel(button);
    if (!tip || !GTK_IS_WINDOW(top) || !gtk_widget_get_mapped(button)) {
        return;
    }
    std::string markup = tip->markup();
    if (markup.empty()) {
        return;
    }
    ensureWindow(button);
    gtk_label_set_markup(GTK_LABEL(s.label), markup.c_str());
    // The window of the tip is not the main window, so the CSS cannot tell the theme by itself: the class says it
    GtkStyleContext* ctx = gtk_widget_get_style_context(s.win);
    if (gtk_style_context_has_class(gtk_widget_get_style_context(top), "darkMode")) {
        gtk_style_context_add_class(ctx, "amb-tip-dark");
    } else {
        gtk_style_context_remove_class(ctx, "amb-tip-dark");
    }
    gtk_window_set_transient_for(GTK_WINDOW(s.win), GTK_WINDOW(top));
    place(button, top);

    bool animate = !reducedMotion(button);
    if (!s.shown) {
        gtk_widget_set_opacity(s.win, animate ? 0 : 1);
        gtk_widget_show(s.win);
        s.shown = true;
    }
    if (animate) {
        startFade(1, FADE_IN_US);
    } else {
        stopFade();
        gtk_widget_set_opacity(s.win, 1);
    }
}

void onEnter(GtkWidget* button) {
    Shared& s = shared();
    cancelTimer();
    s.owner = button;
    bool warm = s.shown || (s.closedAt != 0 && g_get_monotonic_time() - s.closedAt < WARM_US);
    if (warm) {
        show(button);
    } else {
        s.timer = g_timeout_add(
                SHOW_DELAY_MS,
                +[](gpointer p) -> gboolean {
                    show(static_cast<GtkWidget*>(p));
                    return G_SOURCE_REMOVE;
                },
                button);
    }
}

void onGone(GtkWidget* button, bool animate) {
    if (shared().owner == button) {
        dismiss(animate);
    }
}
}  // namespace

void setOverlayTip(GtkWidget* button, std::function<std::string()> markup) {
    if (auto* tip = static_cast<Tip*>(g_object_get_data(G_OBJECT(button), "amb-tip"))) {
        tip->markup = std::move(markup);
        return;
    }
    g_object_set_data_full(
            G_OBJECT(button), "amb-tip", new Tip{std::move(markup)}, +[](gpointer p) { delete static_cast<Tip*>(p); });
    gtk_widget_add_events(button, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK |
                                          GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    // Moving onto a child window of the button (a drawing area inside it) is not leaving the button
    g_signal_connect(button, "enter-notify-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventCrossing* e, gpointer) -> gboolean {
                         if (e->detail != GDK_NOTIFY_INFERIOR && e->mode == GDK_CROSSING_NORMAL) {
                             onEnter(w);
                         }
                         return GDK_EVENT_PROPAGATE;
                     }),
                     nullptr);
    g_signal_connect(button, "leave-notify-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventCrossing* e, gpointer) -> gboolean {
                         if (e->detail != GDK_NOTIFY_INFERIOR) {
                             onGone(w, true);
                         }
                         return GDK_EVENT_PROPAGATE;
                     }),
                     nullptr);
    g_signal_connect(button, "button-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventButton*, gpointer) -> gboolean {
                         onGone(w, true);
                         return GDK_EVENT_PROPAGATE;
                     }),
                     nullptr);
    g_signal_connect(button, "scroll-event", G_CALLBACK(+[](GtkWidget* w, GdkEventScroll*, gpointer) -> gboolean {
                         onGone(w, true);
                         return GDK_EVENT_PROPAGATE;
                     }),
                     nullptr);
    g_signal_connect(button, "unmap", G_CALLBACK(+[](GtkWidget* w, gpointer) { onGone(w, false); }), nullptr);
    g_signal_connect(button, "destroy", G_CALLBACK(+[](GtkWidget* w, gpointer) { onGone(w, false); }), nullptr);
}

void setOverlayTipText(GtkWidget* button, const char* text) {
    std::string t = text ? text : "";
    setOverlayTip(button, [t]() {
        gchar* esc = g_markup_escape_text(t.c_str(), -1);
        std::string out = esc;
        g_free(esc);
        return out;
    });
}

}  // namespace ambitus
