/*
 * Ambitus
 *
 * Ambitus keeps its settings, history and caches in folders of its own
 * ("ambitus"), so that Xournal++ installed on the same computer keeps its own.
 * Ambitus used the Xournal++ folders before: on the first start with the new
 * name they are copied, never moved, and Xournal++ finds its folders as they were.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "filesystem.h"

namespace ambitus::folders {

/// Copies the folder `from` to `to` when `to` does not exist yet. Returns true if it copied.
/// `from` is never changed; a failed copy leaves no half-made `to` behind.
bool copyIfMissing(const fs::path& from, const fs::path& to);

/// On the first start, copies the Xournal++ config, data, state and cache folders to Ambitus's own.
void adoptXournalppFolders();

}  // namespace ambitus::folders
