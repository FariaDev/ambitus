/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <fstream>
#include <iterator>
#include <string>

#include <glib.h>
#include <gtest/gtest.h>

#include "ambitus/Rename.h"
#include "ambitus/Versions.h"
#include "library/LibraryStore.h"

#include "filesystem.h"

using namespace ambitus::rename;
using namespace ambitus::library;
using namespace std::chrono;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        gchar* d = g_dir_make_tmp("ambitus-rename-XXXXXX", nullptr);
        path = d;
        g_free(d);
    }
    ~TempDir() { fs::remove_all(path); }
};

void write(const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::trunc) << text; }

std::string read(const fs::path& p) {
    std::ifstream in(p);
    return {std::istreambuf_iterator<char>(in), {}};
}

const ambitus::versions::Clock::time_point T0 = ambitus::versions::Clock::from_time_t(1'763'000'000);

bool nobodyHasTheName(const std::string&) { return false; }
}  // namespace

TEST(RenameName, emptyNamesAndTheSameNameAreNotChanges) {
    EXPECT_EQ(checkName("", "Antigo", nobodyHasTheName), NameCheck::EMPTY);
    EXPECT_EQ(checkName("   ", "Antigo", nobodyHasTheName), NameCheck::EMPTY);
    EXPECT_EQ(checkName("Antigo", "Antigo", nobodyHasTheName), NameCheck::SAME);
    // the extension is not part of the name the user types
    EXPECT_EQ(checkName("Antigo.xopp", "Antigo", nobodyHasTheName), NameCheck::SAME);
    EXPECT_EQ(checkName("Novo.xopp", "Antigo", nobodyHasTheName), NameCheck::OK);
    EXPECT_EQ(checkName("  Novo  ", "Antigo", nobodyHasTheName), NameCheck::OK);
}

TEST(RenameName, slashesBackslashesHiddenNamesAndControlCharactersAreRefused) {
    EXPECT_EQ(checkName("a/b", "Antigo", nobodyHasTheName), NameCheck::INVALID);
    EXPECT_EQ(checkName("a\\b", "Antigo", nobodyHasTheName), NameCheck::INVALID);
    EXPECT_EQ(checkName(".oculto", "Antigo", nobodyHasTheName), NameCheck::INVALID);
    EXPECT_EQ(checkName("a\tb", "Antigo", nobodyHasTheName), NameCheck::INVALID);
#ifdef __APPLE__
    EXPECT_EQ(checkName("a:b", "Antigo", nobodyHasTheName), NameCheck::INVALID);
#else
    EXPECT_EQ(checkName("a:b", "Antigo", nobodyHasTheName), NameCheck::OK);  // ":" is allowed off macOS
#endif
}

TEST(RenameName, anotherNotebookWithTheNameIsAConflict) {
    auto taken = [](const std::string& n) { return n == "Ocupado"; };
    EXPECT_EQ(checkName("Ocupado", "Antigo", taken), NameCheck::EXISTS);
    EXPECT_EQ(checkName("Livre", "Antigo", taken), NameCheck::OK);
    EXPECT_EQ(nameOf("/caderno/Antigo.xopp"), "Antigo");
}

TEST(RenameName, theNotebookItselfIsNotTakenByItsOwnName) {
    TempDir d;
    const fs::path a = d.path / "Antigo.xopp";
    const fs::path b = d.path / "Outro.xopp";
    write(a, "um");
    write(b, "dois");
    EXPECT_FALSE(isTaken(a, "Antigo"));
    EXPECT_TRUE(isTaken(a, "Outro"));
    EXPECT_FALSE(isTaken(a, "Livre"));
}

TEST(RenameMove, theFileMovesWithItsSideFiles) {
    TempDir d;
    const fs::path versions = d.path / "versoes";
    const fs::path from = d.path / "Antigo.xopp";
    write(from, "conteudo");
    // the same side files that drafts::keep moves: the backup, the .bak and the autosaves
    write(d.path / "Antigo.xopp~", "backup");
    write(d.path / "Antigo.xopp.bak", "bak");
    write(d.path / "Antigo.xopp.autosave.xopp", "autosave");
    write(d.path / ".Antigo.autosave.xopp", "oculto");

    auto to = moveNotebook(from, "Novo", versions);
    ASSERT_TRUE(to.has_value());
    EXPECT_EQ(*to, d.path / "Novo.xopp");
    EXPECT_FALSE(fs::exists(from));
    EXPECT_EQ(read(*to), "conteudo");
    EXPECT_EQ(read(d.path / "Novo.xopp~"), "backup");
    EXPECT_EQ(read(d.path / "Novo.xopp.bak"), "bak");
    EXPECT_EQ(read(d.path / "Novo.xopp.autosave.xopp"), "autosave");
    EXPECT_EQ(read(d.path / ".Novo.autosave.xopp"), "oculto");
    EXPECT_FALSE(fs::exists(d.path / "Antigo.xopp~"));
    EXPECT_FALSE(fs::exists(d.path / ".Antigo.autosave.xopp"));
}

TEST(RenameMove, anExistingTargetIsRefusedAndNothingChanges) {
    TempDir d;
    const fs::path versions = d.path / "versoes";
    const fs::path from = d.path / "Antigo.xopp";
    write(from, "um");
    write(d.path / "Outro.xopp", "dois");
    write(d.path / "Outro.xopp~", "backup do outro");

    EXPECT_FALSE(moveNotebook(from, "Outro", versions).has_value());
    EXPECT_FALSE(moveNotebook(from, "", versions).has_value());
    EXPECT_FALSE(moveNotebook(from, "a/b", versions).has_value());
    EXPECT_FALSE(moveNotebook(from, "Antigo", versions).has_value());  // the same name is no change

    EXPECT_EQ(read(from), "um");
    EXPECT_EQ(read(d.path / "Outro.xopp"), "dois");
    EXPECT_EQ(read(d.path / "Outro.xopp~"), "backup do outro");
}

TEST(RenameMove, theVersionsFollowTheNotebook) {
    TempDir d;
    const fs::path versions = d.path / "versoes";
    const fs::path from = d.path / "Antigo.xopp";
    write(from, "um");
    ASSERT_TRUE(ambitus::versions::keep(versions, from, T0, seconds(0), 30));

    auto to = moveNotebook(from, "Novo", versions);
    ASSERT_TRUE(to.has_value());
    EXPECT_TRUE(ambitus::versions::list(versions, from).empty());
    auto found = ambitus::versions::list(versions, *to);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(read(found[0].file), "um");
}

TEST(RenameMove, theCoverFollowsTheNotebookAndSurvivesARestart) {
    TempDir d;
    const fs::path versions = d.path / "versoes";
    const fs::path from = d.path / "Antigo.xopp";
    write(from, "um");
    const fs::path library = d.path / "library.ini";
    {
        LibraryStore store(library);
        store.setCover(from, Cover{CoverStyle::Plain, "#123456", 0, {}});
        auto to = moveNotebook(from, "Novo", versions);
        ASSERT_TRUE(to.has_value());
        store.moveCover(from, *to);
        ASSERT_TRUE(store.save());
    }
    LibraryStore reloaded(library);
    EXPECT_EQ(reloaded.cover(from), Cover{});
    EXPECT_EQ(reloaded.cover(d.path / "Novo.xopp").color, "#123456");
}
