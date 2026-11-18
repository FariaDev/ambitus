/*
 * Ambitus
 *
 * Renaming a notebook: the rule the typed name must follow, and the move of the file on disk, with its side files and
 * the copies of its versions. The cover, the recent notebooks and the open tab follow in the library and the window
 * (LibraryView, MainWindow). The logic lives here, apart from the widgets, so it can be tested without GTK.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "filesystem.h"

namespace ambitus::rename {

/// Why a typed name cannot be used. SAME: it is the name the notebook has already (nothing to do, and no error).
enum class NameCheck { OK, SAME, EMPTY, INVALID, EXISTS };

/// The name of a notebook as the library shows it: its file name without the ".xopp"
std::string nameOf(const fs::path& notebook);

/// Whether the typed name can be the name of the notebook called `current` (both without ".xopp"). The typed name is
/// cleaned as a new notebook's name is (spaces trimmed, a ".xopp" at the end dropped). INVALID: "/" or "\\" (and ":"
/// on macOS), a control character, or a leading dot (the library skips hidden files). `exists` tells whether a
/// (cleaned) name is taken in the folder.
NameCheck checkName(const std::string& typed, const std::string& current,
                    const std::function<bool(const std::string&)>& exists);

/// Whether the name (cleaned) is taken by another notebook in the folder of `notebook`. The notebook itself does not
/// count: on a system that ignores the case of file names, "lista" is the same file as "Lista".
bool isTaken(const fs::path& notebook, const std::string& name);

/// Renames the notebook to `newName` in its own folder (a rename does not change the subject). The file and its side
/// files move (drafts::moveWithSideFiles), and the copies of its versions follow (versions::followMove under
/// versionsRoot). Nothing changes when the name is not OK, or the new file exists, or the file cannot move. The new
/// path, or nothing.
std::optional<fs::path> moveNotebook(const fs::path& from, const std::string& newName, const fs::path& versionsRoot);

}  // namespace ambitus::rename
