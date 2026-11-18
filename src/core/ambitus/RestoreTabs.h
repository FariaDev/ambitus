/*
 * Ambitus
 *
 * The list of tabs to open at startup, saved as one setting. Paths may hold any
 * character but NUL: backslash, tab, newline and carriage return are escaped,
 * so a name with spaces, quotes, accents or even a newline survives the trip.
 * The last field of a line is 1 for the tab in the window, 2 for the tab shown
 * beside it, 0 for the others. Lists saved before the side pane only have 0 and 1.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "filesystem.h"

namespace ambitus::restoretabs {

struct SavedTab {
    fs::path path;
    int page = 0;
    bool active = false;
    /// the notebook shown beside the one in the window
    bool beside = false;

    bool operator==(const SavedTab& o) const {
        return path == o.path && page == o.page && active == o.active && beside == o.beside;
    }
};

namespace detail {
inline std::string escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c: text) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
        }
    }
    return out;
}

inline std::string unescape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char n = text[++i];
            switch (n) {
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                default:
                    out += n;
            }
        } else {
            out += text[i];
        }
    }
    return out;
}
}  // namespace detail

/// One tab per line: escaped path, page, and whether it was the active one (or the one beside), separated by tabs.
inline std::string serialize(const std::vector<SavedTab>& tabs) {
    std::string out;
    for (auto const& tab: tabs) {
        out += detail::escape(tab.path.string());
        out += '\t';
        out += std::to_string(tab.page);
        out += '\t';
        out += tab.beside ? '2' : (tab.active ? '1' : '0');
        out += '\n';
    }
    return out;
}

/// Malformed lines are ignored; a bad page reads as zero.
inline std::vector<SavedTab> deserialize(std::string_view text) {
    std::vector<SavedTab> out;
    size_t at = 0;
    while (at < text.size()) {
        size_t eol = text.find('\n', at);
        if (eol == std::string_view::npos) {
            eol = text.size();
        }
        const std::string_view line = text.substr(at, eol - at);
        at = eol + 1;
        const size_t first = line.find('\t');
        if (first == std::string_view::npos) {
            continue;
        }
        const size_t second = line.find('\t', first + 1);
        if (second == std::string_view::npos) {
            continue;
        }
        int page = 0;
        try {
            page = std::stoi(std::string(line.substr(first + 1, second - first - 1)));
        } catch (...) {
            page = 0;
        }
        const std::string_view flag = line.substr(second + 1);
        out.push_back({fs::path(detail::unescape(line.substr(0, first))), page, flag == "1", flag == "2"});
    }
    return out;
}

}  // namespace ambitus::restoretabs
