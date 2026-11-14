#include "SendPage.h"

#include <string>
#include <thread>

#include "control/Control.h"
#include "control/settings/Settings.h"
#include "gui/MainWindow.h"
#include "library/InkReader.h"
#include "model/Document.h"
#include "undo/UndoRedoHandler.h"
#include "util/StringUtils.h"  // for char_cast
#include "util/Util.h"         // for execInUiThread
#include "util/i18n.h"

#include "filesystem.h"

namespace ambitus {

namespace {
auto messageFor(library::PageSend result) -> std::string {
    using library::PageSend;
    switch (result) {
        case PageSend::NoSumma:
            return _("Summa is not open. Open Summa and try again.");
        case PageSend::OldSumma:
            return _("This Summa does not take pages from Ambitus yet. Update Summa.");
        case PageSend::NotFound:
            return _("Summa did not find the notebook on disk.");
        case PageSend::NoWindow:
            return _("The window of Summa is not ready. Try again.");
        default:
            return _("The page could not be sent to Summa.");
    }
}

void send(Control* ctrl) {
    Document* doc = ctrl->getDocument();
    doc->lock();
    const fs::path file = doc->getFilepath();
    doc->unlock();
    if (file.empty()) {
        return;
    }
    const size_t page = ctrl->getCurrentPageNo();
    const std::string name(char_cast(file.stem().u8string()));
    // the bridge answers at once, but a socket never runs on the UI thread
    std::thread([file, page, name, ctrl] {
        auto bridge = library::findSummaBridge();
        const auto result = bridge ? library::sendPageToSumma(*bridge, file, page, name) : library::PageSend::NoSumma;
        if (result != library::PageSend::Sent) {
            Util::execInUiThread([ctrl, result] {
                if (auto* win = ctrl->getWindow()) {
                    win->showToast(messageFor(result));
                }
            });
        }
    }).detach();
}
}  // namespace

void sendPageToSumma(Control* ctrl) {
    if (!ctrl->getSettings()->getAmbitusSummaBridge()) {
        if (auto* win = ctrl->getWindow()) {
            win->showToast(_("The bridge with Summa is off. Switch it on in the settings."));
        }
        return;
    }
    Document* doc = ctrl->getDocument();
    doc->lock();
    const bool untitled = doc->getFilepath().empty();
    doc->unlock();
    if (untitled || ctrl->getUndoRedoHandler()->isChanged()) {
        ctrl->save([ctrl](bool saved) {
            if (saved) {
                send(ctrl);
            }
        });
        return;
    }
    send(ctrl);
}

}  // namespace ambitus
