/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <fstream>
#include <iterator>

#include <glib.h>
#include <gtest/gtest.h>

#include "ambitus/Versions.h"

#include "filesystem.h"

using namespace ambitus::versions;
using namespace std::chrono;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        gchar* d = g_dir_make_tmp("ambitus-versions-XXXXXX", nullptr);
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
const Clock::time_point T0 = Clock::from_time_t(1'763'000'000);
}  // namespace

TEST(Versions, aCopyOfWhatIsOnDiskNewestFirst) {
    TempDir tmp;
    const fs::path root = tmp.path / "versions";
    const fs::path nb = tmp.path / "Cálculo — aula 1.xopp";
    EXPECT_FALSE(keep(root, nb, T0, minutes(10), 30));  // nothing on disk yet
    write(nb, "um");
    EXPECT_TRUE(keep(root, nb, T0, minutes(10), 30));
    write(nb, "dois");
    EXPECT_FALSE(keep(root, nb, T0 + minutes(5), minutes(10), 30));  // too soon
    EXPECT_TRUE(keep(root, nb, T0 + minutes(11), minutes(10), 30));
    auto v = list(root, nb);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(read(v[0].file), "dois");
    EXPECT_EQ(read(v[1].file), "um");
    EXPECT_EQ(v[1].when, T0);
    EXPECT_EQ(v[0].size, 4u);
    // the copies are outside the notebook folder
    EXPECT_EQ(std::distance(fs::directory_iterator(tmp.path), fs::directory_iterator()), 2);
}

TEST(Versions, onlyTheNewestAreKeptAndAGapOfZeroAlwaysCopies) {
    TempDir tmp;
    const fs::path nb = tmp.path / "a.xopp";
    write(nb, "x");
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(keep(tmp.path / "v", nb, T0 + seconds(i), seconds(0), 3));
    }
    auto v = list(tmp.path / "v", nb);
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0].when, T0 + seconds(4));
    EXPECT_EQ(v[2].when, T0 + seconds(2));
}

TEST(Versions, eachNotebookHasItsOwnFolder) {
    TempDir tmp;
    write(tmp.path / "a.xopp", "a");
    write(tmp.path / "b.xopp", "b");
    keep(tmp.path / "v", tmp.path / "a.xopp", T0, seconds(0), 30);
    EXPECT_NE(folderFor(tmp.path / "v", tmp.path / "a.xopp"), folderFor(tmp.path / "v", tmp.path / "b.xopp"));
    EXPECT_TRUE(list(tmp.path / "v", tmp.path / "b.xopp").empty());
    EXPECT_EQ(list(tmp.path / "v", tmp.path / "a.xopp").size(), 1u);
}

TEST(Versions, theCopiesFollowTheNotebookWhenItIsMovedOrRenamed) {
    TempDir tmp;
    const fs::path root = tmp.path / "v";
    const fs::path before = tmp.path / "Cálculo.xopp";
    write(before, "um");
    ASSERT_TRUE(keep(root, before, T0, seconds(0), 30));

    const fs::path moved = tmp.path / "Matemática" / "Cálculo — aula 1.xopp";
    fs::create_directories(moved.parent_path());
    fs::rename(before, moved);
    EXPECT_TRUE(followMove(root, before, moved));

    EXPECT_TRUE(list(root, before).empty());
    auto v = list(root, moved);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(read(v[0].file), "um");
    EXPECT_EQ(v[0].when, T0);

    // and the saves from now on keep the copies under the new name
    write(moved, "dois");
    EXPECT_TRUE(keep(root, moved, T0 + seconds(1), seconds(0), 30));
    EXPECT_EQ(list(root, moved).size(), 2u);
}

TEST(Versions, aSaveAsCopiesTheHistoryAndTheOldFileKeepsItsOwn) {
    TempDir tmp;
    const fs::path root = tmp.path / "v";
    const fs::path old = tmp.path / "Cálculo.xopp";
    write(old, "um");
    ASSERT_TRUE(keep(root, old, T0, seconds(0), 30));

    const fs::path copy = tmp.path / "Cálculo (cópia).xopp";
    EXPECT_TRUE(followMove(root, old, copy, true));
    EXPECT_EQ(list(root, old).size(), 1u);
    ASSERT_EQ(list(root, copy).size(), 1u);
    EXPECT_EQ(read(list(root, copy)[0].file), "um");
}

TEST(Versions, theDateSaysTodayOrYesterday) {
    const auto now = Clock::now();
    EXPECT_EQ(describe(now, now).rfind("today, ", 0), 0u);
    EXPECT_EQ(describe(now - hours(24), now).rfind("yesterday, ", 0), 0u);
    EXPECT_EQ(describe(now - hours(24 * 40), now).find("today"), std::string::npos);
}

namespace {
Version at(Clock::time_point when) {
    Version v;
    v.when = when;
    return v;
}

/// The days are the local ones: the test fixes the zone, so "midnight" is where it says
struct FixedZone {
    std::string before;
    bool had;
    FixedZone() {
        const char* tz = g_getenv("TZ");
        had = tz != nullptr;
        before = tz ? tz : "";
        g_setenv("TZ", "UTC", true);
        tzset();
    }
    ~FixedZone() {
        if (had) {
            g_setenv("TZ", before.c_str(), true);
        } else {
            g_unsetenv("TZ");
        }
        tzset();
    }
};
}  // namespace

TEST(Versions, groupedByTheDayTheyWereMadeOn) {
    FixedZone zone;
    // 2025-11-13 12:00 UTC
    const auto now = Clock::from_time_t(1'763'035'200);
    std::vector<Version> list = {
            at(now - hours(1)),   // today 11:00
            at(now - hours(11)),  // today 01:00
            at(now - hours(13)),  // yesterday 23:00
            at(now - hours(30)),  // yesterday 06:00
            at(now - hours(60)),  // two days before, 00:00 of the 11th
            at(now - hours(24 * 9)),
    };
    auto groups = groupByDay(list, now);
    ASSERT_EQ(groups.size(), 4u);
    EXPECT_EQ(groups[0].day, DayGroup::Day::Today);
    EXPECT_EQ(groups[0].items, (std::vector<size_t>{0, 1}));
    EXPECT_EQ(groups[1].day, DayGroup::Day::Yesterday);
    EXPECT_EQ(groups[1].items, (std::vector<size_t>{2, 3}));
    EXPECT_EQ(groups[2].day, DayGroup::Day::Other);
    EXPECT_EQ(groups[2].items, (std::vector<size_t>{4}));
    EXPECT_EQ(groups[2].first, list[4].when);
    EXPECT_EQ(groups[3].day, DayGroup::Day::Other);
    EXPECT_EQ(groups[3].items, (std::vector<size_t>{5}));
}

TEST(Versions, noCopiesNoDays) { EXPECT_TRUE(groupByDay({}, Clock::now()).empty()); }

TEST(Versions, onlyOldCopiesLeaveTodayOut) {
    FixedZone zone;
    const auto now = Clock::from_time_t(1'763'035'200);
    auto groups = groupByDay({at(now - hours(24 * 3))}, now);
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].day, DayGroup::Day::Other);
}
