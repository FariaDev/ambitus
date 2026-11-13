/*
 * Ambitus
 *
 * Finds the .xopp notebooks under a library folder.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "filesystem.h"  // for path

namespace ambitus::library {

struct Notebook {
    fs::path path;       ///< canonical path of the .xopp
    std::string name;    ///< file name without the extension
    std::string folder;  ///< subfolder relative to the scanned root, "" at the root, "/" separated
    std::time_t modified = 0;
};

/**
 * Lists the .xopp files under root, newest first. Same rules the Summa desk used:
 * - at most maxDepth folder levels;
 * - hidden files and folders (starting with '.') are skipped, and so are the Xournal++ autosave/backup files
 *   (".xopp~", "*.autosave.xopp");
 * - a symlink is followed only if its target stays inside root, and every file appears once (by canonical path),
 *   so symlink cycles end.
 * Never writes anything. A missing or unreadable root gives an empty list.
 */
std::vector<Notebook> scanNotebooks(const fs::path& root, int maxDepth = 6);

}  // namespace ambitus::library
