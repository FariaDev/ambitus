#include "SendPage.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ambitus/SummaPoll.h"  // for renderCurrentPage
#include "control/Control.h"
#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "gui/menus/menubar/Menubar.h"
#include "library/InkReader.h"
#include "model/Document.h"
#include "model/PageRef.h"
#include "model/XojPage.h"
#include "undo/UndoRedoHandler.h"
#include "util/Rectangle.h"
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

/// What to send, worked out when it is sent (after the notebook is saved)
using Choose = std::function<summacrop::Pick(Control*)>;

void sendChosen(Control* ctrl, Choose choose);

/// The toast after a page went: an excerpt is offered whole, for the one who wanted all of it
void toastSent(Control* ctrl, const summacrop::Pick& what, bool cropSent) {
    auto* win = ctrl->getWindow();
    if (!win) {
        return;
    }
    if (cropSent && what.fromSelection) {
        win->showToast(_("Selection sent to Summa"));
    } else if (cropSent) {
        const size_t page = what.page;
        win->showToastAction(_("Excerpt sent to Summa"), _("Send the whole page"), [ctrl, page] {
            sendChosen(ctrl, [page](Control*) { return summacrop::Pick{page, std::nullopt, false}; });
        });
    } else {
        win->showToast(_("Page sent to Summa"));
    }
}

void send(Control* ctrl, const Choose& choose) {
    Document* doc = ctrl->getDocument();
    doc->lock();
    const fs::path file = doc->getFilepath();
    const size_t pages = doc->getPageCount();
    doc->unlock();
    if (file.empty()) {
        return;
    }
    const summacrop::Pick what = choose(ctrl);
    if (what.page >= pages) {
        return;
    }
    // a lasso selection keeps its elements out of the layers while it lasts, so the page would be drawn without them
    ctrl->clearSelectionEndText();
    const size_t page = what.page;
    const std::string name(char_cast(file.stem().u8string()));
    // the page goes as an image too, drawn here from memory; without it Summa draws the file on disk
    std::string png = renderCurrentPage(doc, page, what.crop).value_or(std::string());
    // a part goes with its place on the page, and only with the picture: without it Summa reads the whole page
    const std::optional<library::PageCrop> crop = png.empty() ? std::nullopt : wireCrop(what.crop);
    // the bridge answers at once, but a socket never runs on the UI thread
    std::thread([file, page, name, png = std::move(png), crop, what, ctrl] {
        auto bridge = library::findSummaBridge();
        bool cropSent = false;
        const auto result = bridge ? library::sendPageToSumma(*bridge, file, page, name, png, crop, &cropSent) :
                                     library::PageSend::NoSumma;
        Util::execInUiThread([ctrl, result, what, cropSent] {
            if (result != library::PageSend::Sent) {
                if (auto* win = ctrl->getWindow()) {
                    win->showToast(messageFor(result));
                }
                return;
            }
            toastSent(ctrl, what, cropSent);
        });
    }).detach();
}

/// Saves the notebook when it has no file or has changes, then sends what `choose` picks
void sendChosen(Control* ctrl, Choose choose) {
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
        ctrl->save([ctrl, choose = std::move(choose)](bool saved) {
            if (saved) {
                send(ctrl, choose);
            }
        });
        return;
    }
    send(ctrl, choose);
}
}  // namespace

summacrop::SendMode currentSendMode(Control* ctrl) {
    return summacrop::sendModeFromId(ctrl->getSettings()->getAmbitusSummaSend());
}

summacrop::Pick choosePage(Control* ctrl, bool useSelection, summacrop::SendMode mode) {
    Document* doc = ctrl->getDocument();
    const size_t current = ctrl->getCurrentPageNo();
    XournalView* xournal = ctrl->getWindow() ? ctrl->getWindow()->getXournal() : nullptr;

    std::vector<summacrop::OnScreen> onScreen;
    std::optional<summacrop::SelectionOnPage> selection;
    doc->lock_shared();
    const size_t count = doc->getPageCount();
    if (xournal) {
        // the selection is in page points of the page it was made on
        EditSelection* sel = useSelection ? xournal->getSelection() : nullptr;
        if (sel) {
            const PageRef page = sel->getSourcePage();
            const size_t index = doc->indexOf(page);
            if (index < count) {
                selection = summacrop::SelectionOnPage{
                        index, page->getWidth(), page->getHeight(),
                        summacrop::rotatedBounds(summacrop::Rect{sel->getXOnView(), sel->getYOnView(), sel->getWidth(),
                                                                 sel->getHeight()},
                                                 sel->getRotation())};
            }
        }
        if (mode == summacrop::SendMode::Screen) {
            for (size_t i = 0; i < count; i++) {
                std::unique_ptr<xoj::util::Rectangle<double>> rect(xournal->getVisibleRect(i));
                if (rect) {
                    const PageRef page = doc->getPage(i);
                    onScreen.push_back(
                            summacrop::OnScreen{i, page->getWidth(), page->getHeight(),
                                                summacrop::Rect{rect->x, rect->y, rect->width, rect->height}});
                }
            }
        }
    }
    doc->unlock_shared();
    return summacrop::pick(current, onScreen, selection, mode, useSelection);
}

std::optional<library::PageCrop> wireCrop(const std::optional<summacrop::Rect>& crop) {
    if (!crop) {
        return std::nullopt;
    }
    return library::PageCrop{crop->x, crop->y, crop->width, crop->height};
}

void sendPageToSumma(Control* ctrl) {
    sendChosen(ctrl, [](Control* c) { return choosePage(c, true, currentSendMode(c)); });
}

void sendOtherPartToSumma(Control* ctrl) {
    const summacrop::SendMode other = currentSendMode(ctrl) == summacrop::SendMode::Page ? summacrop::SendMode::Screen :
                                                                                           summacrop::SendMode::Page;
    sendChosen(ctrl, [other](Control* c) { return choosePage(c, false, other); });
}

std::string sendOtherPartLabel(Control* ctrl) {
    return currentSendMode(ctrl) == summacrop::SendMode::Page ? _("Send only what is on screen to Summa") :
                                                                _("Send the whole page to Summa");
}

void setSendMode(Control* ctrl, summacrop::SendMode mode) {
    ctrl->getSettings()->setAmbitusSummaSend(summacrop::sendModeId(mode));
    if (auto* win = ctrl->getWindow()) {
        if (auto* menubar = win->getMenubar()) {
            menubar->setActionLabel("win.send-other-part-to-summa", sendOtherPartLabel(ctrl));
        }
    }
}

}  // namespace ambitus
