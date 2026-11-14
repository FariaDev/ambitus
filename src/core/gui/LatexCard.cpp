/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "LatexCard.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <poppler.h>

#include "ambitus/LatexError.h"
#include "control/LatexController.h"
#include "control/settings/LatexSettings.h"
#include "gui/AmbitusMotion.h"
#include "model/TexImage.h"
#include "util/Color.h"
#include "util/i18n.h"

namespace ambitus {

namespace {
constexpr int CARD_WIDTH = 520;         // the .dlg of the prototype
constexpr int PREVIEW_MIN_HEIGHT = 84;  // the .tex of the prototype
constexpr int PREVIEW_MAX_HEIGHT = 220;
constexpr int PREVIEW_RADIUS = 4;

void addClass(GtkWidget* w, const char* cls) { gtk_style_context_add_class(gtk_widget_get_style_context(w), cls); }

GtkWidget* label(const char* text, const char* cls) {
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    addClass(l, cls);
    return l;
}

void clickOf(GtkWidget* w, std::function<void()> f) {
    auto* fn = new std::function<void()>(std::move(f));
    g_signal_connect_data(
            w, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) { (*static_cast<std::function<void()>*>(d))(); }), fn,
            [](gpointer d, GClosure*) { delete static_cast<std::function<void()>*>(d); }, GConnectFlags(0));
}

bool animationsOn(GtkWidget* w) { return !motion::reducedMotion(w); }
int fadeIn() { return static_cast<int>(motion::BASE_US / 1000); }
int fadeOut() { return static_cast<int>(motion::FAST_US / 1000); }

void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}
}  // namespace

void LatexCard::open(GtkOverlay* overlay, GtkWindow* window, std::unique_ptr<LatexController> ctrl) {
    // owns itself: dismantle() deletes it after the fade out
    auto* card = new LatexCard(overlay, window, std::move(ctrl));
    card->show(window);
}

LatexCard::LatexCard(GtkOverlay* overlay, GtkWindow* window, std::unique_ptr<LatexController> ctrl):
        AbstractLatexDialog(std::move(ctrl)), previewHeight(PREVIEW_MIN_HEIGHT) {
    const bool editing = !texCtrl->initialTex.empty();

    // the veil covers the window and takes the clicks: a formula half typed is not lost by a slip of the mouse
    revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    GtkWidget* clicks = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(clicks), false);
    g_signal_connect(clicks, "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer) -> gboolean { return TRUE; }), nullptr);
    GtkWidget* veil = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(veil, "amb-cmd-veil");
    gtk_container_add(GTK_CONTAINER(clicks), veil);
    gtk_container_add(GTK_CONTAINER(revealer), clicks);

    card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(card, "amb-cmd");
    addClass(card, "amb-dlg");
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(card, true);
    gtk_widget_set_size_request(card, CARD_WIDTH, -1);
    gtk_container_add(GTK_CONTAINER(veil), card);

    gtk_box_pack_start(GTK_BOX(card), label(_("Formula"), "amb-dlg-title"), false, false, 0);

    // the preview: what LaTeX made of the formula, on the paper colour that the ink needs
    previewDrawingArea = GTK_DRAWING_AREA(gtk_drawing_area_new());
    GtkWidget* area = GTK_WIDGET(previewDrawingArea);
    addClass(area, "amb-tex-prev");
    gtk_widget_set_size_request(area, -1, previewHeight);
    g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer d) -> gboolean {
                         auto* self = static_cast<LatexCard*>(d);
                         const int w = gtk_widget_get_allocated_width(a);
                         const int h = gtk_widget_get_allocated_height(a);
                         cairo_save(cr);
                         roundedRect(cr, 0, 0, w, h, PREVIEW_RADIUS);
                         cairo_clip(cr);
                         Util::cairo_set_source_rgbi(cr, self->previewBackgroundColor);
                         cairo_paint(cr);
                         previewDrawFunc(self->previewDrawingArea, cr, w, h, self);
                         cairo_restore(cr);
                         gtk_render_frame(gtk_widget_get_style_context(a), cr, 0, 0, w, h);
                         return FALSE;
                     }),
                     this);
    gtk_box_pack_start(GTK_BOX(card), area, false, false, 0);
    gtk_widget_set_margin_bottom(area, 10);

    // the formula
    scroll = gtk_scrolled_window_new(nullptr, nullptr);
    addClass(scroll, "amb-tex-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), true);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 64);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 168);
    view = gtk_text_view_new();
    addClass(view, "amb-tex-field");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),
                                texCtrl->settings.editorWordWrap ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(view), false);  // Tab leaves the field, as in the other fields
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 8);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    const std::string& initial = editing ? texCtrl->initialTex : texCtrl->settings.defaultText;
    gtk_text_buffer_set_text(buffer, initial.c_str(), -1);
    if (!editing) {
        // the default text is selected: typing replaces it
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gtk_text_buffer_select_range(buffer, &start, &end);
    }
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(card), scroll, false, false, 0);
    // the line of the field is only on the focus, as the other fields of the app
    g_signal_connect(view, "focus-in-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer d) -> gboolean {
                         addClass(static_cast<LatexCard*>(d)->scroll, "focused");
                         return FALSE;
                     }),
                     this);
    g_signal_connect(view, "focus-out-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer d) -> gboolean {
                         gtk_style_context_remove_class(
                                 gtk_widget_get_style_context(static_cast<LatexCard*>(d)->scroll), "focused");
                         return FALSE;
                     }),
                     this);

    // the error of LaTeX, in one line
    problem = label("", "amb-dlg-problem");
    gtk_label_set_ellipsize(GTK_LABEL(problem), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(problem), true);
    texErrorLabel = GTK_LABEL(problem);
    gtk_box_pack_start(GTK_BOX(card), problem, false, false, 0);

    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addClass(foot, "amb-dlg-foot");
    btOk = GTK_BUTTON(gtk_button_new_with_label(editing ? _("OK") : _("Insert")));
    addClass(GTK_WIDGET(btOk), "amb-dlg-primary");
    gtk_widget_set_sensitive(GTK_WIDGET(btOk), texCtrl->isValidTex);
    clickOf(GTK_WIDGET(btOk), [this] { insert(); });
    gtk_box_pack_end(GTK_BOX(foot), GTK_WIDGET(btOk), false, false, 0);  // the right end: Cancel is on its left
    cancelButton = gtk_button_new_with_label(_("Cancel"));
    btCancel = GTK_BUTTON(cancelButton);
    addClass(cancelButton, "amb-dlg-ghost");
    clickOf(cancelButton, [this] { cancel(); });
    gtk_box_pack_end(GTK_BOX(foot), cancelButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(card), foot, false, false, 0);

    // a preview that was already there (the formula of the page) is shown at once
    if (texCtrl->temporaryRender) {
        setTempRender(texCtrl->temporaryRender->getPdf());
        growPreviewFor(previewPdfPage.get());
    }

    // once faded out, the card is taken away
    g_signal_connect_swapped(revealer, "notify::child-revealed", G_CALLBACK(+[](LatexCard* self) {
                                 auto* r = GTK_REVEALER(self->revealer);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     self->scheduleDismantle();
                                 }
                             }),
                             this);
    g_signal_connect_swapped(revealer, "destroy", G_CALLBACK(+[](LatexCard* self) { self->onRevealerDestroyed(); }),
                             this);
    gtk_widget_show_all(revealer);
    gtk_overlay_add_overlay(overlay, revealer);
}

LatexCard::~LatexCard() {
    if (dismantleSource) {
        g_source_remove(dismantleSource);
    }
    if (focusBack) {
        g_object_remove_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }
}

void LatexCard::show(GtkWindow* window) {
    focusBack = gtk_window_get_focus(window);
    if (focusBack) {
        g_object_add_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }
    // The window hands the keys to the widget that has the focus before anything else (MainWindow.cpp), so the keys of
    // the card are read on its own widgets: Ctrl+Enter, which in the accelerators of the app is "new page", never
    // reaches them
    for (GtkWidget* w: {view, GTK_WIDGET(btOk), cancelButton}) {
        g_signal_connect(w, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* e, LatexCard* self) -> gboolean {
                             return self->onKey(e);
                         }),
                         this);
    }
    // the preview follows what is typed: the controller compiles and answers with the picture
    g_signal_connect(buffer, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer ctrl) {
                         LatexController::handleTexChanged(static_cast<LatexController*>(ctrl));
                     }),
                     texCtrl.get());

    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? fadeIn() : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
    gtk_widget_realize(view);  // the revealer realizes its child late, and the focus change needs it realized
    gtk_widget_grab_focus(view);

    if (!texCtrl->temporaryRender) {
        // the first compile, unless the formula of the page already has its picture
        LatexController::handleTexChanged(texCtrl.get());
    }
}

bool LatexCard::isOpen() const {
    return revealer && GTK_IS_REVEALER(revealer) && gtk_revealer_get_reveal_child(GTK_REVEALER(revealer));
}

bool LatexCard::onKey(GdkEventKey* e) {
    if (!isOpen()) {
        return false;
    }
    const bool enter = e->keyval == GDK_KEY_Return || e->keyval == GDK_KEY_KP_Enter;
    if (e->keyval == GDK_KEY_Escape) {
        cancel();
        return true;
    }
    if (enter && (e->state & GDK_CONTROL_MASK)) {
        insert();
        return true;
    }
    return false;  // Enter alone: a new line in the field, and "press" on the buttons
}

std::string LatexCard::getBufferContents() {
    if (!buffer) {
        return {};
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char* text = gtk_text_buffer_get_text(buffer, &start, &end, false);
    std::string out = text ? text : "";
    g_free(text);
    return out;
}

void LatexCard::setProblem(const std::string& text) { gtk_label_set_text(GTK_LABEL(problem), text.c_str()); }

void LatexCard::setCompilationStatus(bool isTexValid, bool isCompilationDone, const std::string& output) {
    if (!isCompilationDone) {
        // the line stays as it was until the new answer; the button waits, so the formula that goes in is the one
        // that was compiled
        gtk_widget_set_sensitive(GTK_WIDGET(btOk), false);
        return;
    }
    // the answer is for what was compiled; if the field changed meanwhile, another compile starts right after
    const bool fresh = getBufferContents() == texCtrl->lastPreviewedTex;
    gtk_widget_set_sensitive(GTK_WIDGET(btOk), isTexValid && fresh);
    if (isTexValid) {
        setProblem("");
        if (previewPdfPage) {
            growPreviewFor(previewPdfPage.get());
        }
        if (fresh && insertWhenReady) {
            insertWhenReady = false;
            insert();
        }
    } else {
        insertWhenReady = false;
        const std::string line = latex::firstErrorLine(output);
        setProblem(line.empty() ? std::string(_("LaTeX could not compile this formula.")) : line);
    }
}

void LatexCard::growPreviewFor(PopplerPage* page) {
    if (!page) {
        return;
    }
    // room for a tall formula (a matrix), and it does not shrink back while the card is open: the card would jump
    double w = 0, h = 0;
    poppler_page_get_size(page, &w, &h);
    if (w <= 0 || h <= 0) {
        return;
    }
    const double zoom = std::min(2.0, (CARD_WIDTH - 40.0) / w);
    const int wanted = std::clamp(static_cast<int>(std::ceil(h * zoom)) + 24, PREVIEW_MIN_HEIGHT, PREVIEW_MAX_HEIGHT);
    if (wanted > previewHeight) {
        previewHeight = wanted;
        gtk_widget_set_size_request(GTK_WIDGET(previewDrawingArea), -1, previewHeight);
    }
}

void LatexCard::insert() {
    if (!isOpen()) {
        return;
    }
    if (!gtk_widget_get_sensitive(GTK_WIDGET(btOk))) {
        // still compiling, or not valid: when the compile ends and the formula is good, it goes in
        insertWhenReady = texCtrl->isUpdating();
        return;
    }
    texCtrl->insertTexImage();
    close(true);
}

void LatexCard::cancel() {
    if (!isOpen()) {
        return;
    }
    texCtrl->cancelEditing();
    close(true);
}

void LatexCard::close(bool restoreFocus) {
    insertWhenReady = false;
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? fadeOut() : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), false);
    // without a transition, child-revealed does not change after the revealer is off
    if (!animationsOn(revealer)) {
        gtk_widget_hide(revealer);
        scheduleDismantle();
    }
    if (restoreFocus && focusBack && gtk_widget_get_mapped(focusBack)) {
        gtk_widget_grab_focus(focusBack);
    }
}

void LatexCard::scheduleDismantle() {
    if (dismantleSource) {
        return;
    }
    dismantleSource = g_idle_add(
            +[](gpointer d) -> gboolean {
                auto* self = static_cast<LatexCard*>(d);
                self->dismantleSource = 0;
                self->dismantle();
                return G_SOURCE_REMOVE;
            },
            this);
}

void LatexCard::dismantle() {
    dismantling = true;
    if (revealer) {
        gtk_widget_destroy(revealer);  // onRevealerDestroyed runs, without deleting
    }
    delete this;
}

void LatexCard::onRevealerDestroyed() {
    // the widgets are still alive here. The buffer can outlive the card (the clipboard holds a reference after a
    // copy): it must not call the controller of a card that is gone
    if (buffer) {
        g_signal_handlers_disconnect_by_data(buffer, texCtrl.get());
        buffer = nullptr;
    }
    revealer = nullptr;
    previewDrawingArea = nullptr;
    if (!dismantling) {
        delete this;  // the window itself is going away
    }
}

}  // namespace ambitus
