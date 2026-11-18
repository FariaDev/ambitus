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

#include "ambitus/NewNotebook.h"
#include "control/settings/PageTemplateSettings.h"

#include "filesystem.h"

namespace ambitus::drafts {

/// The folder of the drafts: "drafts" in Ambitus's data folder
fs::path folder();

/// The page template of a draft made in the draft card: the paper and the size chosen there, on the user's page
/// template `base`. The infinite size is the board, as "New board" makes it (board::pageTemplate).
PageTemplateSettings pageTemplate(const PageTemplateSettings& base, newnotebook::Paper paper,
                                  newnotebook::PageSize size);

/// Points folder() elsewhere (an empty path brings the default back). For the tests.
void setFolderForTesting(const fs::path& folder);

/// Whether the file is inside folder()
bool isDraft(const fs::path& file);

/// "Draft 2026-11-15 14-32", with " 2", " 3"... when the name is taken; never a name that `exists`
std::string newName(std::tm now, const std::function<bool(const std::string&)>& exists);

/// The label of a draft tab: "Draft · 14:32", from the name of its file
std::string tabLabel(const std::string& fileName);

/// The name a draft shows in the library and in the window title: "Draft · 15 Nov, 14:32", with the month abbreviated
/// by the locale; the name of the file does not change
std::string displayName(const std::string& stem);

/// Moves a notebook file to `to` with the backup and autosave files that are beside it (the side files that the
/// destination already has are left where they are). Copies and removes when the rename crosses disks. False when the
/// file itself did not move; nothing moves then.
bool moveWithSideFiles(const fs::path& from, const fs::path& to);

/// Moves the draft to subjectFolder/name.xopp (with the backup and autosave files that are beside it). Refuses when
/// the destination exists; copies and removes when the rename crosses disks. The new path, or nothing.
std::optional<fs::path> keep(const fs::path& draft, const fs::path& subjectFolder, const std::string& name);

/// Deletes the draft and its side files; only inside folder()
bool discard(const fs::path& draft);

}  // namespace ambitus::drafts
