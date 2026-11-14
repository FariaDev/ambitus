/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "VersionsRestore.h"

#include <optional>
#include <string>

#include <gtk/gtk.h>

#include "control/Control.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "undo/UndoRedoHandler.h"
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
            win->versionsRestored();  // out of the preview, the list with the copy that was just kept
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
}  // namespace

void restoreVersion(Control* ctrl, const fs::path& notebook, const versions::Version& v) { restore(ctrl, notebook, v); }

}  // namespace ambitus
