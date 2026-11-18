/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "RenameCard.h"

#include <system_error>

#include "ambitus/NewNotebook.h"
#include "ambitus/Rename.h"
#include "gui/AmbitusMotion.h"
#include "util/StringUtils.h"
#include "util/i18n.h"

#include "NewNotebookDialog.h"  // for newnotebook::FADE_IN_MS, newnotebook::FADE_OUT_MS, as KeepDraftCard

namespace ambitus {

namespace {
constexpr int CARD_WIDTH = 420;

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

/// The subject a notebook is in, as its name shows: its folder, or the subject above the "Boards" folder of a board
std::string subjectName(const fs::path& file) {
    fs::path folder = file.parent_path();
    if (std::string(char_cast(folder.filename().u8string())) == _("Boards")) {
        folder = folder.parent_path();
    }
    return std::string(char_cast(folder.filename().u8string()));
}
}  // namespace

RenameCard::RenameCard(GtkOverlay* overlay, GtkWindow* window): window(window) {
    revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    GtkWidget* clicks = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(clicks), false);
    g_signal_connect(clicks, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, RenameCard* self) {
                         self->close();
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

    title = label("", "amb-dlg-title");
    gtk_box_pack_start(GTK_BOX(card), title, false, false, 0);

    gtk_box_pack_start(GTK_BOX(card), label(_("Name"), "amb-opt-label"), false, false, 0);
    entry = gtk_entry_new();
    addClass(entry, "amb-cmd-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Name"));
    gtk_entry_set_activates_default(GTK_ENTRY(entry), false);
    gtk_box_pack_start(GTK_BOX(card), entry, false, false, 0);
    problem = label("", "amb-dlg-problem");
    gtk_label_set_line_wrap(GTK_LABEL(problem), true);
    gtk_box_pack_start(GTK_BOX(card), problem, false, false, 0);

    where = label("", "amb-dlg-problem");  // the grey note under the field, as the line under a new draft's title
    addClass(where, "amb-dlg-note");
    gtk_box_pack_start(GTK_BOX(card), where, false, false, 0);

    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addClass(foot, "amb-dlg-foot");
    renameButton = gtk_button_new_with_label(_("Rename"));
    addClass(renameButton, "amb-dlg-primary");
    clickOf(renameButton, [this] { rename(); });
    gtk_box_pack_end(GTK_BOX(foot), renameButton, false, false, 0);
    cancelButton = gtk_button_new_with_label(_("Cancel"));
    addClass(cancelButton, "amb-dlg-ghost");
    clickOf(cancelButton, [this] { close(); });
    gtk_box_pack_end(GTK_BOX(foot), cancelButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(card), foot, false, false, 0);

    g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable*, RenameCard* self) { self->refresh(); }), this);
    watchKeys(entry);
    watchKeys(cancelButton);
    watchKeys(renameButton);

    g_signal_connect_swapped(revealer, "notify::child-revealed", G_CALLBACK(+[](RenameCard* self) {
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

RenameCard::~RenameCard() {
    if (focusBack) {
        g_object_remove_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }
}

bool RenameCard::isOpen() const {
    return revealer && GTK_IS_REVEALER(revealer) && gtk_revealer_get_reveal_child(GTK_REVEALER(revealer));
}

void RenameCard::watchKeys(GtkWidget* w) {
    g_signal_connect(
            w, "key-press-event",
            G_CALLBACK(+[](GtkWidget* from, GdkEventKey* e, RenameCard* self) { return self->onKey(from, e); }), this);
}

bool RenameCard::onKey(GtkWidget* from, GdkEventKey* e) {
    switch (e->keyval) {
        case GDK_KEY_Escape:
            close();
            return true;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (from == entry) {
                rename();
                return true;
            }
            return false;  // on a button, Enter presses it
        default:
            return false;
    }
}

std::string RenameCard::typedName() const { return gtk_entry_get_text(GTK_ENTRY(entry)); }

void RenameCard::open(const fs::path& notebook, bool isBoard, std::function<void(const std::string&)> callback,
                      std::function<void()> cancel) {
    file = notebook;
    board = isBoard;
    onRename = std::move(callback);
    onCancel = std::move(cancel);
    gtk_label_set_text(GTK_LABEL(title), board ? _("Rename board") : _("Rename notebook"));
    gtk_label_set_text(GTK_LABEL(where), FS(_F("In {1}") % subjectName(file)).c_str());
    gtk_entry_set_text(GTK_ENTRY(entry), rename::nameOf(file).c_str());
    refresh();
    reveal(entry);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}

void RenameCard::reveal(GtkWidget* focus) {
    if (focusBack) {
        g_object_remove_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
        focusBack = nullptr;
    }
    focusBack = gtk_window_get_focus(window);
    if (focusBack) {
        g_object_add_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }

    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? newnotebook::FADE_IN_MS : 0);
    gtk_widget_show(revealer);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
    gtk_widget_realize(focus);
    gtk_widget_grab_focus(focus);
}

void RenameCard::close() {
    close(true);
    auto cancel = std::move(onCancel);
    onCancel = nullptr;
    onRename = nullptr;
    if (cancel) {
        cancel();
    }
}

void RenameCard::close(bool restoreFocus) {
    if (!isOpen()) {
        return;
    }
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? newnotebook::FADE_OUT_MS : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), false);
    if (!animationsOn(revealer)) {
        gtk_widget_hide(revealer);
    }
    if (restoreFocus && focusBack && gtk_widget_get_mapped(focusBack)) {
        gtk_widget_grab_focus(focusBack);
    }
}

void RenameCard::rename() {
    if (!isOpen() || !valid) {
        return;
    }
    const std::string name = newnotebook::cleanName(typedName());
    auto done = std::move(onRename);
    onRename = nullptr;
    onCancel = nullptr;
    close(false);
    if (done) {
        done(name);
    }
}

void RenameCard::refresh() {
    const rename::NameCheck check = rename::checkName(
            typedName(), rename::nameOf(file), [this](const std::string& n) { return rename::isTaken(file, n); });
    valid = check == rename::NameCheck::OK;
    const std::string subject = subjectName(file);
    std::string text;
    switch (check) {
        case rename::NameCheck::OK:
        case rename::NameCheck::SAME:
            break;
        case rename::NameCheck::EMPTY:
            text = board ? _("Give the board a name.") : _("Give the notebook a name.");
            break;
        case rename::NameCheck::INVALID:
#ifdef __APPLE__
            text = _("Use a name without /, \\ or :, and not starting with a dot.");
#else
            text = _("Use a name without / or \\, and not starting with a dot.");
#endif
            break;
        case rename::NameCheck::EXISTS:
            text = board ? FS(_F("A board with that name already exists in {1}.") % subject) :
                           FS(_F("A notebook with that name already exists in {1}.") % subject);
            break;
    }
    gtk_label_set_text(GTK_LABEL(problem), text.c_str());
    // one line under the field: the problem when there is one, else the folder of the notebook
    gtk_widget_set_visible(problem, !text.empty());
    gtk_widget_set_visible(where, text.empty());
    gtk_widget_set_sensitive(renameButton, valid);
    auto* ctx = gtk_widget_get_style_context(entry);
    if (valid || check == rename::NameCheck::SAME) {
        gtk_style_context_remove_class(ctx, "amb-dlg-bad");
    } else {
        gtk_style_context_add_class(ctx, "amb-dlg-bad");
    }
}

}  // namespace ambitus
