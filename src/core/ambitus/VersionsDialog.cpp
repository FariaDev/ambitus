/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "VersionsDialog.h"

#include <optional>
#include <string>

#include <gtk/gtk.h>

#include "control/Control.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "undo/UndoRedoHandler.h"
#include "util/PathUtil.h"
#include "util/StringUtils.h"  // for char_cast
#include "util/i18n.h"

#include "Versions.h"
#include "filesystem.h"

namespace ambitus {

namespace {
fs::path currentFile(Control* ctrl) {
    Document* doc = ctrl->getDocument();
    doc->lock_shared();
    fs::path file = doc->getFilepath();
    doc->unlock_shared();
    return file;
}

void toast(Control* ctrl, const std::string& text) {
    if (auto* win = ctrl->getWindow()) {
        win->showToast(text);
    }
}

/// An error card: the text says what did not work; the detail (what the system said) is only copied on demand
void failure(Control* ctrl, const std::string& text, const std::string& detail) {
    if (auto* win = ctrl->getWindow()) {
        win->showToastError(text, detail);
    }
}

/// What the system said about a failed step on a file, for the "Copy the detail" of the error card
std::string detailOf(const char* step, const fs::path& file, const std::error_code& ec) {
    return std::string(step) + ": " + std::string(char_cast(file.u8string())) + ": " + ec.message();
}

std::string sizeText(std::uintmax_t bytes) {
    gchar* s = g_format_size(bytes);
    std::string out = s;
    g_free(s);
    return out;
}

/// A copy of the version, named after the notebook and the time, opened in a tab of its own
void openCopy(Control* ctrl, const fs::path& notebook, const versions::Version& v) {
    GDateTime* dt = g_date_time_new_from_unix_local(versions::Clock::to_time_t(v.when));
    gchar* when = g_date_time_format(dt, "%d-%m %Hh%M");
    const std::string name =
            FS(_F("{1} (version of {2})") % std::string(char_cast(notebook.stem().u8string())) % when) + ".xopp";
    g_free(when);
    g_date_time_unref(dt);
    const fs::path copy = Util::getCacheSubfolder("versions-open") / fs::u8path(name);
    std::error_code ec;
    // opened before: that copy may have been edited since, so it is opened as it is
    if (!fs::exists(copy, ec)) {
        fs::copy_file(v.file, copy, ec);
    }
    if (ec) {
        failure(ctrl, _("The version could not be opened."), detailOf("copy_file", copy, ec));
        return;
    }
    ctrl->openInTab(copy);
}

/// The notebook goes back to the version; what is on disk now is kept as a version first
void restore(Control* ctrl, const fs::path& notebook, const versions::Version& v) {
    auto doRestore = [ctrl, notebook, v] {
        std::error_code ec;
        // next to the notebook first: keep() below may drop the oldest copy, which may be this one
        fs::path incoming = notebook;
        incoming.replace_filename(".ambitus-restaurando-" + notebook.filename().string());
        fs::copy_file(v.file, incoming, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            const std::error_code copyError = ec;
            fs::remove(incoming, ec);
            failure(ctrl, _("The version could not be restored."), detailOf("copy_file", incoming, copyError));
            return;
        }
        // the notebook as it is now, kept aside first: it is what the "Undo" of the notice puts back
        std::optional<versions::Version> before;
        if (fs::exists(notebook, ec)) {
            if (!versions::keep(versions::defaultRoot(), notebook, versions::Clock::now(), std::chrono::seconds(0),
                                30)) {
                fs::remove(incoming, ec);
                failure(ctrl, _("The notebook as it is now could not be kept aside, so nothing was changed."),
                        "keep: " + std::string(char_cast(notebook.u8string())));
                return;
            }
            const auto kept = versions::list(versions::defaultRoot(), notebook);
            if (!kept.empty()) {
                before = kept.front();
            }
        }
        fs::rename(incoming, notebook, ec);  // whole or not at all
        if (ec) {
            const std::error_code renameError = ec;
            fs::remove(incoming, ec);
            failure(ctrl, _("The version could not be restored."), detailOf("rename", notebook, renameError));
            return;
        }
        const auto page = static_cast<int>(ctrl->getCurrentPageNo());
        const std::string when = versions::describe(v.when, versions::Clock::now());
        ctrl->openFileWithoutSavingTheCurrentDocument(notebook, false, page, [ctrl, notebook, before, when](bool ok) {
            if (!ok) {
                failure(ctrl, _("The version could not be restored."),
                        "open: " + std::string(char_cast(notebook.u8string())));
                return;
            }
            const std::string text = FS(_F("Version from {1} restored. The one before it was kept.") % when);
            auto* win = ctrl->getWindow();
            if (!win) {
                return;
            }
            if (!before) {
                win->showToast(text);
                return;
            }
            // Undo: the version kept before the swap goes back in place (and the one just left is kept in turn)
            win->showToastAction(text, _("Undo"), [ctrl, notebook, back = *before] {
                if (currentFile(ctrl) == notebook) {
                    restore(ctrl, notebook, back);
                }
            });
        });
    };
    if (ctrl->getUndoRedoHandler()->isChanged()) {
        ctrl->save([doRestore](bool saved) {
            if (saved) {
                doRestore();
            }
        });
        return;
    }
    doRestore();
}

void confirmRestore(Control* ctrl, GtkWindow* parent, const fs::path& notebook, const versions::Version& v) {
    GtkWidget* ask = gtk_message_dialog_new(parent, GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                                            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", _("Restore this version?"));
    const std::string detail = FS(_F("The notebook goes back to how it was {1}. How it is now is kept in the list.") %
                                  versions::describe(v.when, versions::Clock::now()));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(ask), "%s", detail.c_str());
    gtk_dialog_add_buttons(GTK_DIALOG(ask), _("Cancel"), GTK_RESPONSE_CANCEL, _("Restore"), GTK_RESPONSE_ACCEPT,
                           nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(ask), GTK_RESPONSE_CANCEL);
    struct Data {
        Control* ctrl;
        fs::path notebook;
        versions::Version v;
    };
    g_signal_connect_data(
            ask, "response", G_CALLBACK(+[](GtkDialog* d, int response, Data* data) {
                gtk_widget_destroy(GTK_WIDGET(d));
                if (response == GTK_RESPONSE_ACCEPT) {
                    restore(data->ctrl, data->notebook, data->v);
                }
            }),
            new Data{ctrl, notebook, v}, +[](gpointer d, GClosure*) { delete static_cast<Data*>(d); },
            GConnectFlags(0));
    gtk_widget_show_all(ask);
}

GtkWidget* rowFor(Control* ctrl, GtkWidget* dialog, const fs::path& notebook, const versions::Version& v) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);
    GtkWidget* when = gtk_label_new(versions::describe(v.when, versions::Clock::now()).c_str());
    gtk_label_set_xalign(GTK_LABEL(when), 0);
    gtk_box_pack_start(GTK_BOX(box), when, true, true, 0);
    GtkWidget* size = gtk_label_new(sizeText(v.size).c_str());
    gtk_style_context_add_class(gtk_widget_get_style_context(size), "dim-label");
    gtk_box_pack_start(GTK_BOX(box), size, false, false, 8);

    struct Data {
        Control* ctrl;
        GtkWidget* dialog;
        fs::path notebook;
        versions::Version v;
    };
    auto button = [&](const char* label, GCallback cb) {
        GtkWidget* b = gtk_button_new_with_label(label);
        g_signal_connect_data(
                b, "clicked", cb, new Data{ctrl, dialog, notebook, v},
                +[](gpointer d, GClosure*) { delete static_cast<Data*>(d); }, GConnectFlags(0));
        gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    };
    button(_("Open"), G_CALLBACK(+[](GtkButton*, Data* d) {
               auto* ctrl = d->ctrl;
               auto notebook = d->notebook;
               auto v = d->v;
               gtk_widget_destroy(d->dialog);  // frees d
               openCopy(ctrl, notebook, v);
           }));
    button(_("Restore…"), G_CALLBACK(+[](GtkButton*, Data* d) {
               auto* ctrl = d->ctrl;
               auto notebook = d->notebook;
               auto v = d->v;
               GtkWindow* parent = gtk_window_get_transient_for(GTK_WINDOW(d->dialog));
               gtk_widget_destroy(d->dialog);  // frees d
               confirmRestore(ctrl, parent, notebook, v);
           }));
    return box;
}
}  // namespace

void showVersions(Control* ctrl) {
    const fs::path notebook = currentFile(ctrl);
    if (notebook.empty()) {
        toast(ctrl, _("Save the notebook first: its versions are kept from then on."));
        return;
    }
    GtkWindow* parent = ctrl->getGtkWindow();
    const std::string title = FS(_F("Earlier versions of {1}") % std::string(char_cast(notebook.stem().u8string())));
    GtkWidget* dialog = gtk_dialog_new_with_buttons(title.c_str(), parent,
                                                    GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                                                    _("Close"), GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 460, 380);
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), nullptr);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 10);
    GtkWidget* hint = gtk_label_new(_("A copy is kept before each save, at most one every ten minutes. "
                                      "The newest thirty stay."));
    gtk_label_set_line_wrap(GTK_LABEL(hint), true);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
    gtk_box_pack_start(GTK_BOX(content), hint, false, false, 0);

    const auto all = versions::list(versions::defaultRoot(), notebook);
    if (all.empty()) {
        GtkWidget* none =
                gtk_label_new(_("No version yet. The first one is kept the next time the notebook is saved."));
        gtk_label_set_line_wrap(GTK_LABEL(none), true);
        gtk_box_pack_start(GTK_BOX(content), none, true, true, 0);
    } else {
        GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        GtkWidget* list = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
        for (auto const& v: all) {
            gtk_container_add(GTK_CONTAINER(list), rowFor(ctrl, dialog, notebook, v));
        }
        gtk_container_add(GTK_CONTAINER(scroll), list);
        gtk_box_pack_start(GTK_BOX(content), scroll, true, true, 0);
    }
    gtk_widget_show_all(dialog);
}

}  // namespace ambitus
