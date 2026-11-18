/*
 * Ambitus
 *
 * Tabs in the Control: one document is in the window, the others wait aside (ambitus/Tabs.h).
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <string>
#include <utility>

#include "control/Control.h"
#include "control/CrashHandler.h"
#include "control/ScrollHandler.h"
#include "control/actions/ActionDatabase.h"
#include "control/settings/MetadataManager.h"
#include "control/settings/Settings.h"
#include "control/xojfile/SaveHandler.h"
#include "control/zoom/ZoomControl.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "undo/UndoRedoHandler.h"
#include "util/PathUtil.h"
#include "util/Util.h"

#include "Tabs.h"

namespace ambitus::tabs {

size_t afterClose(size_t count, size_t active, size_t closed) {
    if (active == NO_TAB || count <= 1) {
        return NO_TAB;
    }
    if (closed != active) {
        return closed < active ? active - 1 : active;
    }
    return closed + 1 < count ? closed : closed - 1;
}

size_t insertAt(size_t count, size_t active) { return active == NO_TAB ? count : active + 1; }

fs::path autosavePath(const fs::path& file, const fs::path& autosaves, long pid, unsigned serial) {
    fs::path path;
    if (file.empty()) {
        path = autosaves / (std::to_string(pid) + "-tab" + std::to_string(serial) + ".xopp");
    } else {
        path = file;
        path.replace_filename(fs::path(".") += file.filename());
    }
    Util::clearExtensions(path);
    path += ".autosave.xopp";
    return path;
}

}  // namespace ambitus::tabs

namespace {
/// The documents kept aside tell nobody about their changes
DocumentHandler* quietHandler() {
    static DocumentHandler handler;
    return &handler;
}
}  // namespace

void Control::ensureActiveTab() {
    if (activeTab != ambitus::NO_TAB) {
        return;
    }
    doc->lock_shared();
    fs::path path = doc->getFilepath();
    bool empty = doc->getPageCount() == 0;
    doc->unlock_shared();
    if (empty || (path.empty() && !undoRedo->isChanged())) {
        return;  // nothing worth a tab (the blank document of the start, the library after the last tab)
    }
    for (auto& tab: tabs) {
        if (!path.empty() && tab.path == path) {
            return;  // a tab being put aside or brought back
        }
    }
    const size_t at = ambitus::tabs::insertAt(tabs.size(), newTabAfter);
    newTabAfter = ambitus::NO_TAB;
    tabs.insert(tabs.begin() + static_cast<std::ptrdiff_t>(at), ambitus::Tab{path, nullptr, {}, {}, nextTabSerial++});
    activeTab = at;
}

void Control::parkActiveTab() {
    if (activeTab == ambitus::NO_TAB) {
        return;
    }
    clearSelectionEndText();
    resetGeometryTool();

    doc->lock_shared();
    auto file = doc->getEvMetadataFilename();
    doc->unlock_shared();
    metadata->storeMetadata(file, static_cast<int>(getCurrentPageNo()), zoom->getZoomReal());
    metadata->documentChanged();

    auto& tab = tabs[activeTab];
    tab.doc = std::make_unique<Document>(quietHandler());
    tab.doc->lock();
    doc->lock_shared();
    *tab.doc = *doc;
    tab.path = doc->getFilepath();
    doc->unlock_shared();
    tab.doc->unlock();
    tab.undo = undoRedo->takeState();
    keepAutosaveAside(tab);
    activeTab = ambitus::NO_TAB;
}

void Control::keepAutosaveAside(ambitus::Tab& tab) {
    // The autosave of the document goes aside with it, or the next document autosaved would delete it
    // (setLastAutosaveFile). One without a file was autosaved under the name of the window (pid), which the next one
    // without a file would overwrite: it takes a name of its own.
    tab.autosave = std::exchange(lastAutosaveFilename, {});
    if (!tab.path.empty() || tab.autosave.empty()) {
        return;
    }
    auto own = ambitus::tabs::autosavePath({}, Util::getCacheSubfolder("autosaves"), static_cast<long>(Util::getPid()),
                                           tab.serial);
    try {
        if (fs::exists(tab.autosave)) {
            fs::rename(tab.autosave, own);
            tab.autosave = own;
        }
    } catch (const fs::filesystem_error& e) {
        g_warning("Could not keep the autosave of a tab aside: %s", e.what());
    }
}

void Control::autosaveTabsAside() {
    for (size_t i = 0; i < tabs.size(); i++) {
        auto& tab = tabs[i];
        if (i == activeTab || !tab.doc || !tab.undo.isChangedAutosave()) {
            continue;
        }
        // Nothing edits a document aside, so it is saved here, at once: each one only once after it is put aside
        auto path = ambitus::tabs::autosavePath(tab.path, Util::getCacheSubfolder("autosaves"),
                                                static_cast<long>(Util::getPid()), tab.serial);
        fs::path temp = path;
        temp += u8"~";
        SaveHandler handler;
        tab.doc->lock_shared();
        handler.prepareSave(tab.doc.get(), path);
        tab.doc->unlock_shared();
        handler.saveTo(temp);
        if (!handler.getErrorMessage().empty()) {
            g_warning("Could not autosave a tab: %s", handler.getErrorMessage().c_str());
            continue;
        }
        try {
            fs::rename(temp, path);
            if (!tab.autosave.empty() && tab.autosave != path && fs::exists(tab.autosave)) {
                fs::remove(tab.autosave);
            }
        } catch (const fs::filesystem_error& e) {
            g_warning("Could not autosave a tab: %s", e.what());
            continue;
        }
        tab.autosave = path;
        tab.undo.autosaved();
    }
}

void Control::deleteTabAutosaves() {
    for (auto& tab: tabs) {
        try {
            if (!tab.autosave.empty() && fs::exists(tab.autosave)) {
                fs::remove(tab.autosave);
            }
        } catch (const fs::filesystem_error& e) {
            g_warning("Could not remove the autosave of a tab: %s", e.what());
        }
        tab.autosave.clear();
    }
}

void Control::activateTab(size_t index) {
    if (index >= tabs.size() || index == activeTab) {
        if (win && index == activeTab) {
            win->showEditor();
        }
        return;
    }
    parkActiveTab();

    auto& tab = tabs[index];
    activeTab = index;
    doc->lock();
    *doc = *tab.doc;  // fires DOCUMENT_CHANGE_COMPLETE
    doc->unlock();
    tab.doc.reset();
    undoRedo->restoreState(std::move(tab.undo));
    tab.undo = {};
    lastAutosaveFilename = std::exchange(tab.autosave, {});

    setEmergencyDocument(doc);
    fileLoaded();
    if (win) {
        win->showEditor();
    }
    tabsChanged();
}

void Control::openInTab(fs::path filepath, int scrollToPage) {
    for (size_t i = 0; i < tabs.size(); i++) {
        fs::path path = tabs[i].path;
        if (i == activeTab) {
            doc->lock_shared();
            path = doc->getFilepath();
            doc->unlock_shared();
        }
        if (!path.empty() && path == filepath) {
            activateTab(i);
            if (scrollToPage >= 0) {
                scrollHandler->scrollToPage(static_cast<size_t>(scrollToPage));
            }
            if (win) {
                win->showEditor();
            }
            return;
        }
    }

    startNewTab();
    const size_t previous = newTabAfter;  // the tab put aside, to come back to if the file does not open
    // the new document gets its tab when it is loaded (fileLoaded → updateWindowTitle → ensureActiveTab)
    openFileWithoutSavingTheCurrentDocument(std::move(filepath), false, scrollToPage, [ctrl = this, previous](bool ok) {
        ctrl->newTabAfter = ambitus::NO_TAB;
        if (!ok && ctrl->activeTab == ambitus::NO_TAB && previous != ambitus::NO_TAB) {
            ctrl->activateTab(previous);
        }
        ctrl->tabsChanged();
    });
}

void Control::startNewTab() {
    ensureActiveTab();
    const size_t previous = activeTab;
    parkActiveTab();
    newTabAfter = previous;
}

void Control::closeActiveTab() {
    if (activeTab != ambitus::NO_TAB) {
        closeTab(activeTab);
    }
}

void Control::closeTab(size_t index) {
    if (index >= tabs.size()) {
        return;
    }
    activateTab(index);
    fs::path path;
    doc->lock_shared();
    path = doc->getFilepath();
    doc->unlock_shared();
    const int page = static_cast<int>(getCurrentPageNo());
    this->close(
            [ctrl = this, path, page](bool closed) {
                if (closed) {
                    ctrl->closedTabs.push(path, page);  // unsaved new files (no path) are skipped
                }
                if (!closed || ctrl->activeTab == ambitus::NO_TAB) {
                    return;
                }
                const size_t closedTab = ctrl->activeTab;
                const size_t next = ambitus::tabs::afterClose(ctrl->tabs.size(), closedTab, closedTab);
                ctrl->tabs.erase(ctrl->tabs.begin() + static_cast<std::ptrdiff_t>(closedTab));
                ctrl->activeTab = ambitus::NO_TAB;
                if (next != ambitus::NO_TAB) {
                    ctrl->activateTab(next);
                } else {
                    ctrl->tabsChanged();
                    if (ctrl->win) {
                        ctrl->win->showLibrary();
                    }
                }
            },
            true, true);
}

auto Control::getTabs() const -> std::vector<ambitus::TabInfo> {
    std::vector<ambitus::TabInfo> infos;
    for (size_t i = 0; i < tabs.size(); i++) {
        ambitus::TabInfo info;
        info.active = i == activeTab;
        if (info.active) {
            doc->lock_shared();
            info.path = doc->getFilepath();
            doc->unlock_shared();
            info.modified = undoRedo->isChanged();
        } else {
            info.path = tabs[i].path;
            info.modified = tabs[i].undo.isChanged();
        }
        infos.push_back(std::move(info));
    }
    return infos;
}

void Control::updateReopenAction() {
    if (actionDB) {
        actionDB->enableAction(Action::REOPEN_CLOSED_TAB, !closedTabs.empty());
    }
}

bool Control::canReopenClosedTab() const { return !closedTabs.empty(); }

void Control::reopenClosedTab() {
    std::vector<fs::path> open;
    for (auto const& tab: getTabs()) {
        if (!tab.path.empty()) {
            open.push_back(tab.path);
        }
    }
    auto found = closedTabs.pop(
            [&open](const fs::path& path) { return std::find(open.begin(), open.end(), path) != open.end(); },
            [](const fs::path& path) {
                std::error_code err;
                return fs::exists(path, err);
            });
    if (found) {
        openInTab(found->path, found->page);
    }
    updateReopenAction();
}

std::vector<ambitus::restoretabs::SavedTab> Control::tabsForRestore() const {
    std::vector<ambitus::restoretabs::SavedTab> tabs;
    for (auto const& info: getTabs()) {
        if (info.path.empty()) {
            continue;
        }
        int page = 0;
        if (info.active) {
            page = static_cast<int>(getCurrentPageNo());
        } else if (auto metadata = MetadataManager::getForFile(info.path)) {
            page = metadata->page;
        }
        tabs.push_back({info.path, page, info.active});
    }
    return tabs;
}

bool Control::restoreTabsFromLastTime() {
    Settings* settings = getSettings();
    if (!settings->getAmbitusOpenTabsFromLastTime()) {
        return false;
    }
    std::vector<ambitus::restoretabs::SavedTab> tabs;
    for (auto& tab: ambitus::restoretabs::deserialize(settings->getAmbitusOpenTabs())) {
        if (tab.path.empty()) {
            continue;
        }
        if (std::error_code err; !fs::exists(tab.path, err)) {
            continue;
        }
        tabs.push_back(std::move(tab));
    }
    if (tabs.empty()) {
        return false;
    }
    size_t active = 0;
    for (size_t i = 0; i < tabs.size(); i++) {
        if (tabs[i].active) {
            active = i;
            break;
        }
    }
    for (auto const& tab: tabs) {
        openInTab(tab.path, tab.page);
    }
    activateTab(active);
    // Every load queues a metadata callback on the main loop. A tab parked before its callback runs would remember
    // page one instead of the page it was saved at. Once those callbacks have gone, put the pages back and show the
    // active notebook at its own page.
    Util::execInUiThread([ctrl = this, tabs = std::move(tabs), active]() {
        for (auto const& tab: tabs) {
            ctrl->metadata->storeMetadata(tab.path, tab.page, ctrl->zoom->getZoomReal());
            ctrl->metadata->documentChanged();
        }
        ctrl->scrollHandler->scrollToPage(static_cast<size_t>(tabs[active].page));
    });
    tabsChanged();
    return true;
}

void Control::tabsChanged() {
    updateReopenAction();
    if (win) {
        win->tabsChanged();
    }
}
