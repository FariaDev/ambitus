#include "NotebookSearch.h"

#include <algorithm>  // for min
#include <array>      // for array
#include <cstring>    // for strcmp
#include <memory>     // for unique_ptr

#include <glib.h>
#include <glib/gstdio.h>          // for g_stat
#include <json-glib/json-glib.h>  // for JsonParser, JsonBuilder
#include <zip.h>                  // for zip_open

#include "ambitus/Board.h"        // for isBoardPageType
#include "util/GzInputStream.h"   // for GzInputStream
#include "util/PathUtil.h"        // for toGFilename
#include "util/StringUtils.h"     // for char_cast
#include "util/ZipInputStream.h"  // for ZipInputStream

namespace ambitus::library {

namespace {
constexpr int INDEX_VERSION = 2;       // 2: pages remember the board mark
constexpr size_t CONTEXT_BEFORE = 40;  ///< characters of excerpt before the match
constexpr size_t CONTEXT_AFTER = 70;   ///< and after

auto utf8(const fs::path& p) -> std::string { return std::string(char_cast(p.u8string())); }
auto fromUtf8(const std::string& s) -> fs::path { return fs::path(std::u8string(s.begin(), s.end())); }

struct ParseState {
    std::vector<PageText> pages;
    bool inText = false;
    std::string text;
};

void onStart(GMarkupParseContext*, const gchar* name, const gchar** attrNames, const gchar** attrValues, gpointer data,
             GError**) {
    auto* st = static_cast<ParseState*>(data);
    if (!strcmp(name, "page")) {
        st->pages.emplace_back();
    } else if (!strcmp(name, "background") && !st->pages.empty()) {
        for (int i = 0; attrNames[i]; i++) {
            if (!strcmp(attrNames[i], "config")) {
                PageType t;
                t.config = attrValues[i];
                st->pages.back().board = ambitus::board::isBoardPageType(t);
            }
        }
    } else if (!strcmp(name, "text") && !st->pages.empty()) {
        st->inText = true;
        st->text.clear();
    } else if (!strcmp(name, "teximage") && !st->pages.empty()) {
        for (int i = 0; attrNames[i]; i++) {
            if (!strcmp(attrNames[i], "text") && *attrValues[i]) {
                st->pages.back().latex.emplace_back(attrValues[i]);
            }
        }
    }
}

void onEnd(GMarkupParseContext*, const gchar* name, gpointer data, GError**) {
    auto* st = static_cast<ParseState*>(data);
    if (!strcmp(name, "text") && st->inText) {
        st->inText = false;
        if (!st->text.empty()) {
            st->pages.back().texts.push_back(std::move(st->text));
        }
        st->text.clear();
    }
}

void onText(GMarkupParseContext*, const gchar* text, gsize len, gpointer data, GError**) {
    auto* st = static_cast<ParseState*>(data);
    if (st->inText) {
        st->text.append(text, len);
    }
}

auto modified(const fs::path& p, long long& size) -> std::optional<std::time_t> {
    GStatBuf st{};
    if (g_stat(Util::toGFilename(p).c_str(), &st) != 0) {
        return std::nullopt;
    }
    size = static_cast<long long>(st.st_size);
    return st.st_mtime;
}

/// Turns newlines and runs of spaces into one space.
auto collapse(const std::string& s) -> std::string {
    std::string out;
    bool space = false;
    for (char c: s) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            space = true;
            continue;
        }
        if (space && !out.empty()) {
            out += ' ';
        }
        space = false;
        out += c;
    }
    return out;
}

/// byte offset `chars` UTF-8 characters back (or forward) from `pos`, staying inside s
auto stepChars(const std::string& s, size_t pos, size_t chars, bool forward) -> size_t {
    const char* base = s.c_str();
    const char* p = base + pos;
    for (size_t i = 0; i < chars; i++) {
        if (forward) {
            if (*p == '\0') {
                break;
            }
            p = g_utf8_next_char(p);
        } else {
            if (p == base) {
                break;
            }
            p = g_utf8_prev_char(p);
        }
    }
    return static_cast<size_t>(p - base);
}

auto excerpt(const std::string& s, size_t from, size_t to, SearchHit& hit) {
    size_t a = stepChars(s, from, CONTEXT_BEFORE, false);
    size_t b = stepChars(s, to, CONTEXT_AFTER, true);
    hit.before = (a > 0 ? "…" : "") + collapse(s.substr(a, from - a));
    if (!s.empty() && from > a && (s[from - 1] == ' ' || s[from - 1] == '\n')) {
        hit.before += ' ';
    }
    hit.match = collapse(s.substr(from, to - from));
    hit.after = collapse(s.substr(to, b - to)) + (b < s.size() ? "…" : "");
    if (to < s.size() && (s[to] == ' ' || s[to] == '\n') && !hit.after.empty() && hit.after[0] != ' ') {
        hit.after.insert(hit.after.begin(), ' ');
    }
}
}  // namespace

auto extractNotebookText(const fs::path& file) -> std::optional<std::vector<PageText>> {
    std::unique_ptr<xoj::util::InputStream> in;
    zip_t* zip = nullptr;
    try {
        int err = 0;
        zip = zip_open(char_cast(file.u8string().c_str()), ZIP_RDONLY, &err);
        if (zip) {
            in = std::make_unique<xoj::util::ZipInputStream>(zip, "content.xml");
        } else if (err == ZIP_ER_NOZIP) {
            in = std::make_unique<xoj::util::GzInputStream>(file);
        } else {
            return std::nullopt;
        }
    } catch (const std::exception&) {
        if (zip) {
            zip_discard(zip);
        }
        return std::nullopt;
    }

    ParseState st;
    GMarkupParser parser{onStart, onEnd, onText, nullptr, nullptr};
    GMarkupParseContext* ctx = g_markup_parse_context_new(&parser, G_MARKUP_DEFAULT_FLAGS, &st, nullptr);
    std::array<char, 1 << 14> buf{};
    bool ok = true;
    int n = 0;
    while ((n = in->read(buf.data(), buf.size())) > 0) {
        if (!g_markup_parse_context_parse(ctx, buf.data(), n, nullptr)) {
            ok = false;
            break;
        }
    }
    ok = ok && n >= 0 && g_markup_parse_context_end_parse(ctx, nullptr);
    g_markup_parse_context_free(ctx);
    in->close();
    in.reset();
    if (zip) {
        zip_discard(zip);
    }
    if (!ok) {
        return std::nullopt;
    }
    return std::move(st.pages);
}

auto foldForSearch(const std::string& s) -> Folded {
    Folded f;
    f.text.reserve(s.size());
    const char* base = s.c_str();
    for (const char* p = base; *p; p = g_utf8_next_char(p)) {
        const size_t off = static_cast<size_t>(p - base);
        const size_t next = static_cast<size_t>(g_utf8_next_char(p) - base);
        gunichar c = g_utf8_get_char_validated(p, -1);
        if (c == static_cast<gunichar>(-1) || c == static_cast<gunichar>(-2)) {
            continue;
        }
        // decompose (é → e + ´), drop the marks, lower the rest
        gunichar decomposed[8];
        gsize nd = g_unichar_fully_decompose(c, false, decomposed, G_N_ELEMENTS(decomposed));
        for (gsize i = 0; i < nd; i++) {
            const auto type = g_unichar_type(decomposed[i]);
            if (type == G_UNICODE_NON_SPACING_MARK || type == G_UNICODE_SPACING_MARK ||
                type == G_UNICODE_ENCLOSING_MARK) {
                continue;
            }
            gunichar lower = g_unichar_tolower(decomposed[i]);
            if (g_unichar_isspace(lower)) {
                lower = ' ';
            }
            char out[6];
            const int len = g_unichar_to_utf8(lower, out);
            for (int k = 0; k < len; k++) {
                f.text += out[k];
                f.start.push_back(off);
                f.end.push_back(next);
            }
        }
    }
    return f;
}

SearchIndex::SearchIndex(fs::path cacheFile): file(std::move(cacheFile)) {
    JsonParser* parser = json_parser_new();
    if (!json_parser_load_from_file(parser, Util::toGFilename(file).c_str(), nullptr)) {
        g_object_unref(parser);
        return;
    }
    JsonNode* root = json_parser_get_root(parser);
    JsonObject* obj = root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : nullptr;
    if (obj && json_object_get_int_member_with_default(obj, "version", 0) == INDEX_VERSION &&
        json_object_has_member(obj, "notebooks")) {
        JsonNode* nbs = json_object_get_member(obj, "notebooks");
        JsonArray* arr = JSON_NODE_HOLDS_ARRAY(nbs) ? json_node_get_array(nbs) : nullptr;
        for (guint i = 0; arr && i < json_array_get_length(arr); i++) {
            JsonNode* n = json_array_get_element(arr, i);
            if (!JSON_NODE_HOLDS_OBJECT(n)) {
                continue;
            }
            JsonObject* o = json_node_get_object(n);
            const char* path = json_object_get_string_member_with_default(o, "path", nullptr);
            if (!path || !json_object_has_member(o, "pages")) {
                continue;
            }
            Entry e;
            e.mtime = static_cast<std::time_t>(json_object_get_int_member_with_default(o, "mtime", 0));
            e.size = json_object_get_int_member_with_default(o, "size", -1);
            JsonNode* pagesNode = json_object_get_member(o, "pages");
            if (!JSON_NODE_HOLDS_ARRAY(pagesNode)) {
                continue;
            }
            JsonArray* pages = json_node_get_array(pagesNode);
            for (guint k = 0; k < json_array_get_length(pages); k++) {
                PageText pt;
                JsonNode* pn = json_array_get_element(pages, k);
                if (JSON_NODE_HOLDS_OBJECT(pn)) {
                    JsonObject* po = json_node_get_object(pn);
                    pt.board = json_object_get_boolean_member_with_default(po, "board", false);
                    for (auto [key, vec]: {std::pair{"text", &pt.texts}, std::pair{"latex", &pt.latex}}) {
                        if (!json_object_has_member(po, key)) {
                            continue;
                        }
                        JsonNode* an = json_object_get_member(po, key);
                        if (!JSON_NODE_HOLDS_ARRAY(an)) {
                            continue;
                        }
                        JsonArray* a = json_node_get_array(an);
                        for (guint m = 0; m < json_array_get_length(a); m++) {
                            if (const char* s = json_array_get_string_element(a, m)) {
                                vec->emplace_back(s);
                            }
                        }
                    }
                }
                e.pages.push_back(std::move(pt));
            }
            entries[fromUtf8(path)] = std::move(e);
        }
    }
    g_object_unref(parser);
}

auto SearchIndex::update(const std::vector<fs::path>& notebooks) -> size_t {
    std::map<fs::path, Entry> next;
    size_t read = 0;
    for (const auto& p: notebooks) {
        if (next.count(p)) {
            continue;
        }
        long long size = -1;
        auto mtime = modified(p, size);
        if (!mtime) {
            continue;
        }
        auto it = entries.find(p);
        if (it != entries.end() && it->second.mtime == *mtime && it->second.size == size) {
            next[p] = std::move(it->second);
            continue;
        }
        Entry e;
        e.mtime = *mtime;
        e.size = size;
        // an unreadable notebook is remembered as empty, so it is not read again until it changes
        if (auto pages = extractNotebookText(p)) {
            e.pages = std::move(*pages);
        }
        next[p] = std::move(e);
        read++;
    }
    entries = std::move(next);
    return read;
}

auto SearchIndex::isBoard(const fs::path& notebook) const -> bool {
    auto it = entries.find(notebook);
    return it != entries.end() && it->second.pages.size() == 1 && it->second.pages[0].board;
}

auto SearchIndex::save() const -> bool {
    JsonBuilder* b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "version");
    json_builder_add_int_value(b, INDEX_VERSION);
    json_builder_set_member_name(b, "notebooks");
    json_builder_begin_array(b);
    for (const auto& [path, e]: entries) {
        json_builder_begin_object(b);
        json_builder_set_member_name(b, "path");
        json_builder_add_string_value(b, utf8(path).c_str());
        json_builder_set_member_name(b, "mtime");
        json_builder_add_int_value(b, static_cast<gint64>(e.mtime));
        json_builder_set_member_name(b, "size");
        json_builder_add_int_value(b, e.size);
        json_builder_set_member_name(b, "pages");
        json_builder_begin_array(b);
        for (const auto& pt: e.pages) {
            json_builder_begin_object(b);
            if (pt.board) {
                json_builder_set_member_name(b, "board");
                json_builder_add_boolean_value(b, true);
            }
            for (auto [key, vec]: {std::pair{"text", &pt.texts}, std::pair{"latex", &pt.latex}}) {
                if (vec->empty()) {
                    continue;
                }
                json_builder_set_member_name(b, key);
                json_builder_begin_array(b);
                for (const auto& s: *vec) {
                    json_builder_add_string_value(b, s.c_str());
                }
                json_builder_end_array(b);
            }
            json_builder_end_object(b);
        }
        json_builder_end_array(b);
        json_builder_end_object(b);
    }
    json_builder_end_array(b);
    json_builder_end_object(b);

    JsonGenerator* gen = json_generator_new();
    JsonNode* root = json_builder_get_root(b);
    json_generator_set_root(gen, root);
    gsize len = 0;
    gchar* data = json_generator_to_data(gen, &len);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const bool ok = g_file_set_contents(Util::toGFilename(file).c_str(), data, static_cast<gssize>(len), nullptr);
    g_free(data);
    json_node_unref(root);
    g_object_unref(gen);
    g_object_unref(b);
    return ok;
}

auto SearchIndex::search(const std::string& query, const std::vector<fs::path>& order, size_t limit) const
        -> std::vector<SearchHit> {
    std::vector<SearchHit> hits;
    auto q = foldForSearch(query).text;
    // spaces at the ends do not count
    q.erase(0, q.find_first_not_of(' '));
    q.erase(q.find_last_not_of(' ') + 1);
    if (q.empty()) {
        return hits;
    }
    for (const auto& path: order) {
        auto it = entries.find(path);
        if (it == entries.end()) {
            continue;
        }
        for (size_t page = 0; page < it->second.pages.size(); page++) {
            const auto& pt = it->second.pages[page];
            for (bool latex: {false, true}) {
                for (const auto& s: latex ? pt.latex : pt.texts) {
                    const Folded f = foldForSearch(s);
                    // every match in this box, but each one shown once
                    for (size_t at = f.text.find(q); at != std::string::npos; at = f.text.find(q, at + q.size())) {
                        SearchHit h;
                        h.notebook = path;
                        h.page = page;
                        h.latex = latex;
                        excerpt(s, f.start[at], f.end[at + q.size() - 1], h);
                        hits.push_back(std::move(h));
                        if (hits.size() >= limit) {
                            return hits;
                        }
                    }
                }
            }
        }
    }
    return hits;
}

}  // namespace ambitus::library
