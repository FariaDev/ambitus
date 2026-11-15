#include "SummaState.h"

#include <glib.h>
#include <json-glib/json-glib.h>

#include "util/PathUtil.h"          // for readString
#include "util/raii/GObjectSPtr.h"  // for GObjectSPtr

#include "InkReader.h"     // for summaBridgePath
#include "SummaCourses.h"  // for summaConfigPath

namespace ambitus::library {

namespace {
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

auto parseSummaState(const std::string& json) -> SummaState {
    SummaState state;
    xoj::util::GObjectSPtr<JsonParser> parser(json_parser_new(), xoj::util::adopt);
    if (!json_parser_load_from_data(parser.get(), json.data(), static_cast<gssize>(json.size()), nullptr)) {
        return state;
    }
    JsonNode* root = json_parser_get_root(parser.get());
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        return state;
    }
    JsonObject* obj = json_node_get_object(root);
    const std::string language = stringMember(obj, "uiLanguage");
    if (language == "pt-BR" || language == "en") {
        state.language = language;
    }
    const std::string accent = stringMember(obj, "accent");
    if (accent == "graphite" || accent == "violet") {
        state.accent = accent;
    }
    return state;
}

auto summaStatePath(const fs::path& configPath, const std::string& configJson, const char* envRuntime) -> fs::path {
    // desk.json lives in the same runtime folder as the bridge of the handwriting
    return summaBridgePath(configPath, configJson, envRuntime).parent_path() / "desk.json";
}

auto loadSummaState() -> SummaState {
    const fs::path configPath = summaConfigPath();
    const auto config = Util::readString(configPath, false);
    const auto state =
            Util::readString(summaStatePath(configPath, config.value_or(""), g_getenv("LEARNING_DESK_RUNTIME")), false);
    return state ? parseSummaState(*state) : SummaState{};
}

}  // namespace ambitus::library
