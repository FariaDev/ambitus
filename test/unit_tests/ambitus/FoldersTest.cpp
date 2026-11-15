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

#include "ambitus/Folders.h"

#include "filesystem.h"

using namespace ambitus::folders;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        gchar* d = g_dir_make_tmp("ambitus-folders-XXXXXX", nullptr);
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
}  // namespace

TEST(AmbitusFolders, theXournalppFolderIsCopiedAndStaysAsItWas) {
    TempDir dir;
    write(dir.path / "xournalpp" / "settings.xml", "<settings/>");
    write(dir.path / "xournalpp" / "plugins" / "a.lua", "-- a");

    EXPECT_TRUE(copyIfMissing(dir.path / "xournalpp", dir.path / "ambitus"));

    EXPECT_EQ(read(dir.path / "ambitus" / "settings.xml"), "<settings/>");
    EXPECT_EQ(read(dir.path / "ambitus" / "plugins" / "a.lua"), "-- a");
    EXPECT_EQ(read(dir.path / "xournalpp" / "settings.xml"), "<settings/>");
    EXPECT_EQ(read(dir.path / "xournalpp" / "plugins" / "a.lua"), "-- a");
}

TEST(AmbitusFolders, anExistingAmbitusFolderIsNotTouched) {
    TempDir dir;
    write(dir.path / "xournalpp" / "settings.xml", "<xournalpp/>");
    write(dir.path / "ambitus" / "settings.xml", "<ambitus/>");

    EXPECT_FALSE(copyIfMissing(dir.path / "xournalpp", dir.path / "ambitus"));

    EXPECT_EQ(read(dir.path / "ambitus" / "settings.xml"), "<ambitus/>");
    EXPECT_FALSE(fs::exists(dir.path / "ambitus" / "xournalpp"));
}

TEST(AmbitusFolders, withoutXournalppNothingIsMade) {
    TempDir dir;

    EXPECT_FALSE(copyIfMissing(dir.path / "xournalpp", dir.path / "ambitus"));

    EXPECT_FALSE(fs::exists(dir.path / "ambitus"));
}
