/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "KeepDraftCard.h"

#include <algorithm>
#include <system_error>

#include "gui/AmbitusMotion.h"
#include "util/i18n.h"

#include "NewNotebookDialog.h"

namespace ambitus {

using namespace newnotebook;

namespace {
constexpr int CARD_WIDTH = 460;

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

/// The square of the colour of the subject, as the library draws it beside a course
GtkWidget* dotFor(const std::string& color) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 8, 8);
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    g_object_set_data_full(G_OBJECT(area), "amb-color", g_strdup(color.c_str()), g_free);
    g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer) -> gboolean {
                         GdkRGBA c;
                         auto* hex = static_cast<const char*>(g_object_get_data(G_OBJECT(a), "amb-color"));
                         if (!hex || !*hex || !gdk_rgba_parse(&c, hex)) {
                             gdk_rgba_parse(&c, "#8d8d8d");
                         }
                         const double w = gtk_widget_get_allocated_width(a);
                         const double h = gtk_widget_get_allocated_height(a);
                         const double r = 2;
                         cairo_new_sub_path(cr);
                         cairo_arc(cr, w - r, r, r, -G_PI / 2, 0);
                         cairo_arc(cr, w - r, h - r, r, 0, G_PI / 2);
                         cairo_arc(cr, r, h - r, r, G_PI / 2, G_PI);
                         cairo_arc(cr, r, r, r, G_PI, 3 * G_PI / 2);
                         cairo_close_path(cr);
                         gdk_cairo_set_source_rgba(cr, &c);
                         cairo_fill(cr);
                         return FALSE;
                     }),
                     nullptr);
    return area;
}

/// The last subject the user kept a draft in, so the card starts there next time
fs::path lastSubject;
}  // namespace

KeepDraftCard::KeepDraftCard(GtkOverlay* overlay, GtkWindow* window): window(window) {
    revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    GtkWidget* clicks = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(clicks), false);
    g_signal_connect(clicks, "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton*, KeepDraftCard* self) -> gboolean {
                         if (self->mode == Mode::CLOSE) {
                             self->close();
                         }
                         return TRUE;
                     }),
                     this);
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

    keepPage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(card), keepPage, false, false, 0);

    gtk_box_pack_start(GTK_BOX(keepPage), label(_("Keep draft"), "amb-dlg-title"), false, false, 0);
    GtkWidget* note = label(_("It becomes a normal notebook, which Summa reads too."), "amb-dlg-problem");
    addClass(note, "amb-dlg-note");
    gtk_label_set_line_wrap(GTK_LABEL(note), true);
    gtk_box_pack_start(GTK_BOX(keepPage), note, false, false, 0);

    gtk_box_pack_start(GTK_BOX(keepPage), label(_("Name"), "amb-opt-label"), false, false, 0);
    entry = gtk_entry_new();
    addClass(entry, "amb-cmd-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Name"));
    gtk_entry_set_activates_default(GTK_ENTRY(entry), false);
    gtk_box_pack_start(GTK_BOX(keepPage), entry, false, false, 0);
    problem = label("", "amb-dlg-problem");
    gtk_label_set_ellipsize(GTK_LABEL(problem), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(keepPage), problem, false, false, 0);

    gtk_box_pack_start(GTK_BOX(keepPage), label(_("Subject"), "amb-opt-label"), false, false, 0);
    subjectsBox = gtk_flow_box_new();
    addClass(subjectsBox, "amb-dlg-chips");
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(subjectsBox), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(subjectsBox), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(subjectsBox), 6);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(subjectsBox), 3);
    gtk_box_pack_start(GTK_BOX(keepPage), subjectsBox, false, false, 0);

    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addClass(foot, "amb-dlg-foot");
    keepButton = gtk_button_new_with_label(_("Keep"));
    addClass(keepButton, "amb-dlg-primary");
    clickOf(keepButton, [this] { keep(); });
    gtk_box_pack_end(GTK_BOX(foot), keepButton, false, false, 0);
    cancelButton = gtk_button_new_with_label(_("Cancel"));
    addClass(cancelButton, "amb-dlg-ghost");
    clickOf(cancelButton, [this] { close(); });
    gtk_box_pack_end(GTK_BOX(foot), cancelButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(keepPage), foot, false, false, 0);

    closePage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(card), closePage, false, false, 0);
    gtk_box_pack_start(GTK_BOX(closePage), label(_("Keep this draft?"), "amb-dlg-title"), false, false, 0);
    GtkWidget* saved = label(_("It is saved in Drafts until you decide."), "amb-dlg-problem");
    addClass(saved, "amb-dlg-note");
    gtk_label_set_line_wrap(GTK_LABEL(saved), true);
    gtk_box_pack_start(GTK_BOX(closePage), saved, false, false, 0);

    GtkWidget* closeFoot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addClass(closeFoot, "amb-dlg-foot");
    closeNotebook = gtk_button_new_with_label(_("Keep as notebook…"));
    addClass(closeNotebook, "amb-dlg-primary");
    clickOf(closeNotebook, [this] { chooseClose(CloseChoice::KEEP_AS_NOTEBOOK); });
    gtk_box_pack_end(GTK_BOX(closeFoot), closeNotebook, false, false, 0);
    closeDrafts = gtk_button_new_with_label(_("Keep in drafts"));
    addClass(closeDrafts, "amb-dlg-secondary");
    clickOf(closeDrafts, [this] { chooseClose(CloseChoice::KEEP_IN_DRAFTS); });
    gtk_box_pack_end(GTK_BOX(closeFoot), closeDrafts, false, false, 0);
    closeDiscard = gtk_button_new_with_label(_("Discard"));
    addClass(closeDiscard, "amb-dlg-ghost");
    clickOf(closeDiscard, [this] { chooseClose(CloseChoice::DISCARD); });
    gtk_box_pack_end(GTK_BOX(closeFoot), closeDiscard, false, false, 0);
    gtk_box_pack_start(GTK_BOX(closePage), closeFoot, false, false, 0);
    gtk_widget_set_no_show_all(closePage, true);

    g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable*, KeepDraftCard* self) { self->refresh(); }), this);
    watchKeys(entry);
    watchKeys(cancelButton);
    watchKeys(keepButton);
    watchKeys(closeDiscard);
    watchKeys(closeDrafts);
    watchKeys(closeNotebook);

    g_signal_connect_swapped(revealer, "notify::child-revealed", G_CALLBACK(+[](KeepDraftCard* self) {
                                 auto* r = GTK_REVEALER(self->revealer);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     gtk_widget_hide(self->revealer);
                                 }
                             }),
                             this);

    gtk_widget_show_all(revealer);
    gtk_widget_set_no_show_all(revealer, true);
    gtk_widget_hide(revealer);
    gtk_overlay_add_overlay(overlay, revealer);
}

KeepDraftCard::~KeepDraftCard() {
    if (focusBack) {
        g_object_remove_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }
}

bool KeepDraftCard::isOpen() const {
    return revealer && GTK_IS_REVEALER(revealer) && gtk_revealer_get_reveal_child(GTK_REVEALER(revealer));
}

void KeepDraftCard::watchKeys(GtkWidget* w) {
    g_signal_connect(w, "key-press-event", G_CALLBACK(+[](GtkWidget* from, GdkEventKey* e, KeepDraftCard* self) {
                         return self->onKey(from, e);
                     }),
                     this);
}

bool KeepDraftCard::onKey(GtkWidget* from, GdkEventKey* e) {
    switch (e->keyval) {
        case GDK_KEY_Escape:
            close();
            return true;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (mode == Mode::CLOSE || from == cancelButton || from == keepButton) {
                return false;
            }
            keep();
            return true;
        default:
            return false;
    }
}

std::string KeepDraftCard::typedName() const { return gtk_entry_get_text(GTK_ENTRY(entry)); }

fs::path KeepDraftCard::folderOfSubject() const {
    if (subjects.empty()) {
        return {};
    }
    return subjects[subject].path;
}

void KeepDraftCard::open(std::vector<library::Subject> list, std::function<void(const Request&)> callback,
                         std::function<void()> cancel) {
    mode = Mode::KEEP;
    subjects = std::move(list);
    onKeep = std::move(callback);
    onCloseChoice = nullptr;
    onCancel = std::move(cancel);
    subject = 0;
    if (!subjects.empty() && !lastSubject.empty()) {
        auto it = std::find_if(subjects.begin(), subjects.end(),
                               [](const library::Subject& s) { return s.path == lastSubject; });
        if (it != subjects.end()) {
            subject = static_cast<size_t>(std::distance(subjects.begin(), it));
        }
    }
    fillSubjects();
    chooseSubject(subject);
    gtk_entry_set_text(GTK_ENTRY(entry), "");
    refresh();
    gtk_widget_hide(closePage);
    gtk_widget_show_all(keepPage);
    reveal(entry);
}

void KeepDraftCard::openClose(std::function<void(CloseChoice)> onChoice, std::function<void()> cancel) {
    mode = Mode::CLOSE;
    onKeep = nullptr;
    onCloseChoice = std::move(onChoice);
    onCancel = std::move(cancel);
    gtk_widget_hide(keepPage);
    gtk_widget_set_no_show_all(closePage, false);
    gtk_widget_show_all(closePage);
    reveal(closeDrafts);
}

void KeepDraftCard::reveal(GtkWidget* focus) {
    if (focusBack) {
        g_object_remove_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
        focusBack = nullptr;
    }
    focusBack = gtk_window_get_focus(window);
    if (focusBack) {
        g_object_add_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }

    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? FADE_IN_MS : 0);
    gtk_widget_show(revealer);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
    gtk_widget_realize(focus);
    gtk_widget_grab_focus(focus);
}

void KeepDraftCard::chooseClose(CloseChoice choice) {
    if (!isOpen() || mode != Mode::CLOSE) {
        return;
    }
    auto done = std::move(onCloseChoice);
    onCloseChoice = nullptr;
    onKeep = nullptr;
    onCancel = nullptr;
    close(false);
    if (done) {
        done(choice);
    }
}

void KeepDraftCard::close() {
    close(true);
    auto cancel = std::move(onCancel);
    onCancel = nullptr;
    onKeep = nullptr;
    onCloseChoice = nullptr;
    if (cancel) {
        cancel();
    }
}

void KeepDraftCard::close(bool restoreFocus) {
    if (!isOpen()) {
        return;
    }
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? FADE_OUT_MS : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), false);
    if (!animationsOn(revealer)) {
        gtk_widget_hide(revealer);
    }
    if (restoreFocus && focusBack && gtk_widget_get_mapped(focusBack)) {
        gtk_widget_grab_focus(focusBack);
    }
}

void KeepDraftCard::keep() {
    if (!isOpen() || mode != Mode::KEEP || !valid) {
        return;
    }
    Request r;
    r.name = cleanName(typedName());
    const fs::path dir = folderOfSubject();
    if (!dir.empty()) {
        r.file = dir / (r.name + ".xopp");
        lastSubject = dir;
    }
    auto done = std::move(onKeep);
    onKeep = nullptr;
    onCancel = nullptr;
    onCloseChoice = nullptr;
    close(false);
    if (done) {
        done(r);
    }
}

void KeepDraftCard::fillSubjects() {
    gtk_container_foreach(GTK_CONTAINER(subjectsBox), [](GtkWidget* w, gpointer) { gtk_widget_destroy(w); }, nullptr);
    subjectButtons.clear();
    GSList* group = nullptr;
    for (size_t i = 0; i < subjects.size(); i++) {
        GtkWidget* b = gtk_radio_button_new(group);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(b));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), false);
        addClass(b, "amb-dlg-chip");
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        if (!subjects[i].color.empty()) {
            gtk_box_pack_start(GTK_BOX(row), dotFor(subjects[i].color), false, false, 0);
        }
        gtk_box_pack_start(GTK_BOX(row), gtk_label_new(subjects[i].name.c_str()), false, false, 0);
        gtk_container_add(GTK_CONTAINER(b), row);
        g_signal_connect(b, "toggled", G_CALLBACK(+[](GtkToggleButton* t, KeepDraftCard* self) {
                             if (!gtk_toggle_button_get_active(t)) {
                                 return;
                             }
                             for (size_t i = 0; i < self->subjectButtons.size(); i++) {
                                 if (self->subjectButtons[i] == GTK_WIDGET(t)) {
                                     self->chooseSubject(i);
                                     return;
                                 }
                             }
                         }),
                         this);
        gtk_flow_box_insert(GTK_FLOW_BOX(subjectsBox), b, -1);
        subjectButtons.push_back(b);
        watchKeys(b);
    }
    gtk_widget_show_all(subjectsBox);
}

void KeepDraftCard::chooseSubject(size_t index) {
    if (!subjects.empty()) {
        subject = std::min(index, subjects.size() - 1);
        for (size_t i = 0; i < subjectButtons.size(); i++) {
            auto* ctx = gtk_widget_get_style_context(subjectButtons[i]);
            if (i == subject) {
                gtk_style_context_add_class(ctx, "on");
            } else {
                gtk_style_context_remove_class(ctx, "on");
            }
        }
    }
    refresh();
}

void KeepDraftCard::refresh() {
    const fs::path dir = folderOfSubject();
    auto exists = [&dir](const std::string& n) {
        std::error_code ec;
        return !dir.empty() && fs::exists(dir / (n + ".xopp"), ec);
    };
    const NameCheck check = checkName(typedName(), exists);
    valid = check == NameCheck::OK && !subjects.empty();
    const char* text = "";
    if (subjects.empty()) {
        text = _("There is no subject in the library yet.");
    } else {
        switch (check) {
            case NameCheck::OK:
                break;
            case NameCheck::EMPTY:
                text = _("Give the draft a name.");
                break;
            case NameCheck::EXISTS:
                text = _("There is already a notebook with this name here.");
                break;
            case NameCheck::INVALID:
                text = _("Use a name without / or \\, and not starting with a dot.");
                break;
        }
    }
    gtk_label_set_text(GTK_LABEL(problem), text);
    gtk_widget_set_sensitive(keepButton, valid);
    auto* ctx = gtk_widget_get_style_context(entry);
    if (valid) {
        gtk_style_context_remove_class(ctx, "amb-dlg-bad");
    } else {
        gtk_style_context_add_class(ctx, "amb-dlg-bad");
    }
}

}  // namespace ambitus
