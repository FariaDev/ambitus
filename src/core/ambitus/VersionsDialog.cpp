/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "VersionsDialog.h"

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
    fs::copy_file(v.file, copy, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        toast(ctrl, _("The version could not be opened."));
        return;
    }
    ctrl->openInTab(copy);
}

/// The notebook goes back to the version; what is on disk now is kept as a version first
void restore(Control* ctrl, const fs::path& notebook, const versions::Version& v) {
    auto doRestore = [ctrl, notebook, v] {
        versions::keep(versions::defaultRoot(), notebook, versions::Clock::now(), std::chrono::seconds(0), 30);
        std::error_code ec;
        fs::copy_file(v.file, notebook, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            toast(ctrl, _("The version could not be restored."));
            return;
        }
        const auto page = static_cast<int>(ctrl->getCurrentPageNo());
        ctrl->openFileWithoutSavingTheCurrentDocument(notebook, false, page, [ctrl](bool ok) {
            toast(ctrl, ok ? _("Version restored. The state before it is in the list too.") :
                             _("The version could not be restored."));
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
