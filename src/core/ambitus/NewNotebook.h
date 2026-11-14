/*
 * Ambitus
 *
 * The logic of the "New notebook" dialog (docs/design/r5, section 3), apart from the widgets: the papers it offers and
 * what each one is in the .xopp, the page sizes, how a name is checked and how one is suggested. The papers are only
 * background types and colours that Xournal++ already knows, so the file opens there as any other.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "model/PageType.h"

namespace ambitus::newnotebook {

/// The papers of the dialog, in the order of the cards
enum class Paper { SUMMA, BLUE_RULED, GRID, DOTTED, PLAIN };
constexpr Paper DEFAULT_PAPER = Paper::SUMMA;
inline const std::vector<Paper>& allPapers() {
    static const std::vector<Paper> all{Paper::SUMMA, Paper::BLUE_RULED, Paper::GRID, Paper::DOTTED, Paper::PLAIN};
    return all;
}

/// The size of the page; INFINITE is the board of Ambitus (one page that grows)
enum class PageSize { A4, LETTER, INFINITE };
constexpr PageSize DEFAULT_SIZE = PageSize::A4;

/// The background of the first page for a paper: the format and its config line, as saved in the .xopp
PageType paperType(Paper paper);

/// Width and height of a page in points; for INFINITE, the size a board starts with
struct Dimensions {
    double width, height;
};
Dimensions pageDimensions(PageSize size);

/// The config line of the paper of the Summa (grey rules, soft violet margin; the dark pair is for dark paper). It is
/// also what a new page gets by default (Settings).
constexpr char SUMMA_CONFIG[] = "f1=c6c6c6,f2=b89cea,af1=393939,af2=b89cea";

/// Why a name cannot be used
enum class NameCheck { OK, EMPTY, EXISTS, INVALID };

/// What the user typed, as the file name goes: spaces trimmed from both ends and a ".xopp" at the end dropped
std::string cleanName(const std::string& typed);

/// OK when the cleaned name can be a file name here. `exists` tells whether a (cleaned) name is taken in the folder.
/// INVALID: a path separator, a control character, or a leading dot (the library skips hidden files).
NameCheck checkName(const std::string& typed, const std::function<bool(const std::string&)>& exists);

/// The name to start with. When the newest notebook of the folder ends in a number ("Limits - class 3") the next one
/// ("Limits - class 4"); otherwise `fallback`, with " 2", " 3"... until it is free. Never a name that `exists`.
std::string suggestName(const std::string& newest, const std::string& fallback,
                        const std::function<bool(const std::string&)>& exists);

}  // namespace ambitus::newnotebook
