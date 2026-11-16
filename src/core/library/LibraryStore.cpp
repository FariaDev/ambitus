#include "LibraryStore.h"

#include <algorithm>  // for find_if, remove_if
#include <cctype>     // for isxdigit

#include <glib.h>

#include "util/PathUtil.h"     // for toGFilename
#include "util/StringUtils.h"  // for char_cast

namespace ambitus::library {

namespace {
constexpr auto FOLDERS_GROUP = "folders";
constexpr auto FOLDERS_KEY = "paths";
constexpr auto COVER_PREFIX = "cover ";

auto utf8(const fs::path& p) -> std::string { return std::string(char_cast(p.u8string())); }
auto fromUtf8(const std::string& s) -> fs::path { return fs::path(std::u8string(s.begin(), s.end())); }

auto validColor(const std::string& c) -> bool {
    return c.size() == 7 && c[0] == '#' &&
           std::all_of(c.begin() + 1, c.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); });
}

struct KeyFile {
    GKeyFile* kf = g_key_file_new();
    ~KeyFile() { g_key_file_unref(kf); }
    auto str(const char* group, const char* key) const -> std::string {
        gchar* v = g_key_file_get_string(kf, group, key, nullptr);
        std::string s = v ? v : "";
        g_free(v);
        return s;
    }
};

constexpr CoverStyle ALL_STYLES[] = {
        CoverStyle::Contour, CoverStyle::Linen,      CoverStyle::Page,    CoverStyle::Plain,     CoverStyle::Field,
        CoverStyle::Waves,   CoverStyle::Sunflower,  CoverStyle::Truchet, CoverStyle::Lissajous, CoverStyle::Graph,
        CoverStyle::Mesh,    CoverStyle::Ford,       CoverStyle::Pascal,  CoverStyle::Ulam,      CoverStyle::Hilbert,
        CoverStyle::Surface, CoverStyle::Pythagoras, CoverStyle::Rose,    CoverStyle::Fibonacci, CoverStyle::Blocks,
        CoverStyle::Relief,  CoverStyle::Julia};
}  // namespace

auto coverStyleToString(CoverStyle s) -> const char* {
    switch (s) {
        case CoverStyle::Contour:
            return "contour";
        case CoverStyle::Linen:
            return "linen";
        case CoverStyle::Page:
            return "page";
        case CoverStyle::Plain:
            return "plain";
        case CoverStyle::Field:
            return "field";
        case CoverStyle::Waves:
            return "waves";
        case CoverStyle::Sunflower:
            return "sunflower";
        case CoverStyle::Truchet:
            return "truchet";
        case CoverStyle::Lissajous:
            return "lissajous";
        case CoverStyle::Graph:
            return "graph";
        case CoverStyle::Mesh:
            return "mesh";
        case CoverStyle::Ford:
            return "ford";
        case CoverStyle::Pascal:
            return "pascal";
        case CoverStyle::Ulam:
            return "ulam";
        case CoverStyle::Hilbert:
            return "hilbert";
        case CoverStyle::Surface:
            return "surface";
        case CoverStyle::Pythagoras:
            return "pythagoras";
        case CoverStyle::Rose:
            return "rose";
        case CoverStyle::Fibonacci:
            return "fibonacci";
        case CoverStyle::Blocks:
            return "blocks";
        case CoverStyle::Relief:
            return "relief";
        case CoverStyle::Julia:
            return "julia";
    }
    return "contour";
}

auto coverStyleFromString(const std::string& s) -> std::optional<CoverStyle> {
    for (auto style: ALL_STYLES) {
        if (s == coverStyleToString(style)) {
            return style;
        }
    }
    return std::nullopt;
}

LibraryStore::LibraryStore(fs::path f): file(std::move(f)) {
    KeyFile k;
    if (!g_key_file_load_from_file(k.kf, Util::toGFilename(file).c_str(), G_KEY_FILE_NONE, nullptr)) {
        return;
    }
    gsize n = 0;
    if (gchar** paths = g_key_file_get_string_list(k.kf, FOLDERS_GROUP, FOLDERS_KEY, &n, nullptr)) {
        for (gsize i = 0; i < n; i++) {
            addFolder(fromUtf8(paths[i]));
        }
        g_strfreev(paths);
    }
    gchar** groups = g_key_file_get_groups(k.kf, &n);
    for (gsize i = 0; i < n; i++) {
        std::string group = groups[i];
        if (group.rfind(COVER_PREFIX, 0) != 0) {
            continue;
        }
        Cover c;
        if (auto style = coverStyleFromString(k.str(groups[i], "style"))) {
            c.style = *style;
        }
        if (auto color = k.str(groups[i], "color"); validColor(color)) {
            c.color = color;
        }
        c.seed = static_cast<unsigned>(g_key_file_get_uint64(k.kf, groups[i], "seed", nullptr));
        c.image = fromUtf8(k.str(groups[i], "image"));
        if (auto path = k.str(groups[i], "path"); !path.empty()) {
            covers[fromUtf8(path)] = c;
        }
    }
    g_strfreev(groups);
}

auto LibraryStore::addFolder(const fs::path& path) -> bool {
    if (path.empty() || std::any_of(folderList.begin(), folderList.end(), [&](auto& f) { return f.path == path; })) {
        return false;
    }
    auto name = utf8(path.filename().empty() ? path.parent_path().filename() : path.filename());
    folderList.push_back({name, path});
    return true;
}

auto LibraryStore::removeFolder(const fs::path& path) -> bool {
    auto it = std::remove_if(folderList.begin(), folderList.end(), [&](auto& f) { return f.path == path; });
    bool removed = it != folderList.end();
    folderList.erase(it, folderList.end());
    return removed;
}

auto LibraryStore::cover(const fs::path& notebook) const -> Cover {
    auto it = covers.find(notebook);
    return it == covers.end() ? Cover{} : it->second;
}

void LibraryStore::setCover(const fs::path& notebook, const Cover& c) {
    if (c == Cover{}) {
        covers.erase(notebook);
    } else {
        covers[notebook] = c;
    }
}

void LibraryStore::moveCover(const fs::path& from, const fs::path& to) {
    auto it = covers.find(from);
    if (it == covers.end()) {
        return;
    }
    covers[to] = it->second;
    covers.erase(from);
}

auto LibraryStore::save() const -> bool {
    KeyFile k;
    std::vector<std::string> paths;
    for (auto& f: folderList) {
        paths.push_back(utf8(f.path));
    }
    std::vector<const gchar*> cpaths;
    for (auto& p: paths) {
        cpaths.push_back(p.c_str());
    }
    g_key_file_set_string_list(k.kf, FOLDERS_GROUP, FOLDERS_KEY, cpaths.data(), cpaths.size());
    // Group names cannot hold '[' or ']', so the path goes in a key and the groups are numbered.
    size_t n = 0;
    for (auto& [path, c]: covers) {
        const auto group = COVER_PREFIX + std::to_string(++n);
        g_key_file_set_string(k.kf, group.c_str(), "path", utf8(path).c_str());
        g_key_file_set_string(k.kf, group.c_str(), "style", coverStyleToString(c.style));
        if (!c.color.empty()) {
            g_key_file_set_string(k.kf, group.c_str(), "color", c.color.c_str());
        }
        if (c.seed) {
            g_key_file_set_uint64(k.kf, group.c_str(), "seed", c.seed);
        }
        if (!c.image.empty()) {
            g_key_file_set_string(k.kf, group.c_str(), "image", utf8(c.image).c_str());
        }
    }
    gsize len = 0;
    gchar* data = g_key_file_to_data(k.kf, &len, nullptr);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    bool ok = g_file_set_contents(Util::toGFilename(file).c_str(), data, static_cast<gssize>(len), nullptr);
    g_free(data);
    return ok;
}

}  // namespace ambitus::library
