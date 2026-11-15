/*
 * Ambitus
 *
 * The quick draft: a notebook with no name and no subject, saved on its own in
 * Ambitus's data folder until the user keeps it in a subject or throws it away.
 * The logic lives here, apart from the widgets, so it can be tested without GTK.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <ctime>
#include <functional>
#include <optional>
#include <string>

#include "filesystem.h"

namespace ambitus::drafts {

/// The folder of the drafts: "drafts" in Ambitus's data folder
fs::path folder();

/// Points folder() elsewhere (an empty path brings the default back). For the tests.
void setFolderForTesting(const fs::path& folder);

/// Whether the file is inside folder()
bool isDraft(const fs::path& file);

/// "Draft 2026-11-15 14-32", with " 2", " 3"... when the name is taken; never a name that `exists`
std::string newName(std::tm now, const std::function<bool(const std::string&)>& exists);

/// The label of a draft tab: "Draft · 14:32", from the name of its file
std::string tabLabel(const std::string& fileName);

/// Moves the draft to subjectFolder/name.xopp (with the backup and autosave files that are beside it). Refuses when
/// the destination exists; copies and removes when the rename crosses disks. The new path, or nothing.
std::optional<fs::path> keep(const fs::path& draft, const fs::path& subjectFolder, const std::string& name);

/// Deletes the draft and its side files; only inside folder()
bool discard(const fs::path& draft);

}  // namespace ambitus::drafts
