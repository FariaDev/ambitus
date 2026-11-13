/*
 * Ambitus
 *
 * Search inside notebooks: the typed text and the LaTeX source of every page,
 * and the handwriting once Summa has read it (see InkReader.h). The index lives
 * in the cache folder and is rebuilt only for notebooks that changed; nothing is
 * written to the .xopp.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <utility>  // for pair
#include <vector>

#include "filesystem.h"  // for path

namespace ambitus::library {

struct PageText {
    std::vector<std::string> texts;  ///< text boxes, in file order
    std::vector<std::string> latex;  ///< LaTeX source of the formulas
    bool board = false;              ///< the background carries the board mark (see ambitus/Board.h)
    /// fingerprint of the strokes of the page (SHA-1 of their attributes and points); empty without strokes
    std::string inkHash;
    /// the handwriting as Summa read it, for the strokes of inkHash; empty until read
    std::string ink;
    bool inkRead = false;  ///< ink holds the reading of inkHash (it may be empty: nothing legible)
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
    bool ink = false;  ///< found in the handwriting read by Summa
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

    /// A page with strokes not read yet
    struct InkPage {
        fs::path notebook;
        size_t page = 0;  ///< 0-based
        std::string hash;
    };
    /// Pages whose strokes were not read yet, in the order of the notebooks given; at most limit
    std::vector<InkPage> unreadInk(const std::vector<fs::path>& order, size_t limit) const;
    /// Keeps what Summa read on a page; false (nothing kept) when the page changed since (another hash)
    bool setInk(const fs::path& notebook, size_t page, const std::string& hash, std::string text);
    /// Pages with strokes, and how many of them were read
    std::pair<size_t, size_t> inkProgress() const;

    /// Whether the notebook is a board: one page, with the board mark (false when not indexed)
    bool isBoard(const fs::path& notebook) const;

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
