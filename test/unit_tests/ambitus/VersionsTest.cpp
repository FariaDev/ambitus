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

TEST(Versions, theDateSaysTodayOrYesterday) {
    const auto now = Clock::now();
    EXPECT_EQ(describe(now, now).rfind("today, ", 0), 0u);
    EXPECT_EQ(describe(now - hours(24), now).rfind("yesterday, ", 0), 0u);
    EXPECT_EQ(describe(now - hours(24 * 40), now).find("today"), std::string::npos);
}
