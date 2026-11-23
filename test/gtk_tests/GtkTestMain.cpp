#include <cstdlib>
#include <initializer_list>
#include <system_error>

#include <glib.h>
#include <gtest/gtest.h>

#include "util/PathUtil.h"

int main(int argc, char** argv) {
    // Set the profile paths before GLib caches them during GTK startup.
    auto* profile = g_dir_make_tmp("xournalpp-gtk-tests-XXXXXX", nullptr);
    if (!profile) {
        g_printerr("Could not create a temporary Xournal++ test profile\n");
        return EXIT_FAILURE;
    }
    for (const char* dir: {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"}) {
        g_setenv(dir, profile, true);
    }
    auto profilePath = Util::fromGFilename(profile);
    g_free(profile);

    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    std::error_code error;
    fs::remove_all(profilePath, error);
    return result;
}
