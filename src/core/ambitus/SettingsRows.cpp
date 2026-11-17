/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "SettingsRows.h"

#include <sstream>

#include <json-glib/json-glib.h>

#include "ambitus/Commands.h"
#include "util/raii/GObjectSPtr.h"

namespace ambitus::settingsrows {

bool matches(const std::string& query, const std::string& title, const std::string& description,
             const std::string& section) {
    const std::string text = foldForSearch(title + " " + description + " " + section);
    std::istringstream words(foldForSearch(query));
    for (std::string w; words >> w;) {
        if (text.find(w) == std::string::npos) {
            return false;
        }
    }
    return true;
}

std::string shownPath(const std::string& path, const std::string& home) {
    if (home.empty()) {
        return path;
    }
    if (path == home) {
        return "~";
    }
    if (path.size() > home.size() && path.compare(0, home.size(), home) == 0 && path[home.size()] == '/') {
        return "~" + path.substr(home.size());
    }
    return path;
}

std::string vaultPathOf(const std::string& json) {
    xoj::util::GObjectSPtr<JsonParser> parser(json_parser_new(), xoj::util::adopt);
    if (!json_parser_load_from_data(parser.get(), json.data(), static_cast<gssize>(json.size()), nullptr)) {
        return {};
    }
    JsonNode* root = json_parser_get_root(parser.get());
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        return {};
    }
    JsonObject* obj = json_node_get_object(root);
    if (!json_object_has_member(obj, "vaultPath")) {
        return {};
    }
    JsonNode* node = json_object_get_member(obj, "vaultPath");
    if (!JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_STRING) {
        return {};
    }
    const char* s = json_node_get_string(node);
    return s ? s : "";
}

namespace {
struct PaperId {
    newnotebook::Paper paper;
    const char* id;
};
constexpr PaperId PAPER_IDS[] = {
        {newnotebook::Paper::SUMMA, "summa"}, {newnotebook::Paper::BLUE_RULED, "ruled"},
        {newnotebook::Paper::GRID, "grid"},   {newnotebook::Paper::DOTTED, "dotted"},
        {newnotebook::Paper::PLAIN, "plain"},
};
}  // namespace

const char* paperId(newnotebook::Paper paper) {
    for (auto const& p: PAPER_IDS) {
        if (p.paper == paper) {
            return p.id;
        }
    }
    return "summa";
}

std::optional<newnotebook::Paper> paperFromId(const std::string& id) {
    for (auto const& p: PAPER_IDS) {
        if (id == p.id) {
            return p.paper;
        }
    }
    return std::nullopt;
}

const char* sizeId(newnotebook::PageSize size) { return size == newnotebook::PageSize::LETTER ? "letter" : "a4"; }

std::optional<newnotebook::PageSize> sizeFromId(const std::string& id) {
    if (id == "a4") {
        return newnotebook::PageSize::A4;
    }
    if (id == "letter") {
        return newnotebook::PageSize::LETTER;
    }
    return std::nullopt;
}

const char* draftSizeId(newnotebook::PageSize size) {
    return size == newnotebook::PageSize::INFINITE ? "infinite" : sizeId(size);
}

std::optional<newnotebook::PageSize> draftSizeFromId(const std::string& id) {
    if (id == "infinite") {
        return newnotebook::PageSize::INFINITE;
    }
    return sizeFromId(id);
}

}  // namespace ambitus::settingsrows
