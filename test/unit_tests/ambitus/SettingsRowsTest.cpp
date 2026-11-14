/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "ambitus/SettingsRows.h"

using namespace ambitus::settingsrows;
using ambitus::newnotebook::Paper;

TEST(SettingsRows, theSearchIgnoresAccentsAndCapitals) {
    EXPECT_TRUE(matches("pressao", "Pressão da caneta muda a espessura", "", "Caneta e toque"));
    EXPECT_TRUE(matches("PRESSÃO", "Pressão da caneta muda a espessura", "", "Caneta e toque"));
    EXPECT_TRUE(matches("versoes", "Guardar versões anteriores", "", "Cadernos"));
    EXPECT_FALSE(matches("borracha", "Pressão da caneta muda a espessura", "", "Caneta e toque"));
}

TEST(SettingsRows, theSearchLooksAtTheDescriptionAndTheSection) {
    // the description of "Salvar sozinho" says what it does; the section name finds all of its rows
    EXPECT_TRUE(matches("trocar de aba", "Salvar sozinho", "a cada minuto e ao trocar de aba", "Cadernos"));
    EXPECT_TRUE(matches("cadernos", "Salvar sozinho", "", "Cadernos"));
    EXPECT_FALSE(matches("tema", "Salvar sozinho", "a cada minuto", "Cadernos"));
}

TEST(SettingsRows, everyWordOfTheQueryMustBeThere) {
    EXPECT_TRUE(matches("salvar  aba", "Salvar sozinho", "ao trocar de aba", "Cadernos"));
    EXPECT_FALSE(matches("salvar tema", "Salvar sozinho", "ao trocar de aba", "Cadernos"));
}

TEST(SettingsRows, anEmptyQueryShowsEveryRow) {
    EXPECT_TRUE(matches("", "Tema", "", "Aparência"));
    EXPECT_TRUE(matches("   ", "Tema", "", "Aparência"));
}

TEST(SettingsRows, theHomeFolderIsATilde) {
    EXPECT_EQ(shownPath("/home/ana/Estudos/cofre", "/home/ana"), "~/Estudos/cofre");
    EXPECT_EQ(shownPath("/home/ana", "/home/ana"), "~");
    // another folder that only starts with the same letters is not inside the home
    EXPECT_EQ(shownPath("/home/ana2/Estudos", "/home/ana"), "/home/ana2/Estudos");
    EXPECT_EQ(shownPath("/mnt/disco/cadernos", "/home/ana"), "/mnt/disco/cadernos");
    EXPECT_EQ(shownPath("/mnt/x", ""), "/mnt/x");
}

TEST(SettingsRows, theVaultIsTheOneInTheConfigOfSumma) {
    EXPECT_EQ(vaultPathOf(R"({"vaultPath":"/home/ana/Estudos/cofre","courses":[]})"), "/home/ana/Estudos/cofre");
    EXPECT_EQ(vaultPathOf(R"({"courses":[]})"), "");
    EXPECT_EQ(vaultPathOf(R"({"vaultPath":3})"), "");
    EXPECT_EQ(vaultPathOf("not json"), "");
    EXPECT_EQ(vaultPathOf(""), "");
}

TEST(SettingsRows, everyPaperKeepsItsIdAndComesBack) {
    for (Paper p: ambitus::newnotebook::allPapers()) {
        auto back = paperFromId(paperId(p));
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(*back, p);
    }
    EXPECT_EQ(std::string(paperId(Paper::SUMMA)), "summa");
    EXPECT_FALSE(paperFromId("cork"));
    EXPECT_FALSE(paperFromId(""));
}
