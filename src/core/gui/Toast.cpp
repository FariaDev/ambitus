#include "Toast.h"

#include <algorithm>
#include <utility>

#include "util/i18n.h"

namespace ambitus {

namespace {
/// The shadow of the notices is wider than what the revealer would show: the frame keeps this margin for it
constexpr int SHADOW = 32;

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

bool animationsOn(GtkWidget* w) {
    gboolean on = true;
    g_object_get(gtk_widget_get_settings(w), "gtk-enable-animations", &on, nullptr);
    return on;
}

GtkWidget* wrappedLabel(const char* cssClass) {
    GtkWidget* l = gtk_label_new(nullptr);
    addClass(l, cssClass);
    gtk_label_set_line_wrap(GTK_LABEL(l), true);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 60);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}

/// A button of the card; it does not take the keyboard focus on click, so the page keeps it
GtkWidget* cardButton(const char* cssClass) {
    GtkWidget* b = gtk_button_new();
    addClass(b, cssClass);
    gtk_widget_set_focus_on_click(b, false);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    return b;
}
}  // namespace

Toast::Toast(GtkOverlay* overlay, GtkWidget* area): overlay(overlay), area(area) {
    revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);

    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(frame, SHADOW);
    gtk_widget_set_margin_end(frame, SHADOW);
    gtk_widget_set_margin_top(frame, SHADOW);
    gtk_widget_set_margin_bottom(frame, SHADOW);
    gtk_container_add(GTK_CONTAINER(revealer), frame);

    // the strip
    strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    addClass(strip, "amb-toast");
    spinner = gtk_spinner_new();
    stripLabel = wrappedLabel("amb-toast-text");
    gtk_container_add(GTK_CONTAINER(strip), spinner);
    gtk_container_add(GTK_CONTAINER(strip), stripLabel);
    gtk_container_add(GTK_CONTAINER(frame), strip);

    // the card, with an action or with an error
    card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    addClass(card, "amb-notice");
    cardLabel = wrappedLabel("amb-notice-text");
    gtk_container_add(GTK_CONTAINER(card), cardLabel);

    GtkWidget* acts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    actionButton = cardButton("amb-notice-btn");
    g_signal_connect_swapped(actionButton, "clicked", G_CALLBACK(+[](Toast* self) {
                                 // closed first: the action may show a notice of its own
                                 auto callback = std::exchange(self->onAction, nullptr);
                                 self->hide();
                                 if (callback) {
                                     callback();
                                 }
                             }),
                             this);
    count = gtk_label_new(nullptr);
    addClass(count, "amb-notice-count");
    copyButton = cardButton("amb-notice-ghost");
    gtk_button_set_label(GTK_BUTTON(copyButton), _("Copy the detail"));
    g_signal_connect_swapped(copyButton, "clicked", G_CALLBACK(+[](Toast* self) {
                                 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                                                        self->detail.c_str(), -1);
                                 gtk_button_set_label(GTK_BUTTON(self->copyButton), _("Copied"));
                             }),
                             this);
    GtkWidget* close = cardButton("amb-notice-ghost");
    gtk_button_set_image(GTK_BUTTON(close), gtk_image_new_from_icon_name("xopp-x", GTK_ICON_SIZE_BUTTON));
    gtk_image_set_pixel_size(GTK_IMAGE(gtk_button_get_image(GTK_BUTTON(close))), 14);
    gtk_widget_set_tooltip_text(close, _("Close"));
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(+[](Toast* self) { self->hide(); }), this);
    gtk_box_pack_start(GTK_BOX(acts), actionButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(acts), copyButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(acts), count, false, false, 0);
    gtk_box_pack_end(GTK_BOX(acts), close, false, false, 0);
    gtk_container_add(GTK_CONTAINER(card), acts);
    gtk_container_add(GTK_CONTAINER(frame), card);

    // once faded out, the revealer goes away, so it takes no clicks from the page
    g_signal_connect_swapped(revealer, "notify::child-revealed", G_CALLBACK(+[](Toast* self) {
                                 auto* r = GTK_REVEALER(self->revealer);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     gtk_widget_hide(self->revealer);
                                 }
                             }),
                             this);

    gtk_widget_show_all(revealer);
    for (GtkWidget* w: {strip, card, spinner, actionButton, copyButton, count}) {
        gtk_widget_set_no_show_all(w, true);
    }
    gtk_widget_hide(strip);
    gtk_widget_hide(card);
    gtk_widget_set_no_show_all(revealer, true);
    gtk_widget_hide(revealer);

    gtk_overlay_add_overlay(overlay, revealer);
    positionId = g_signal_connect(overlay, "get-child-position", G_CALLBACK(onChildPosition), this);
}

Toast::~Toast() {
    stopClock();
    g_signal_handler_disconnect(overlay, positionId);
}

gboolean Toast::onChildPosition(GtkOverlay* overlay, GtkWidget* w, GdkRectangle* alloc, Toast* self) {
    if (w != self->revealer || !self->area) {
        return false;
    }
    int x = 0, y = 0;
    const int areaW = gtk_widget_get_allocated_width(self->area);
    const int areaH = gtk_widget_get_allocated_height(self->area);
    if (areaW <= 0 || !gtk_widget_translate_coordinates(self->area, GTK_WIDGET(overlay), 0, 0, &x, &y)) {
        return false;
    }
    GtkRequisition natural;
    gtk_widget_get_preferred_size(w, nullptr, &natural);
    alloc->width = std::min(natural.width, areaW);
    alloc->height = natural.height;
    alloc->x = x + (areaW - alloc->width) / 2;
    alloc->y = y + areaH - alloc->height;
    return true;
}

void Toast::present(Kind kind, const std::string& text) {
    stopClock();
    onAction = nullptr;
    gtk_spinner_stop(GTK_SPINNER(spinner));
    const bool isStrip = kind == Kind::Strip;
    gtk_widget_set_visible(strip, isStrip);
    gtk_widget_set_visible(card, !isStrip);
    if (isStrip) {
        gtk_label_set_text(GTK_LABEL(stripLabel), text.c_str());
    } else {
        gtk_label_set_text(GTK_LABEL(cardLabel), text.c_str());
        auto* ctx = gtk_widget_get_style_context(card);
        if (kind == Kind::Error) {
            gtk_style_context_add_class(ctx, "amb-notice-error");
        } else {
            gtk_style_context_remove_class(ctx, "amb-notice-error");
        }
        gtk_widget_set_visible(actionButton, kind == Kind::Action);
        gtk_widget_set_visible(count, kind == Kind::Action);
        gtk_widget_set_visible(copyButton, kind == Kind::Error);
    }
    if (!gtk_widget_get_visible(revealer)) {
        gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? toast::FADE_IN_MS : 0);
        gtk_widget_show(revealer);
    }
    gtk_widget_queue_resize(revealer);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
}

void Toast::show(const std::string& text, bool busy) {
    present(Kind::Strip, text);
    gtk_widget_set_visible(spinner, busy);
    if (busy) {
        gtk_spinner_start(GTK_SPINNER(spinner));
    } else {
        startClock(toast::STRIP_MS, false);
    }
}

void Toast::showAction(const std::string& text, const std::string& actionLabel, std::function<void()> callback) {
    present(Kind::Action, text);
    gtk_button_set_label(GTK_BUTTON(actionButton), actionLabel.c_str());
    onAction = std::move(callback);
    startClock(toast::ACTION_MS, true);
}

void Toast::showError(const std::string& text, const std::string& detailText) {
    present(Kind::Error, text);
    detail = detailText;
    gtk_button_set_label(GTK_BUTTON(copyButton), _("Copy the detail"));
    gtk_widget_set_visible(copyButton, !detail.empty());
}

void Toast::hide() {
    stopClock();
    onAction = nullptr;
    gtk_spinner_stop(GTK_SPINNER(spinner));
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? toast::FADE_OUT_MS : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), false);
    // without a transition, child-revealed does not change after the revealer is off: hide it here
    if (!animationsOn(revealer)) {
        gtk_widget_hide(revealer);
    }
}

void Toast::startClock(int64_t durationMs, bool withCount) {
    counting = withCount;
    deadline = g_get_monotonic_time() + durationMs * 1000;
    if (counting) {
        tick();  // paints the first number at once
    }
    timer = g_timeout_add(
            counting ? toast::TICK_MS : static_cast<guint>(durationMs),
            +[](gpointer d) -> gboolean {
                auto* self = static_cast<Toast*>(d);
                if (self->counting && g_get_monotonic_time() < self->deadline) {
                    self->tick();
                    return G_SOURCE_CONTINUE;
                }
                self->timer = 0;
                self->hide();
                return G_SOURCE_REMOVE;
            },
            this);
}

void Toast::stopClock() {
    if (timer) {
        g_source_remove(timer);
        timer = 0;
    }
}

void Toast::tick() {
    const int s = toast::secondsLeft((deadline - g_get_monotonic_time()) / 1000);
    const std::string text = std::to_string(s) + "s";
    gtk_label_set_text(GTK_LABEL(count), text.c_str());
}

}  // namespace ambitus
