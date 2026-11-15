/*
 * Ambitus
 *
 * Earlier versions of a notebook. Before a save writes over the file, the file
 * as it was is copied aside (at most one copy every ten minutes, the newest
 * thirty kept), in Ambitus's data folder: the notebook folder, that Summa reads,
 * gets nothing new and the .xopp stays as it is. A copy is read in the right
 * panel of the window (VersionsPanel), in place of the page, or put back in
 * place of the notebook (VersionsRestore).
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

/// The copies follow the notebook when the program moves or renames it: the folder of the copies goes from the key of
/// `from` to the key of `to` (copies already under the new key stay, and are not replaced). True when there were copies
/// and they are under the new key now.
bool followMove(const fs::path& root, const fs::path& from, const fs::path& to);

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

/// A day of the list of versions: the copies made on it, as indices into the list that was grouped
struct DayGroup {
    enum class Day { Today, Yesterday, Other };
    Day day = Day::Other;
    Clock::time_point first;  ///< the newest copy of the day, for the name of a day that is neither today nor yesterday
    std::vector<size_t> items;
};

/// The copies, newest first, grouped by the local day they were made on (newest day first); days without a copy are
/// left out
std::vector<DayGroup> groupByDay(const std::vector<Version>& newestFirst, Clock::time_point now);

/// Like "hoje, 14:32", "ontem, 09:10" or "3 de nov., 18:05", in local time
std::string describe(Clock::time_point when, Clock::time_point now);

}  // namespace ambitus::versions
