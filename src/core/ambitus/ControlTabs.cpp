/*
 * Ambitus
 *
 * Tabs in the Control: one document is in the window, the others wait aside (ambitus/Tabs.h).
 *
 * @license GNU GPLv2 or later
 */

#include <utility>

#include "control/Control.h"
#include "control/CrashHandler.h"
#include "control/ScrollHandler.h"
#include "control/settings/MetadataManager.h"
#include "control/zoom/ZoomControl.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "undo/UndoRedoHandler.h"

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
    tabs.insert(tabs.begin() + static_cast<std::ptrdiff_t>(at), ambitus::Tab{path, nullptr, {}});
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
    activeTab = ambitus::NO_TAB;
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

void Control::closeTab(size_t index) {
    if (index >= tabs.size()) {
        return;
    }
    activateTab(index);
    this->close(
            [ctrl = this](bool closed) {
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

void Control::tabsChanged() {
    if (win) {
        win->tabsChanged();
    }
}
