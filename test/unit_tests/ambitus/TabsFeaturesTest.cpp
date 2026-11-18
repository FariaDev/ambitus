#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ambitus/ClosedTabs.h"
#include "ambitus/RestoreTabs.h"
#include "control/settings/Settings.h"

using namespace ambitus;

namespace {
bool existsIn(const std::set<std::string>& paths, const fs::path& path) { return paths.count(path.string()) != 0; }
}  // namespace

TEST(ClosedTabsTest, remembersTheNewestAndForgetsPastTwenty) {
    ClosedTabs tabs;
    EXPECT_TRUE(tabs.empty());
    for (size_t i = 0; i < ClosedTabs::CAPACITY + 5; i++) {
        tabs.push(fs::path("/notebooks/Caderno " + std::to_string(i) + ".xopp"), static_cast<int>(i));
    }
    EXPECT_EQ(tabs.size(), ClosedTabs::CAPACITY);

    const std::set<std::string> none;
    auto allPresent = [&none](const fs::path& p) { return existsIn(none, p); };
    auto onDisk = [](const fs::path&) { return true; };
    auto first = tabs.pop(allPresent, onDisk);
    ASSERT_TRUE(first.has_value());
    // the five oldest (0 to 4) fell off; the newest remembered is 24
    EXPECT_EQ(first->path, fs::path("/notebooks/Caderno 24.xopp"));
    EXPECT_EQ(first->page, 24);
}

TEST(ClosedTabsTest, popSkipsOpenAndMissingFiles) {
    ClosedTabs tabs;
    tabs.push("/notebooks/open.xopp", 1);
    tabs.push("/notebooks/gone.xopp", 2);
    tabs.push("/notebooks/back.xopp", 3);
    tabs.push("/notebooks/unsaved-but-with-a-path.xopp", 4);

    const std::set<std::string> open = {"/notebooks/open.xopp"};
    auto isOpen = [&open](const fs::path& p) { return existsIn(open, p); };
    auto onDisk = [](const fs::path& p) { return p.string().find("gone") == std::string::npos; };

    auto found = tabs.pop(isOpen, onDisk);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->path, fs::path("/notebooks/unsaved-but-with-a-path.xopp"));
    EXPECT_EQ(found->page, 4);

    // the next newest comes back; the open and the missing ones were dropped along the way
    found = tabs.pop(isOpen, onDisk);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->path, fs::path("/notebooks/back.xopp"));
    EXPECT_EQ(found->page, 3);
    EXPECT_FALSE(tabs.pop(isOpen, onDisk).has_value());
}

TEST(ClosedTabsTest, aFileWithoutAPathIsNotRemembered) {
    ClosedTabs tabs;
    tabs.push(fs::path(), 7);
    EXPECT_TRUE(tabs.empty());
}

TEST(RestoreTabsTest, roundTripsAwkwardNamesAndPages) {
    const std::vector<restoretabs::SavedTab> tabs = {
            {fs::path("/vault/Matéria, prova \"final\".xopp"), 2, true},
            {fs::path("/vault/with space and, comma.xopp"), 0, false},
            {fs::path("/vault/line\nbreak\\and\ttab.xopp"), 12, false},
    };
    const std::string saved = restoretabs::serialize(tabs);
    EXPECT_NE(saved.find('\t'), std::string::npos);
    EXPECT_EQ(restoretabs::deserialize(saved), tabs);
    EXPECT_TRUE(restoretabs::deserialize("").empty());
}

TEST(SettingsTest, openTabsFromLastTimeIsOnByDefaultAndRemembered) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_openTabs.xml";
    if (fs::exists(outPath)) {
        fs::remove(outPath);
    }

    Settings settings(outPath);
    EXPECT_TRUE(settings.getAmbitusOpenTabsFromLastTime());
    EXPECT_TRUE(settings.getAmbitusOpenTabs().empty());
    settings.setAmbitusOpenTabs("/vault/A, B.xopp\t3\t1\n/vault/C\nD.xopp\t0\t0\n");
    settings.setAmbitusOpenTabsFromLastTime(false);

    Settings loaded(outPath);
    loaded.load();
    EXPECT_FALSE(loaded.getAmbitusOpenTabsFromLastTime());
    EXPECT_EQ(loaded.getAmbitusOpenTabs(), settings.getAmbitusOpenTabs());
    EXPECT_EQ(restoretabs::deserialize(loaded.getAmbitusOpenTabs()).size(), 2u);

    fs::remove(outPath);
}
