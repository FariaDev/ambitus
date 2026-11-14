/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "Commands.h"

#include <algorithm>
#include <sstream>

#include <glib.h>

#include "util/raii/GObjectSPtr.h"

namespace ambitus {

namespace {
std::string stringAttribute(GMenuModel* menu, int item, const char* name) {
    gchar* value = nullptr;
    if (!g_menu_model_get_item_attribute(menu, item, name, "s", &value)) {
        return {};
    }
    std::string out = value;
    g_free(value);
    return out;
}

/// "_Editar" → "Editar", "A__B" → "A_B"
std::string withoutMnemonic(const std::string& label) {
    std::string out;
    for (size_t i = 0; i < label.size(); i++) {
        if (label[i] == '_') {
            if (i + 1 < label.size() && label[i + 1] == '_') {
                out += '_';
                i++;
            }
            continue;
        }
        out += label[i];
    }
    return out;
}

void collect(GMenuModel* menu, const std::string& path, std::vector<Command>& out) {
    const int n = g_menu_model_get_n_items(menu);
    for (int i = 0; i < n; i++) {
        const std::string label = withoutMnemonic(stringAttribute(menu, i, G_MENU_ATTRIBUTE_LABEL));
        const std::string action = stringAttribute(menu, i, G_MENU_ATTRIBUTE_ACTION);
        if (!action.empty() && !label.empty()) {
            Command c;
            c.label = label;
            c.path = path;
            c.action = action;
            c.target = xoj::util::GVariantSPtr(
                    g_menu_model_get_item_attribute_value(menu, i, G_MENU_ATTRIBUTE_TARGET, nullptr), xoj::util::adopt);
            c.accel = stringAttribute(menu, i, "accel");
            out.push_back(std::move(c));
        }
        xoj::util::GObjectSPtr<GMenuLinkIter> it(g_menu_model_iterate_item_links(menu, i), xoj::util::adopt);
        while (g_menu_link_iter_next(it.get())) {
            xoj::util::GObjectSPtr<GMenuModel> sub(g_menu_link_iter_get_value(it.get()), xoj::util::adopt);
            const bool isSubmenu = std::string(g_menu_link_iter_get_name(it.get())) == G_MENU_LINK_SUBMENU;
            collect(sub.get(), isSubmenu && !label.empty() ? (path.empty() ? label : path + " › " + label) : path, out);
        }
    }
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string w; in >> w;) {
        out.push_back(w);
    }
    return out;
}
}  // namespace

std::vector<Command> collectCommands(GMenuModel* menu) {
    std::vector<Command> out;
    if (menu) {
        collect(menu, "", out);
    }
    return out;
}

std::string foldForSearch(const std::string& text) {
    gchar* ascii = g_str_to_ascii(text.c_str(), "C");
    gchar* lower = g_ascii_strdown(ascii, -1);
    std::string out = lower;
    g_free(lower);
    g_free(ascii);
    return out;
}

std::vector<size_t> rankCandidates(const std::vector<Candidate>& candidates, const std::string& query, size_t limit) {
    const auto wanted = words(foldForSearch(query));
    std::vector<std::pair<int, size_t>> scored;  // (score, index)
    for (size_t i = 0; i < candidates.size(); i++) {
        const std::string label = foldForSearch(candidates[i].label);
        const std::string extra = foldForSearch(candidates[i].extra);
        int score = 0;
        bool all = true;
        for (auto const& w: wanted) {
            const auto at = label.find(w);
            if (at == 0) {
                score += 3;
            } else if (label.find(" " + w) != std::string::npos) {
                score += 2;
            } else if (at != std::string::npos) {
                score += 1;
            } else if (extra.find(w) == std::string::npos) {
                all = false;
                break;
            }
        }
        if (all) {
            scored.emplace_back(score, i);
        }
    }
    std::stable_sort(scored.begin(), scored.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    std::vector<size_t> out;
    for (auto const& [score, i]: scored) {
        if (out.size() == limit) {
            break;
        }
        out.push_back(i);
    }
    return out;
}

std::vector<size_t> matchCommands(const std::vector<Command>& commands, const std::string& query, size_t limit) {
    std::vector<Candidate> candidates;
    candidates.reserve(commands.size());
    for (auto const& c: commands) {
        candidates.push_back({c.label, c.path});
    }
    return rankCandidates(candidates, query, limit);
}

std::vector<std::pair<size_t, size_t>> highlightRanges(const std::string& text, const std::string& query) {
    // the text folded one character at a time, so that every byte of it knows the characters of `text` it came from
    std::string folded;
    std::vector<size_t> begin, end;
    for (const gchar* p = text.c_str(); *p;) {
        const gchar* next = g_utf8_next_char(p);
        const size_t from = static_cast<size_t>(p - text.c_str());
        const size_t to = static_cast<size_t>(next - text.c_str());
        for (char c: foldForSearch(std::string(p, next))) {
            folded += c;
            begin.push_back(from);
            end.push_back(std::min(to, text.size()));
        }
        p = next;
    }
    std::vector<std::pair<size_t, size_t>> found;
    for (auto const& w: words(foldForSearch(query))) {
        const auto at = folded.find(w);
        if (at != std::string::npos) {
            found.emplace_back(begin[at], end[at + w.size() - 1]);
        }
    }
    std::sort(found.begin(), found.end());
    std::vector<std::pair<size_t, size_t>> out;
    for (auto const& r: found) {
        if (!out.empty() && r.first <= out.back().second) {
            out.back().second = std::max(out.back().second, r.second);
        } else {
            out.push_back(r);
        }
    }
    return out;
}

std::vector<PaletteRow> arrangePalette(const std::vector<Candidate>& commands, const std::vector<Candidate>& notebooks,
                                       const std::vector<size_t>& recent, const std::string& query,
                                       const PaletteLimits& limits) {
    std::vector<PaletteRow> rows;
    if (words(query).empty()) {
        for (size_t i = 0; i < recent.size() && i < limits.recent; i++) {
            rows.push_back({PaletteSection::Recent, recent[i]});
        }
        if (rows.empty()) {
            for (size_t i = 0; i < commands.size() && i < limits.commands; i++) {
                rows.push_back({PaletteSection::Commands, i});
            }
        }
        return rows;
    }
    for (size_t i: rankCandidates(commands, query, limits.commands)) {
        rows.push_back({PaletteSection::Commands, i});
    }
    for (size_t i: rankCandidates(notebooks, query, limits.notebooks)) {
        rows.push_back({PaletteSection::Notebooks, i});
    }
    return rows;
}

}  // namespace ambitus
