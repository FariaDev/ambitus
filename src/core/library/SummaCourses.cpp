#include "SummaCourses.h"

#include <cctype>  // for isxdigit

#include <glib.h>
#include <json-glib/json-glib.h>

#include "util/PathUtil.h"          // for readString, fromGFilename
#include "util/raii/GObjectSPtr.h"  // for GObjectSPtr

namespace ambitus::library {

namespace {
constexpr auto SUMMA_DATA_FOLDER = "Mesa de Estudos";

auto validColor(const std::string& c) -> bool {
    if (c.size() != 7 || c[0] != '#') {
        return false;
    }
    for (size_t i = 1; i < c.size(); i++) {
        if (!std::isxdigit(static_cast<unsigned char>(c[i]))) {
            return false;
        }
    }
    return true;
}

auto stringMember(JsonObject* obj, const char* name) -> std::string {
    if (!json_object_has_member(obj, name)) {
        return {};
    }
    JsonNode* node = json_object_get_member(obj, name);
    if (!JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_STRING) {
        return {};
    }
    const char* s = json_node_get_string(node);
    return s ? s : "";
}
}  // namespace

auto summaConfigPath() -> fs::path {
    if (const char* runtime = g_getenv("LEARNING_DESK_RUNTIME"); runtime && *runtime) {
        return Util::fromGFilename(runtime) / "config.json";
    }
#if defined(_WIN32)
    const char* appData = g_getenv("APPDATA");
    fs::path base =
            appData ? Util::fromGFilename(appData) : Util::fromGFilename(g_get_home_dir()) / "AppData" / "Roaming";
#elif defined(__APPLE__)
    fs::path base = Util::fromGFilename(g_get_home_dir()) / "Library" / "Application Support";
#else
    fs::path base = Util::fromGFilename(g_get_user_config_dir());
#endif
    return base / SUMMA_DATA_FOLDER / "config.json";
}

auto parseSummaCourses(const std::string& json) -> std::vector<Course> {
    std::vector<Course> courses;
    xoj::util::GObjectSPtr<JsonParser> parser(json_parser_new(), xoj::util::adopt);
    if (!json_parser_load_from_data(parser.get(), json.data(), static_cast<gssize>(json.size()), nullptr)) {
        return courses;
    }
    JsonNode* root = json_parser_get_root(parser.get());
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        return courses;
    }
    JsonObject* obj = json_node_get_object(root);
    if (!json_object_has_member(obj, "courses")) {
        return courses;
    }
    JsonNode* list = json_object_get_member(obj, "courses");
    if (!JSON_NODE_HOLDS_ARRAY(list)) {
        return courses;
    }
    JsonArray* arr = json_node_get_array(list);
    for (guint i = 0; i < json_array_get_length(arr); i++) {
        JsonNode* item = json_array_get_element(arr, i);
        if (!JSON_NODE_HOLDS_OBJECT(item)) {
            continue;
        }
        JsonObject* c = json_node_get_object(item);
        const auto path = stringMember(c, "path");
        Course course{stringMember(c, "id"), stringMember(c, "name"), fs::path(std::u8string(path.begin(), path.end())),
                      stringMember(c, "color")};
        if (course.id.empty() || course.name.empty() || course.path.empty()) {
            continue;
        }
        if (!validColor(course.color)) {
            course.color.clear();
        }
        courses.push_back(std::move(course));
    }
    return courses;
}

auto loadSummaCourses() -> std::vector<Course> {
    auto content = Util::readString(summaConfigPath(), false);
    if (!content) {
        return {};
    }
    return parseSummaCourses(*content);
}

}  // namespace ambitus::library
