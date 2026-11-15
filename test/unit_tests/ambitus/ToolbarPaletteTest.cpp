/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/ToolbarPalette.h"

using ambitus::paletteFileForToolbar;

TEST(ToolbarPalette, theXournalPresetBringsTheXournalPalette) {
    EXPECT_EQ(paletteFileForToolbar("Xournal++", "ambitus.gpl"), "xournal.gpl");
}

TEST(ToolbarPalette, theAmbitusPresetBringsTheAmbitusPaletteBack) {
    EXPECT_EQ(paletteFileForToolbar("Ambitus", "xournal.gpl"), "ambitus.gpl");
}

TEST(ToolbarPalette, theSamePaletteStays) {
    EXPECT_EQ(paletteFileForToolbar("Xournal++", "xournal.gpl"), "");
    EXPECT_EQ(paletteFileForToolbar("Ambitus", "ambitus.gpl"), "");
}

TEST(ToolbarPalette, aPaletteChosenByThePersonIsNeverReplaced) {
    EXPECT_EQ(paletteFileForToolbar("Xournal++", "xournalpp.gpl"), "");
    EXPECT_EQ(paletteFileForToolbar("Ambitus", "minha-paleta.gpl"), "");
}

TEST(ToolbarPalette, theOtherPresetsDoNotTouchThePalette) {
    EXPECT_EQ(paletteFileForToolbar("Portrait", "ambitus.gpl"), "");
    EXPECT_EQ(paletteFileForToolbar("Portrait", "xournal.gpl"), "");
    EXPECT_EQ(paletteFileForToolbar("", "ambitus.gpl"), "");
}
