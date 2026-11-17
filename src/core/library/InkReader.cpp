#include "InkReader.h"

#include <cstdlib>  // for atoi, strtoul
#include <set>      // for set
#include <utility>  // for pair

#include <gio/gio.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include "util/PathUtil.h"          // for getExePath, fromGFilename, toGFilename
#include "util/StringUtils.h"       // for char_cast
#include "util/raii/GObjectSPtr.h"  // for GObjectSPtr

#include "SummaCourses.h"  // for summaConfigPath

namespace ambitus::library {

namespace {
constexpr auto BRIDGE_FILE = "ambitus-bridge.json";
constexpr int BRIDGE_VERSION = 1;
/// Pages in a row that fail before a run gives up (Summa answering, but not reading)
constexpr int FAILURES_IN_A_ROW = 3;

auto parseObject(const std::string& json, xoj::util::GObjectSPtr<JsonParser>& parser) -> JsonObject* {
    parser.reset(json_parser_new(), xoj::util::adopt);
    if (!json_parser_load_from_data(parser.get(), json.data(), static_cast<gssize>(json.size()), nullptr)) {
        return nullptr;
    }
    JsonNode* root = json_parser_get_root(parser.get());
    return root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : nullptr;
}

auto stringMember(JsonObject* obj, const char* name) -> std::optional<std::string> {
    if (!json_object_has_member(obj, name)) {
        return std::nullopt;
    }
    JsonNode* node = json_object_get_member(obj, name);
    if (!JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_STRING) {
        return std::nullopt;
    }
    return std::string(json_node_get_string(node));
}

auto stopFor(const std::string& reason) -> InkStop {
    static const std::pair<const char*, InkStop> reasons[] = {
            {"disabled", InkStop::Disabled}, {"no-model", InkStop::NoModel},          {"no-vision", InkStop::NoVision},
            {"free", InkStop::Free},         {"claude-unavailable", InkStop::Claude}, {"quota", InkStop::Quota},
            {"busy", InkStop::Busy},
    };
    for (auto& [name, stop]: reasons) {
        if (reason == name) {
            return stop;
        }
    }
    return InkStop::Failed;  // timeout, bad-request, forbidden, and what a newer Summa may add
}

auto readAll(GInputStream* in, GCancellable* cancel = nullptr) -> std::optional<std::string> {
    std::string data;
    char buf[8192];
    for (;;) {
        gssize n = g_input_stream_read(in, buf, sizeof(buf), cancel, nullptr);
        if (n < 0) {
            return std::nullopt;
        }
        if (n == 0) {
            return data;
        }
        data.append(buf, static_cast<size_t>(n));
    }
}

/// Status and body of an HTTP/1.x response (chunked or not)
auto parseResponse(const std::string& raw, int& status) -> std::optional<std::string> {
    const auto headEnd = raw.find("\r\n\r\n");
    if (raw.rfind("HTTP/1.", 0) != 0 || headEnd == std::string::npos) {
        return std::nullopt;
    }
    status = std::atoi(raw.c_str() + raw.find(' ') + 1);
    std::string head = raw.substr(0, headEnd);
    for (auto& c: head) {
        c = static_cast<char>(g_ascii_tolower(c));
    }
    std::string body = raw.substr(headEnd + 4);
    if (head.find("transfer-encoding: chunked") == std::string::npos) {
        return body;
    }
    std::string out;
    size_t at = 0;
    for (;;) {
        const auto lineEnd = body.find("\r\n", at);
        if (lineEnd == std::string::npos) {
            return std::nullopt;
        }
        const size_t size = std::strtoul(body.c_str() + at, nullptr, 16);
        if (size == 0) {
            return out;
        }
        if (lineEnd + 2 + size > body.size()) {
            return std::nullopt;
        }
        out.append(body, lineEnd + 2, size);
        at = lineEnd + 2 + size + 2;
    }
}

auto readFile(const fs::path& p) -> std::optional<std::string> {
    gchar* data = nullptr;
    gsize len = 0;
    if (!g_file_get_contents(Util::toGFilename(p).c_str(), &data, &len, nullptr)) {
        return std::nullopt;
    }
    std::string s(data, len);
    g_free(data);
    return s;
}
}  // namespace

auto summaBridgePath(const fs::path& configPath, const std::string& configJson, const char* envRuntime) -> fs::path {
    if (envRuntime && *envRuntime) {
        return Util::fromGFilename(envRuntime) / BRIDGE_FILE;
    }
    xoj::util::GObjectSPtr<JsonParser> parser;
    if (JsonObject* obj = configJson.empty() ? nullptr : parseObject(configJson, parser)) {
        if (auto runtime = stringMember(obj, "runtimePath"); runtime && !runtime->empty()) {
            return Util::fromGFilename(runtime->c_str()) / BRIDGE_FILE;
        }
    }
    return configPath.parent_path() / "runtime" / BRIDGE_FILE;
}

auto parseSummaBridge(const std::string& json) -> std::optional<SummaBridge> {
    xoj::util::GObjectSPtr<JsonParser> parser;
    JsonObject* obj = parseObject(json, parser);
    if (!obj || json_object_get_int_member_with_default(obj, "version", 0) != BRIDGE_VERSION) {
        return std::nullopt;
    }
    SummaBridge bridge;
    bridge.port = static_cast<int>(json_object_get_int_member_with_default(obj, "port", 0));
    bridge.token = stringMember(obj, "token").value_or("");
    if (bridge.port <= 0 || bridge.port > 65535 || bridge.token.empty() ||
        bridge.token.find_first_of("\r\n") != std::string::npos) {
        return std::nullopt;
    }
    return bridge;
}

auto parseInkReply(int status, const std::string& body) -> InkReply {
    InkReply reply;
    xoj::util::GObjectSPtr<JsonParser> parser;
    JsonObject* obj = parseObject(body, parser);
    if (status != 200 || !obj) {
        return reply;
    }
    if (auto text = stringMember(obj, "text")) {
        reply.stop = InkStop::None;
        reply.text = std::move(*text);
    } else if (auto reason = stringMember(obj, "reason")) {
        reply.stop = stopFor(*reason);
    }
    return reply;
}

namespace {
/// An answer of the bridge; connected false when nobody listens on the port
struct BridgeAnswer {
    bool connected = false;
    int status = 0;
    std::optional<std::string> body;
};

auto postBridge(const SummaBridge& bridge, const char* route, const char* type, const std::string& body,
                std::chrono::seconds timeout, const char* method = "POST", GCancellable* cancel = nullptr)
        -> BridgeAnswer {
    BridgeAnswer answer;
    xoj::util::GObjectSPtr<GSocketClient> client(g_socket_client_new(), xoj::util::adopt);
    g_socket_client_set_timeout(client.get(), static_cast<guint>(timeout.count()));
    xoj::util::GObjectSPtr<GSocketConnection> conn(
            g_socket_client_connect_to_host(client.get(), "127.0.0.1", static_cast<guint16>(bridge.port), cancel,
                                            nullptr),
            xoj::util::adopt);
    if (!conn) {
        return answer;
    }
    answer.connected = true;
    // HTTP/1.0: the answer comes whole and the connection closes after it
    const std::string typeHeader = *type ? std::string("\r\nContent-Type: ") + type : std::string();
    const std::string head = std::string(method) + " " + route +
                             " HTTP/1.0\r\nHost: 127.0.0.1:" + std::to_string(bridge.port) + typeHeader +
                             "\r\nContent-Length: " + std::to_string(body.size()) +
                             "\r\nx-summa-token: " + bridge.token + "\r\nConnection: close\r\n\r\n";
    GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn.get()));
    if (!g_output_stream_write_all(out, head.data(), head.size(), nullptr, cancel, nullptr) ||
        !g_output_stream_write_all(out, body.data(), body.size(), nullptr, cancel, nullptr)) {
        return answer;
    }
    auto raw = readAll(g_io_stream_get_input_stream(G_IO_STREAM(conn.get())), cancel);
    answer.body = raw ? parseResponse(*raw, answer.status) : std::nullopt;
    return answer;
}
}  // namespace

auto postInk(const SummaBridge& bridge, const std::string& png, std::chrono::seconds timeout) -> InkReply {
    auto answer = postBridge(bridge, "/ler-tinta", "image/png", png, timeout);
    if (!answer.connected) {
        return {InkStop::NoSumma, {}};
    }
    return answer.body ? parseInkReply(answer.status, *answer.body) : InkReply{};
}

auto parsePollReply(int status, const std::string& body) -> PollReply {
    PollReply reply;
    if (status == 204) {
        reply.kind = PollKind::Idle;
        return reply;
    }
    xoj::util::GObjectSPtr<JsonParser> parser;
    JsonObject* obj = status == 200 ? parseObject(body, parser) : nullptr;
    if (!obj) {
        return reply;
    }
    auto id = stringMember(obj, "pedido");
    if (id && !id->empty()) {
        reply.kind = PollKind::Request;
        reply.id = std::move(*id);
    }
    return reply;
}

auto pollPageRequest(const SummaBridge& bridge, GCancellable* cancel) -> PollReply {
    // Summa holds the poll for ~25 s: the socket waits longer, so a quiet minute is the 204 and not a timeout
    auto answer = postBridge(bridge, "/pedido-pagina", "", "", std::chrono::seconds(40), "GET", cancel);
    if (!answer.connected) {
        return {PollKind::NoSumma, {}};
    }
    return answer.body ? parsePollReply(answer.status, *answer.body) : PollReply{};
}

namespace {
auto jsonText(JsonBuilder* builder) -> std::string {
    xoj::util::GObjectSPtr<JsonGenerator> gen(json_generator_new(), xoj::util::adopt);
    JsonNode* root = json_builder_get_root(builder);
    json_generator_set_root(gen.get(), root);
    gchar* text = json_generator_to_data(gen.get(), nullptr);
    std::string out = text;
    g_free(text);
    json_node_unref(root);
    return out;
}

/// The member "recorte": the part of the page the PNG shows, in page points
void addCrop(JsonBuilder* builder, const PageCrop& crop) {
    json_builder_set_member_name(builder, "recorte");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "x");
    json_builder_add_double_value(builder, crop.x);
    json_builder_set_member_name(builder, "y");
    json_builder_add_double_value(builder, crop.y);
    json_builder_set_member_name(builder, "largura");
    json_builder_add_double_value(builder, crop.width);
    json_builder_set_member_name(builder, "altura");
    json_builder_add_double_value(builder, crop.height);
    json_builder_end_object(builder);
}
}  // namespace

auto pageAnswerJson(const std::string& requestId, const std::string& notebook, size_t page, const std::string& name,
                    const std::string& png, const std::optional<PageCrop>& crop) -> std::string {
    xoj::util::GObjectSPtr<JsonBuilder> builder(json_builder_new(), xoj::util::adopt);
    json_builder_begin_object(builder.get());
    json_builder_set_member_name(builder.get(), "pedido");
    json_builder_add_string_value(builder.get(), requestId.c_str());
    json_builder_set_member_name(builder.get(), "caminho");
    json_builder_add_string_value(builder.get(), notebook.c_str());
    json_builder_set_member_name(builder.get(), "pagina");
    json_builder_add_int_value(builder.get(), static_cast<gint64>(page + 1));
    json_builder_set_member_name(builder.get(), "nome");
    json_builder_add_string_value(builder.get(), name.c_str());
    if (crop && !png.empty()) {
        addCrop(builder.get(), *crop);
    }
    json_builder_set_member_name(builder.get(), "png");
    gchar* b64 = g_base64_encode(reinterpret_cast<const guchar*>(png.data()), png.size());
    json_builder_add_string_value(builder.get(), b64);
    g_free(b64);
    json_builder_end_object(builder.get());
    return jsonText(builder.get());
}

auto noPageAnswerJson(const std::string& requestId) -> std::string {
    xoj::util::GObjectSPtr<JsonBuilder> builder(json_builder_new(), xoj::util::adopt);
    json_builder_begin_object(builder.get());
    json_builder_set_member_name(builder.get(), "pedido");
    json_builder_add_string_value(builder.get(), requestId.c_str());
    json_builder_set_member_name(builder.get(), "erro");
    json_builder_add_string_value(builder.get(), "sem-pagina");
    json_builder_end_object(builder.get());
    return jsonText(builder.get());
}

auto postPageAnswer(const SummaBridge& bridge, const std::string& json, GCancellable* cancel) -> bool {
    auto answer =
            postBridge(bridge, "/pagina-atual", "application/json", json, std::chrono::seconds(10), "POST", cancel);
    return answer.connected && answer.status == 200;
}

auto pollBackoff(int failures) -> std::chrono::seconds {
    static constexpr int steps[] = {2, 5, 15, 30};
    const int at = failures < 1 ? 0 : (failures > 4 ? 3 : failures - 1);
    return std::chrono::seconds(steps[at]);
}

auto findSummaBridge() -> std::optional<SummaBridge> {
    const fs::path config = summaConfigPath();
    auto file = readFile(summaBridgePath(config, readFile(config).value_or(""), g_getenv("LEARNING_DESK_RUNTIME")));
    return file ? parseSummaBridge(*file) : std::nullopt;
}

auto parsePageReply(int status, const std::string& body) -> PageSend {
    if (status == 404) {
        return PageSend::OldSumma;  // a Summa from before the route
    }
    xoj::util::GObjectSPtr<JsonParser> parser;
    JsonObject* obj = parseObject(body, parser);
    if (status != 200 || !obj) {
        return PageSend::Failed;
    }
    if (json_object_get_boolean_member_with_default(obj, "ok", false)) {
        return PageSend::Sent;
    }
    auto reason = stringMember(obj, "reason").value_or("");
    if (reason == "not-found") {
        return PageSend::NotFound;
    }
    if (reason == "no-window") {
        return PageSend::NoWindow;
    }
    if (reason == "unavailable") {
        return PageSend::OldSumma;
    }
    return PageSend::Failed;
}

auto pageSendJson(const fs::path& notebook, size_t page, const std::string& name, const std::string& png,
                  const std::optional<PageCrop>& crop) -> std::string {
    xoj::util::GObjectSPtr<JsonBuilder> builder(json_builder_new(), xoj::util::adopt);
    json_builder_begin_object(builder.get());
    json_builder_set_member_name(builder.get(), "caminho");
    const std::string path(char_cast(notebook.u8string()));
    json_builder_add_string_value(builder.get(), path.c_str());
    json_builder_set_member_name(builder.get(), "pagina");
    json_builder_add_int_value(builder.get(), static_cast<gint64>(page + 1));
    json_builder_set_member_name(builder.get(), "nome");
    json_builder_add_string_value(builder.get(), name.c_str());
    if (crop && !png.empty()) {
        addCrop(builder.get(), *crop);
    }
    if (!png.empty()) {
        json_builder_set_member_name(builder.get(), "png");
        gchar* b64 = g_base64_encode(reinterpret_cast<const guchar*>(png.data()), png.size());
        json_builder_add_string_value(builder.get(), b64);
        g_free(b64);
    }
    json_builder_end_object(builder.get());
    return jsonText(builder.get());
}

auto sendPageToSumma(const SummaBridge& bridge, const fs::path& notebook, size_t page, const std::string& name,
                     const std::string& png, const std::optional<PageCrop>& crop, bool* cropSent) -> PageSend {
    if (cropSent) {
        *cropSent = false;
    }
    auto answer = postBridge(bridge, "/mandar-pagina", "application/json",
                             pageSendJson(notebook, page, name, png, crop), std::chrono::seconds(10));
    if (!answer.connected) {
        return PageSend::NoSumma;
    }
    auto result = answer.body ? parsePageReply(answer.status, *answer.body) : PageSend::Failed;
    if (result == PageSend::Failed && !png.empty() && answer.status == 400) {
        // a Summa from before the png field refuses the larger body: the path alone still works there (the whole page)
        return sendPageToSumma(bridge, notebook, page, name, std::string(), std::nullopt, cropSent);
    }
    if (cropSent) {
        *cropSent = result == PageSend::Sent && crop && !png.empty();
    }
    return result;
}

auto renderPagePng(const fs::path& notebook, size_t page) -> std::optional<std::string> {
    return renderPagePng(notebook, page, PAGE_DPI, false);
}

auto renderPagePng(const fs::path& notebook, size_t page, int dpi, bool ruling) -> std::optional<std::string> {
    gchar* dir = g_dir_make_tmp("ambitus-ink-XXXXXX", nullptr);
    if (!dir) {
        return std::nullopt;
    }
    const fs::path tmp = Util::fromGFilename(dir);
    g_free(dir);
    const fs::path out = tmp / "page.png";
#ifdef _WIN32
    const auto exe = Util::toGFilename(Util::getExePath() / "xournalpp.exe");
#else
    const auto exe = Util::toGFilename(Util::getExePath() / "xournalpp");
#endif
    const auto file = Util::toGFilename(notebook);
    const std::string range = "--export-range=" + std::to_string(page + 1);
    const auto img = Util::toGFilename(out);
    const std::string dpiArg = std::to_string(dpi);
    std::vector<const gchar*> argv = {exe.c_str(), file.c_str(),       range.c_str(), "--create-img",
                                      img.c_str(), "--export-png-dpi", dpiArg.c_str()};
    if (!ruling) {
        argv.push_back("--export-no-ruling");
    }
    argv.push_back(nullptr);
    // without a display, as Summa runs it: the export needs no window
    gchar** env = g_get_environ();
    env = g_environ_unsetenv(env, "DISPLAY");
    env = g_environ_unsetenv(env, "WAYLAND_DISPLAY");
    gint wait = 0;
    const bool ran = g_spawn_sync(nullptr, const_cast<gchar**>(argv.data()), env,
                                  static_cast<GSpawnFlags>(G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                                  nullptr, nullptr, nullptr, nullptr, &wait, nullptr);
    g_strfreev(env);
    std::optional<std::string> png;
    if (ran && g_spawn_check_wait_status(wait, nullptr)) {
        png = readFile(out);
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return png;
}

auto readInk(SearchIndex& index, const std::vector<fs::path>& order, std::chrono::steady_clock::time_point deadline,
             const std::atomic<bool>& cancel) -> InkRun {
    InkRun run;
    auto bridge = findSummaBridge();
    if (!bridge) {
        run.stop = InkStop::NoSumma;
        return run;
    }
    int failures = 0;
    std::set<std::pair<fs::path, size_t>> failed;  // tried in this run: not again until the next one
    while (!cancel && std::chrono::steady_clock::now() < deadline) {
        std::optional<SearchIndex::InkPage> next;
        for (auto& p: index.unreadInk(order, failed.size() + 1)) {
            if (!failed.count({p.notebook, p.page})) {
                next = std::move(p);
                break;
            }
        }
        if (!next) {
            return run;
        }
        auto png = renderPagePng(next->notebook, next->page);
        InkReply reply = png ? postInk(*bridge, *png, std::chrono::seconds(120)) : InkReply{};
        if (reply.stop == InkStop::None) {
            failures = 0;
            if (index.setInk(next->notebook, next->page, next->hash, std::move(reply.text))) {
                run.read++;
                index.save();
            }
            continue;
        }
        if (reply.stop != InkStop::Failed) {
            run.stop = reply.stop;
            return run;
        }
        failed.insert({next->notebook, next->page});
        if (++failures >= FAILURES_IN_A_ROW) {
            run.stop = InkStop::Failed;
            return run;
        }
    }
    return run;
}

}  // namespace ambitus::library
