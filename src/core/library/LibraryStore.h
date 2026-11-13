/*
 * Ambitus
 *
 * What the library keeps on its own: the folders added by hand and the cover
 * of each notebook. Stored as a key file (library.ini) in the config folder,
 * never inside the .xopp, so the notebooks stay plain Xournal++ files.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "filesystem.h"  // for path

namespace ambitus::library {

enum class CoverStyle { Contour, Linen, Page, Plain };

const char* coverStyleToString(CoverStyle s);
std::optional<CoverStyle> coverStyleFromString(const std::string& s);

struct Cover {
    CoverStyle style = CoverStyle::Contour;
    /// "#rrggbb"; empty means the colour of the course (or the default grey)
    std::string color;
    /// 0 draws the pattern from the notebook name; "Outro desenho" picks another seed
    unsigned seed = 0;
    /// own image instead of a drawn cover; empty when unused
    fs::path image;

    bool operator==(const Cover&) const = default;
};

struct Folder {
    std::string name;
    fs::path path;
};

class LibraryStore {
public:
    /// Loads file if it exists; an unreadable file starts empty (and is only overwritten on save()).
    explicit LibraryStore(fs::path file);

    const std::vector<Folder>& folders() const { return folderList; }
    /// Adds a folder; the name is the last path component. Returns false if it was already there.
    bool addFolder(const fs::path& path);
    bool removeFolder(const fs::path& path);

    /// Cover of a notebook, default cover when none was chosen.
    Cover cover(const fs::path& notebook) const;
    void setCover(const fs::path& notebook, const Cover& cover);
    /// Keeps the cover when a notebook is renamed or moved inside the library.
    void moveCover(const fs::path& from, const fs::path& to);

    /// Writes the file atomically (g_file_set_contents). Returns false on failure.
    bool save() const;

private:
    fs::path file;
    std::vector<Folder> folderList;
    std::map<fs::path, Cover> covers;
};

}  // namespace ambitus::library
