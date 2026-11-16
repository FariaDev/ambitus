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
#include "SubjectPicker.h"

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
    subjectPicker = std::make_unique<SubjectPicker>();
    subjectPicker->setOnChanged([this] { refresh(); });
    gtk_box_pack_start(GTK_BOX(keepPage), subjectPicker->field(), false, false, 0);

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
    watchKeys(subjectPicker->field());
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
        case GDK_KEY_Up:
        case GDK_KEY_Down:
            // on the subject, the arrows go through the list without opening it
            if (from == subjectPicker->field() && !subjectPicker->empty()) {
                const bool down = e->keyval == GDK_KEY_Down;
                const size_t subject = subjectPicker->selected();
                if (down && subject + 1 < subjectPicker->list().size()) {
                    subjectPicker->choose(subject + 1);
                } else if (!down && subject > 0) {
                    subjectPicker->choose(subject - 1);
                }
                return true;
            }
            return false;
        default:
            return false;
    }
}

std::string KeepDraftCard::typedName() const { return gtk_entry_get_text(GTK_ENTRY(entry)); }

fs::path KeepDraftCard::folderOfSubject() const {
    if (subjectPicker->empty()) {
        return {};
    }
    return subjectPicker->list()[subjectPicker->selected()].path;
}

void KeepDraftCard::open(std::vector<library::Subject> list, std::function<void(const Request&)> callback,
                         std::function<void()> cancel) {
    mode = Mode::KEEP;
    onKeep = std::move(callback);
    onCloseChoice = nullptr;
    onCancel = std::move(cancel);
    subjectPicker->setSubjects(std::move(list));
    size_t subject = 0;
    if (!lastSubject.empty()) {
        const auto& subjects = subjectPicker->list();
        auto it = std::find_if(subjects.begin(), subjects.end(),
                               [](const library::Subject& s) { return s.path == lastSubject; });
        if (it != subjects.end()) {
            subject = static_cast<size_t>(std::distance(subjects.begin(), it));
        }
    }
    subjectPicker->choose(subject);
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
    subjectPicker->popdown();
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

void KeepDraftCard::refresh() {
    const fs::path dir = folderOfSubject();
    auto exists = [&dir](const std::string& n) {
        std::error_code ec;
        return !dir.empty() && fs::exists(dir / (n + ".xopp"), ec);
    };
    const NameCheck check = checkName(typedName(), exists);
    valid = check == NameCheck::OK && !subjectPicker->empty();
    const char* text = "";
    if (subjectPicker->empty()) {
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
