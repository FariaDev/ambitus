/*
 * Ambitus
 *
 * Earlier versions of a notebook. Before a save writes over the file, the file
 * as it was is copied aside (at most one copy every ten minutes, the newest
 * thirty kept), in Ambitus's data folder: the notebook folder, that Summa reads,
 * gets nothing new and the .xopp stays as it is. A copy can be opened in a tab
 * of its own or put back in place of the notebook.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "filesystem.h"

namespace ambitus::versions {

using Clock = std::chrono::system_clock;

struct Version {
    fs::path file;
    Clock::time_point when;  ///< when the copy was made (a second's precision)
    std::uintmax_t size = 0;
};

/// The folder of the copies of a notebook under root: one per notebook path
fs::path folderFor(const fs::path& root, const fs::path& notebook);

/// Copies the notebook as it is on disk now, unless it does not exist or the newest copy is younger than gap
/// (gap 0 always copies); then keeps only the newest keep. True when a copy was made.
bool keep(const fs::path& root, const fs::path& notebook, Clock::time_point now, std::chrono::seconds gap,
          size_t keepCount);

/// The copies of a notebook, newest first
std::vector<Version> list(const fs::path& root, const fs::path& notebook);

/// Where the copies are: "versions" in Ambitus's data folder
fs::path defaultRoot();

/// What SaveJob calls before writing: keep() with ten minutes and thirty copies; never fails the save
void keepBeforeSave(const fs::path& notebook);

/// Like "hoje, 14:32", "ontem, 09:10" or "3 de nov., 18:05", in local time
std::string describe(Clock::time_point when, Clock::time_point now);

}  // namespace ambitus::versions
