#include "NotebookScanner.h"

#include <algorithm>     // for sort
#include <chrono>        // for system_clock
#include <set>           // for set
#include <system_error>  // for error_code

#include "util/StringUtils.h"  // for char_cast

namespace ambitus::library {

namespace {
auto isInside(const fs::path& child, const fs::path& root) -> bool {
    auto c = child.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++c) {
        if (c == child.end() || *c != *r) {
            return false;
        }
    }
    return true;
}

auto utf8(const fs::path& p) -> std::string { return std::string(char_cast(p.u8string())); }

auto toTimeT(const fs::file_time_type& t) -> std::time_t {
    auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() +
                                                                                 std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sys);
}

struct Walk {
    fs::path root;
    int maxDepth;
    std::set<fs::path> seenDirs;
    std::set<fs::path> seenFiles;
    std::vector<Notebook> out;

    void dir(const fs::path& real, const fs::path& relative, int depth) {
        if (depth > maxDepth || !seenDirs.insert(real).second) {
            return;
        }
        std::error_code ec;
        fs::directory_iterator it(real, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            return;
        }
        for (const auto& entry: it) {
            const auto name = utf8(entry.path().filename());
            if (name.empty() || name[0] == '.') {
                continue;
            }
            auto canonical = fs::canonical(entry.path(), ec);
            if (ec || !isInside(canonical, root)) {
                ec.clear();
                continue;
            }
            if (fs::is_directory(canonical, ec)) {
                dir(canonical, relative / entry.path().filename(), depth + 1);
                continue;
            }
            if (!fs::is_regular_file(canonical, ec) || entry.path().extension() != ".xopp") {
                continue;
            }
            const auto stem = utf8(entry.path().stem());
            if (stem.size() >= 9 && stem.compare(stem.size() - 9, 9, ".autosave") == 0) {
                continue;
            }
            if (!seenFiles.insert(canonical).second) {
                continue;
            }
            Notebook nb;
            nb.path = canonical;
            nb.name = stem;
            nb.folder = std::string(char_cast(relative.generic_u8string()));
            nb.modified = toTimeT(fs::last_write_time(canonical, ec));
            out.push_back(std::move(nb));
        }
    }
};
}  // namespace

auto scanNotebooks(const fs::path& root, int maxDepth) -> std::vector<Notebook> {
    std::error_code ec;
    auto realRoot = fs::canonical(root, ec);
    if (ec || !fs::is_directory(realRoot, ec)) {
        return {};
    }
    Walk walk{realRoot, maxDepth, {}, {}, {}};
    walk.dir(realRoot, fs::path(), 0);
    std::sort(walk.out.begin(), walk.out.end(), [](const Notebook& a, const Notebook& b) {
        return a.modified != b.modified ? a.modified > b.modified : a.path < b.path;
    });
    return std::move(walk.out);
}

}  // namespace ambitus::library
