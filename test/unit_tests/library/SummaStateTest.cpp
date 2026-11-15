/*
 * Ambitus
 *
 * Unit tests for the reading of Summa's interface state (language and accent).
 *
 * @license GNU GPLv2 or later
 */

#include <fstream>
#include <optional>
#include <string>

#include <glib.h>
#include <gtest/gtest.h>

#include "library/SummaState.h"
#include "util/StringUtils.h"  // for char_cast

#include "filesystem.h"

using namespace ambitus::library;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        gchar* p = g_dir_make_tmp("ambitus-summa-state-XXXXXX", nullptr);
        path = fs::canonical(p);
        g_free(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

struct EnvVar {
    const char* name;
    std::optional<std::string> old;
    EnvVar(const char* name, const std::string& value): name(name) {
        if (const char* v = g_getenv(name)) {
            old = v;
        }
        g_setenv(name, value.c_str(), true);
    }
    ~EnvVar() {
        if (old) {
            g_setenv(name, old->c_str(), true);
        } else {
            g_unsetenv(name);
        }
    }
};

void write(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}
}  // namespace

TEST(SummaState, readsTheLanguageAndTheAccentOfTheFile) {
    TempDir dir;
    EnvVar runtime("LEARNING_DESK_RUNTIME", std::string(char_cast(dir.path.u8string())));
    write(dir.path / "desk.json", R"({"uiLanguage": "pt-BR", "accent": "violet", "theme": "dark"})");

    const SummaState state = loadSummaState();
    EXPECT_EQ(state.language, "pt-BR");
    EXPECT_EQ(state.accent, "violet");
}

TEST(SummaState, acceptsEnglishAndGraphite) {
    EXPECT_EQ(parseSummaState(R"({"uiLanguage": "en", "accent": "graphite"})").language, "en");
    EXPECT_EQ(parseSummaState(R"({"uiLanguage": "en", "accent": "graphite"})").accent, "graphite");
}

TEST(SummaState, noFileLeavesBothEmpty) {
    TempDir dir;
    EnvVar runtime("LEARNING_DESK_RUNTIME", std::string(char_cast(dir.path.u8string())));

    const SummaState state = loadSummaState();
    EXPECT_TRUE(state.language.empty());
    EXPECT_TRUE(state.accent.empty());
}

TEST(SummaState, malformedFileLeavesBothEmpty) {
    TempDir dir;
    EnvVar runtime("LEARNING_DESK_RUNTIME", std::string(char_cast(dir.path.u8string())));
    write(dir.path / "desk.json", "{isto não é json");

    const SummaState state = loadSummaState();
    EXPECT_TRUE(state.language.empty());
    EXPECT_TRUE(state.accent.empty());

    // a root that is not an object is not a state either
    EXPECT_TRUE(parseSummaState(R"(["pt-BR", "violet"])").language.empty());
    EXPECT_TRUE(parseSummaState(R"(["pt-BR", "violet"])").accent.empty());
    EXPECT_TRUE(parseSummaState("").language.empty());
}

TEST(SummaState, valuesAmbitusDoesNotKnowAreIgnored) {
    SummaState state = parseSummaState(R"({"uiLanguage": "fr", "accent": "azul"})");
    EXPECT_TRUE(state.language.empty());
    EXPECT_TRUE(state.accent.empty());

    // only a chosen language counts: "" is "never chosen" in Summa
    state = parseSummaState(R"({"uiLanguage": "", "accent": ""})");
    EXPECT_TRUE(state.language.empty());
    EXPECT_TRUE(state.accent.empty());

    // values of another type do not pass for a choice
    state = parseSummaState(R"({"uiLanguage": 3, "accent": true})");
    EXPECT_TRUE(state.language.empty());
    EXPECT_TRUE(state.accent.empty());
}

TEST(SummaState, theFileIsInTheRuntimeFolderOfSumma) {
    const fs::path config = "/home/u/.config/Mesa de Estudos/config.json";
    EXPECT_EQ(summaStatePath(config, "", nullptr), fs::path("/home/u/.config/Mesa de Estudos/runtime/desk.json"));
    EXPECT_EQ(summaStatePath(config, R"({"runtimePath": "/dados/rt"})", nullptr), fs::path("/dados/rt/desk.json"));
    EXPECT_EQ(summaStatePath(config, R"({"runtimePath": ""})", nullptr),
              fs::path("/home/u/.config/Mesa de Estudos/runtime/desk.json"));
    EXPECT_EQ(summaStatePath(config, "{quebrado", nullptr),
              fs::path("/home/u/.config/Mesa de Estudos/runtime/desk.json"));
    // Summa honours $LEARNING_DESK_RUNTIME for both, and so does Ambitus
    EXPECT_EQ(summaStatePath(config, R"({"runtimePath": "/dados/rt"})", "/tmp/rt"), fs::path("/tmp/rt/desk.json"));
}
