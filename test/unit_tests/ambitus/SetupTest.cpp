/*
 * Ambitus
 *
 * The rule of the setup of the first time: it shows when the settings file did not exist before this opening, and only
 * then (a file from before the setup is saved as done; a setup that was left half way shows again).
 *
 * @license GNU GPLv2 or later
 */

#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <unistd.h>

#include "control/settings/Settings.h"

#include "filesystem.h"

namespace {
fs::path scratchFile(const char* name) {
    const fs::path dir = fs::temp_directory_path() / ("ambitus-setup-test-" + std::to_string(getpid()));
    fs::create_directories(dir);
    const fs::path file = dir / name;
    fs::remove(file);
    return file;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
}
}  // namespace

TEST(Setup, aSettingsFileThatDidNotExistStartsTheSetup) {
    const fs::path file = scratchFile("first.xml");
    Settings settings(file);
    settings.load();
    EXPECT_FALSE(settings.getAmbitusSetupDone());
    // the file that was regenerated already says so: leaving the program half way shows the setup again
    EXPECT_NE(readAll(file).find("name=\"ambitusSetupDone\" value=\"false\""), std::string::npos);
}

TEST(Setup, finishingKeepsItDoneForTheNextOpening) {
    const fs::path file = scratchFile("finished.xml");
    {
        Settings settings(file);
        settings.load();
        settings.setAmbitusSetupDone(true);
    }
    Settings again(file);
    again.load();
    EXPECT_TRUE(again.getAmbitusSetupDone());
    EXPECT_NE(readAll(file).find("name=\"ambitusSetupDone\" value=\"true\""), std::string::npos);
}

TEST(Setup, aSettingsFileFromBeforeTheSetupIsSavedAsDone) {
    const fs::path file = scratchFile("old.xml");
    write(file,
          "<?xml version=\"1.0\"?>\n<settings>\n  <property name=\"ambitusSearchInk\" value=\"true\"/>\n</settings>\n");
    Settings settings(file);
    settings.load();
    EXPECT_TRUE(settings.getAmbitusSetupDone());
    EXPECT_TRUE(settings.getAmbitusSearchInk());  // what the file had is still read
    EXPECT_NE(readAll(file).find("name=\"ambitusSetupDone\" value=\"true\""), std::string::npos);
}

TEST(Setup, aSetupLeftHalfWayShowsAgain) {
    const fs::path file = scratchFile("half.xml");
    write(file, "<?xml version=\"1.0\"?>\n<settings>\n  <property name=\"ambitusSetupDone\" "
                "value=\"false\"/>\n</settings>\n");
    Settings settings(file);
    settings.load();
    EXPECT_FALSE(settings.getAmbitusSetupDone());
}

TEST(Setup, thePageSizeOfNewNotebooksIsKept) {
    const fs::path file = scratchFile("size.xml");
    {
        Settings settings(file);
        settings.load();
        EXPECT_EQ(settings.getAmbitusNewSize(), "a4");
        settings.setAmbitusNewSize("letter");
    }
    Settings again(file);
    again.load();
    EXPECT_EQ(again.getAmbitusNewSize(), "letter");
}

TEST(Setup, theDraftChoiceStartsSummaA4AndIsKept) {
    const fs::path file = scratchFile("draft.xml");
    {
        Settings settings(file);
        settings.load();
        EXPECT_EQ(settings.getAmbitusDraftPaper(), "summa");
        EXPECT_EQ(settings.getAmbitusDraftSize(), "a4");
        settings.setAmbitusDraftPaper("grid");
        settings.setAmbitusDraftSize("infinite");
    }
    Settings again(file);
    again.load();
    EXPECT_EQ(again.getAmbitusDraftPaper(), "grid");
    EXPECT_EQ(again.getAmbitusDraftSize(), "infinite");
}

TEST(Setup, theDraftChoiceDoesNotChangeTheNewNotebook) {
    const fs::path file = scratchFile("draft-apart.xml");
    {
        Settings settings(file);
        settings.load();
        settings.setAmbitusDraftPaper("dotted");
        settings.setAmbitusDraftSize("letter");
    }
    Settings again(file);
    again.load();
    EXPECT_EQ(again.getAmbitusNewPaper(), "summa");
    EXPECT_EQ(again.getAmbitusNewSize(), "a4");
}
