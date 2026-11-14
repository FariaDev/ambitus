#include <string>

#include <gtest/gtest.h>

#include "ambitus/LatexError.h"

using ambitus::latex::firstErrorLine;

namespace {
const char* UNDEFINED = "This is pdfTeX, Version 3.141592653-2.6-1.40.25 (TeX Live 2023)\n"
                        "(./tex.tex\nLaTeX2e <2023-11-01>\n"
                        "! Undefined control sequence.\n"
                        "<recently read> \\foo \n"
                        "                \n"
                        "l.7 \\foo\n"
                        "          x\n"
                        "? \n"
                        "! Emergency stop.\n"
                        "<recently read> \\foo \n"
                        "l.7 \\foo\n"
                        "End of file on the terminal!\n";
}

TEST(LatexError, theMessageOfTheCompilerAndTheSourceItPointsAt) {
    EXPECT_EQ(firstErrorLine(UNDEFINED), "Undefined control sequence. \xE2\x80\x94 \\foo");
}

TEST(LatexError, onlyTheFirstErrorCounts) {
    // not "Emergency stop.", which the compiler adds after the first one
    EXPECT_EQ(firstErrorLine(UNDEFINED).find("Emergency"), std::string::npos);
}

TEST(LatexError, anErrorWithoutASourceLineIsJustTheMessage) {
    EXPECT_EQ(firstErrorLine("! LaTeX Error: File `foo.sty' not found.\n\nType X to quit.\n"),
              "LaTeX Error: File `foo.sty' not found.");
}

TEST(LatexError, theSourceOfTheNextErrorIsNotTakenForTheFirst) {
    EXPECT_EQ(firstErrorLine("! Missing $ inserted.\n<inserted text> \n$\n! Extra }.\nl.3 }\n"), "Missing $ inserted.");
}

TEST(LatexError, theFileLineErrorFormatGivesTheMessageAfterTheLineNumber) {
    EXPECT_EQ(firstErrorLine("./tex.tex:5: Undefined control sequence.\nl.5 \\bar\n"),
              "Undefined control sequence. \xE2\x80\x94 \\bar");
}

TEST(LatexError, aWindowsLineEndingDoesNotStayInTheText) {
    EXPECT_EQ(firstErrorLine("! Missing } inserted.\r\nl.2 a_{1\r\n"), "Missing } inserted. \xE2\x80\x94 a_{1");
}

TEST(LatexError, withoutAnExclamationTheFirstLineThatSaysErrorWins) {
    EXPECT_EQ(firstErrorLine("Starting\nRuntime error: no such file\nbye\n"), "Runtime error: no such file");
}

TEST(LatexError, withoutAnyOfThatTheFirstLineWithText) {
    EXPECT_EQ(firstErrorLine("\n\nFailed to find LaTeX generator program in PATH: pdflatex\nMore\n"),
              "Failed to find LaTeX generator program in PATH: pdflatex");
}

TEST(LatexError, noTextIsNoLine) {
    EXPECT_EQ(firstErrorLine(""), "");
    EXPECT_EQ(firstErrorLine("\n  \n"), "");
}

TEST(LatexError, aLongLineIsCutOnACharacterAndEndsInAnEllipsis) {
    std::string text = "! ";
    for (int i = 0; i < 100; i++) {
        text += "\xC3\xA9";  // é
    }
    const std::string out = firstErrorLine(text, 51);
    EXPECT_LE(out.size(), 51u + 3);
    ASSERT_GE(out.size(), 3u);
    EXPECT_EQ(out.substr(out.size() - 3), "\xE2\x80\xA6");
    // what is left before the ellipsis is whole "é"s
    EXPECT_EQ((out.size() - 3) % 2, 0u);
}
