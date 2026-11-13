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

std::vector<size_t> matchCommands(const std::vector<Command>& commands, const std::string& query, size_t limit) {
    const auto wanted = words(foldForSearch(query));
    std::vector<std::pair<int, size_t>> scored;  // (score, index)
    for (size_t i = 0; i < commands.size(); i++) {
        const std::string label = foldForSearch(commands[i].label);
        const std::string path = foldForSearch(commands[i].path);
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
            } else if (path.find(w) == std::string::npos) {
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

}  // namespace ambitus
