/*
 * Ambitus
 *
 * Tabs of open documents. Xournal++ has one document per window; the tabs keep
 * the others aside, each with a copy of its Document (the pages are shared, so
 * the undo history still points at them) and its undo history.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <string>

#include "model/Document.h"
#include "undo/UndoRedoHandler.h"

#include "filesystem.h"

namespace ambitus {

constexpr size_t NO_TAB = std::numeric_limits<size_t>::max();

struct Tab {
    fs::path path;
    /// null for the tab in the window
    std::unique_ptr<Document> doc;
    UndoRedoHandler::State undo;
};

/// What the tab strip shows
struct TabInfo {
    fs::path path;
    bool active = false;
    bool modified = false;
};

namespace tabs {
/// Which tab is in the window after `closed` is closed among `count` tabs, `active` being in the window before; the
/// index is in the list without the closed tab. The tab to the right takes the place, else the one to the left; NO_TAB
/// when none is left.
size_t afterClose(size_t count, size_t active, size_t closed);
/// Where a new tab goes: right after the active one, at the end when none is active
size_t insertAt(size_t count, size_t active);
}  // namespace tabs

}  // namespace ambitus
