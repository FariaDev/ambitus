#include <cmath>
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

TEST(RestoreTabsTest, theTabBesideRoundTripsWithItsOwnFlag) {
    const std::vector<restoretabs::SavedTab> tabs = {
            {fs::path("/vault/in window.xopp"), 3, true, false},
            {fs::path("/vault/beside.xopp"), 5, false, true},
            {fs::path("/vault/aside.xopp"), 0, false, false},
    };
    const std::string saved = restoretabs::serialize(tabs);
    EXPECT_NE(saved.find("\t2\n"), std::string::npos);
    EXPECT_EQ(restoretabs::deserialize(saved), tabs);
}

TEST(RestoreTabsTest, listsSavedBeforeTheSideTabStillLoad) {
    // the lines of the sessions before the side tab: 1 for the tab in the window, 0 for the others
    const auto tabs = restoretabs::deserialize("/vault/A.xopp\t3\t1\n/vault/B.xopp\t0\t0\n");
    ASSERT_EQ(tabs.size(), 2u);
    EXPECT_TRUE(tabs[0].active);
    EXPECT_FALSE(tabs[0].beside);
    EXPECT_FALSE(tabs[1].active);
    EXPECT_FALSE(tabs[1].beside);
    EXPECT_EQ(tabs[0].page, 3);
}

TEST(RestoreTabsTest, theTabBesideIsNotTheActiveOne) {
    const auto tabs = restoretabs::deserialize("/vault/A.xopp\t1\t1\n/vault/B.xopp\t4\t2\n");
    ASSERT_EQ(tabs.size(), 2u);
    EXPECT_TRUE(tabs[0].active);
    EXPECT_FALSE(tabs[0].beside);
    EXPECT_FALSE(tabs[1].active);
    EXPECT_TRUE(tabs[1].beside);
    EXPECT_EQ(tabs[1].page, 4);
}

TEST(SettingsTest, sideRatioIsKeptWithinBoundsAndSurvivesASave) {
    const fs::path outPath = fs::temp_directory_path() / "xournalpp-test-units_Settings_sideRatio.xml";
    if (fs::exists(outPath)) {
        fs::remove(outPath);
    }

    Settings settings(outPath);
    EXPECT_DOUBLE_EQ(settings.getAmbitusSideRatio(), 0.4);
    settings.setAmbitusSideRatio(0.55);
    settings.save();

    Settings loaded(outPath);
    loaded.load();
    EXPECT_DOUBLE_EQ(loaded.getAmbitusSideRatio(), 0.55);

    loaded.setAmbitusSideRatio(3.0);
    EXPECT_DOUBLE_EQ(loaded.getAmbitusSideRatio(), 0.8);
    loaded.setAmbitusSideRatio(0.01);
    EXPECT_DOUBLE_EQ(loaded.getAmbitusSideRatio(), 0.2);
    loaded.setAmbitusSideRatio(std::nan(""));
    EXPECT_DOUBLE_EQ(loaded.getAmbitusSideRatio(), 0.4);

    fs::remove(outPath);
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
