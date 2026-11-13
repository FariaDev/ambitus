/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <gdk/gdkkeysyms.h>
#include <gtest/gtest.h>

#include "ambitus/ToolKeys.h"

using namespace ambitus;

TEST(ToolKeys, eachToolHasItsLetterSmallOrCapital) {
    EXPECT_EQ(toolForKey(GDK_KEY_p), TOOL_PEN);
    EXPECT_EQ(toolForKey(GDK_KEY_P), TOOL_PEN);
    EXPECT_EQ(toolForKey(GDK_KEY_m), TOOL_HIGHLIGHTER);
    EXPECT_EQ(toolForKey(GDK_KEY_e), TOOL_ERASER);
    EXPECT_EQ(toolForKey(GDK_KEY_s), TOOL_SELECT_REGION);
    EXPECT_EQ(toolForKey(GDK_KEY_t), TOOL_TEXT);
    EXPECT_EQ(toolForKey(GDK_KEY_x), TOOL_LATEX);
}

TEST(ToolKeys, theKeysOfXournalStayWithXournal) {
    // h j k l scroll, the digits pick a colour, the space scrolls a screen
    for (unsigned int k: {GDK_KEY_h, GDK_KEY_j, GDK_KEY_k, GDK_KEY_l, GDK_KEY_1, GDK_KEY_space, GDK_KEY_Escape}) {
        EXPECT_FALSE(toolForKey(k)) << k;
    }
}

TEST(ToolKeys, theTooltipShowsTheKey) {
    EXPECT_EQ(withToolKey("Caneta", TOOL_PEN), "Caneta (P)");
    EXPECT_EQ(withToolKey("Laço", TOOL_SELECT_REGION), "Laço (S)");
    EXPECT_EQ(withToolKey("Imagem", TOOL_IMAGE), "Imagem");
}
