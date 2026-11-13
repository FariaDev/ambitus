/*
 * Ambitus
 *
 * Search inside notebooks: the typed text and the LaTeX source of every page.
 * Handwriting is not searched. The index lives in the cache folder and is
 * rebuilt only for notebooks that changed; nothing is written to the .xopp.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "filesystem.h"  // for path

namespace ambitus::library {

struct PageText {
    std::vector<std::string> texts;  ///< text boxes, in file order
    std::vector<std::string> latex;  ///< LaTeX source of the formulas
    bool operator==(const PageText&) const = default;
};

/// Reads the text of every page of a .xopp (gzip or zip). Empty optional when the file cannot be read.
std::optional<std::vector<PageText>> extractNotebookText(const fs::path& file);

/// Case and accent insensitive form of a string, with the byte offset in the original of every folded byte.
struct Folded {
    std::string text;
    std::vector<size_t> start;  ///< start[i]: offset in the original of the character that gave byte i
    std::vector<size_t> end;    ///< end[i]: offset just after that character
};
Folded foldForSearch(const std::string& s);

struct SearchHit {
    fs::path notebook;
    size_t page = 0;  ///< 0-based
    bool latex = false;
    /// excerpt split around the match, whitespace collapsed; before/after may start/end with "…"
    std::string before, match, after;
};

class SearchIndex {
public:
    /// Loads cacheFile if it can; an unreadable cache starts empty.
    explicit SearchIndex(fs::path cacheFile);

    /// Re-reads the notebooks that changed (by mtime and size), adds new ones, drops the ones not listed.
    /// Returns how many notebooks were read again.
    size_t update(const std::vector<fs::path>& notebooks);
    bool save() const;

    /// Hits in the order of the notebooks given (then page, then position); at most limit.
    std::vector<SearchHit> search(const std::string& query, const std::vector<fs::path>& order,
                                  size_t limit = 200) const;

    size_t size() const { return entries.size(); }

private:
    struct Entry {
        std::time_t mtime = 0;
        long long size = -1;
        std::vector<PageText> pages;
    };
    fs::path file;
    std::map<fs::path, Entry> entries;
};

}  // namespace ambitus::library
