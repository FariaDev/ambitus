/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <fstream>
#include <iterator>
#include <set>
#include <string>

#include <glib.h>
#include <gtest/gtest.h>

#include "ambitus/Drafts.h"
#include "util/i18n.h"

#include "filesystem.h"

using namespace ambitus::drafts;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        gchar* d = g_dir_make_tmp("ambitus-drafts-XXXXXX", nullptr);
        path = d;
        g_free(d);
    }
    ~TempDir() { fs::remove_all(path); }
};

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file) << text;
}

std::string read(const fs::path& file) {
    std::ifstream in(file);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct DraftsTest: ::testing::Test {
    TempDir dir;
    fs::path drafts;
    fs::path subject;

    void SetUp() override {
        drafts = dir.path / "drafts";
        subject = dir.path / "Matéria";
        fs::create_directories(drafts);
        setFolderForTesting(drafts);
    }
    void TearDown() override { setFolderForTesting({}); }
};

std::tm timeOf(int year, int month, int day, int hour, int minute) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    return t;
}
}  // namespace

TEST_F(DraftsTest, aNewNameHasTheTimeAndCountsOnWhenItIsTaken) {
    std::set<std::string> names;
    auto exists = [&names](const std::string& n) { return names.count(n) > 0; };
    const std::string base = std::string(_("Draft")) + " 2026-11-15 14-32";

    EXPECT_EQ(newName(timeOf(2026, 11, 15, 14, 32), exists), base);

    names.insert(base);
    EXPECT_EQ(newName(timeOf(2026, 11, 15, 14, 32), exists), base + " 2");

    names.insert(base + " 2");
    EXPECT_EQ(newName(timeOf(2026, 11, 15, 14, 32), exists), base + " 3");
}

TEST_F(DraftsTest, theTabSaysDraftAndTheTime) {
    const std::string draft = std::string(_("Draft"));
    EXPECT_EQ(tabLabel("Draft 2026-11-15 14-32.xopp"), draft + " · 14:32");
    EXPECT_EQ(tabLabel("Draft 2026-11-15 14-32 2.xopp"), draft + " · 14:32");
    EXPECT_EQ(tabLabel("Rascunho 2026-11-15 09-05.xopp"), draft + " · 09:05");
    EXPECT_EQ(tabLabel("Draft 2026-11-15 14-32.xopp"), tabLabel("Draft 2026-11-15 14-32"));
}

TEST_F(DraftsTest, theDisplayNameSaysTheDayAndTheTime) {
    const std::string draft = _("Draft");
    GDateTime* when = g_date_time_new_local(2026, 11, 15, 21, 12, 0);
    gchar* formatted = g_date_time_format(when, "%e %b");
    std::string dayMonth = formatted ? formatted : "";
    g_free(formatted);
    g_date_time_unref(when);
    const size_t first = dayMonth.find_first_not_of(' ');
    if (first != std::string::npos && first > 0) {
        dayMonth.erase(0, first);
    }

    EXPECT_EQ(displayName("Draft 2026-11-15 21-12"), draft + " · " + dayMonth + ", 21:12");
    EXPECT_EQ(displayName("Rascunho 2026-11-15 09-05 2"), draft + " · " + dayMonth + ", 09:05");
    EXPECT_EQ(displayName("sem data"), draft);
}

TEST_F(DraftsTest, onlyFilesInsideTheDraftsFolderAreDrafts) {
    EXPECT_TRUE(isDraft(drafts / "Draft 2026-11-15 14-32.xopp"));
    EXPECT_TRUE(isDraft(fs::path(drafts) / "outra pasta" / "algo.xopp"));
    EXPECT_FALSE(isDraft(dir.path / "Cálculo" / "Limites.xopp"));
    EXPECT_FALSE(isDraft(drafts));
    EXPECT_FALSE(isDraft({}));
}

TEST_F(DraftsTest, keepingMovesTheFileAndItsSideFiles) {
    const fs::path draft = drafts / "Draft 2026-11-15 14-32.xopp";
    write(draft, "ideias");
    const fs::path autosave = drafts / ".Draft 2026-11-15 14-32.autosave.xopp";
    write(autosave, "quase");

    auto kept = keep(draft, subject, "Ideias para a lista 3");
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(*kept, subject / "Ideias para a lista 3.xopp");
    EXPECT_EQ(read(*kept), "ideias");
    EXPECT_FALSE(fs::exists(draft));
    EXPECT_TRUE(fs::exists(subject / ".Ideias para a lista 3.autosave.xopp"));
    EXPECT_FALSE(fs::exists(autosave));
}

TEST_F(DraftsTest, keepingRefusesANameThatIsTaken) {
    const fs::path draft = drafts / "Draft 2026-11-15 14-32.xopp";
    write(draft, "ideias");
    const fs::path taken = subject / "Limites.xopp";
    write(taken, "outro");

    EXPECT_FALSE(keep(draft, subject, "Limites").has_value());
    EXPECT_TRUE(fs::exists(draft));
    EXPECT_EQ(read(taken), "outro");
}

TEST_F(DraftsTest, discardingOnlyWorksInsideTheDraftsFolder) {
    const fs::path draft = drafts / "Draft 2026-11-15 14-32.xopp";
    write(draft, "ideias");
    const fs::path side = drafts / ".Draft 2026-11-15 14-32.autosave.xopp";
    write(side, "quase");
    const fs::path outside = dir.path / "Cálculo" / "Limites.xopp";
    write(outside, "guardado");

    EXPECT_FALSE(discard(outside));
    EXPECT_TRUE(fs::exists(outside));

    EXPECT_TRUE(discard(draft));
    EXPECT_FALSE(fs::exists(draft));
    EXPECT_FALSE(fs::exists(side));
}

TEST_F(DraftsTest, theFolderIsAmbitusOwnDraftsFolder) {
    setFolderForTesting({});
    EXPECT_EQ(folder(), fs::path(g_get_user_data_dir()) / "ambitus" / "drafts");
}
