#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ambitus/SideTab.h"
#include "ambitus/Tabs.h"

using namespace ambitus;
using namespace ambitus::sidetab;

TEST(SideTab, insertingBeforeTheSideTabMovesItOnePlaceRight) {
    EXPECT_EQ(afterInsert(2, 0), 3u);
    EXPECT_EQ(afterInsert(2, 2), 3u);  // inserted where the side tab was: it moves
    EXPECT_EQ(afterInsert(2, 3), 2u);  // inserted after it: it stays
    EXPECT_EQ(afterInsert(NO_TAB, 0), NO_TAB);
}

TEST(SideTab, closingATabLeftOfTheSideTabShiftsItLeft) {
    EXPECT_EQ(afterErase(3, 1), 2u);
    EXPECT_EQ(afterErase(3, 0), 2u);
}

TEST(SideTab, closingATabRightOfTheSideTabLeavesItWhere) {
    EXPECT_EQ(afterErase(1, 4), 1u);
    EXPECT_EQ(afterErase(1, 2), 1u);
}

TEST(SideTab, closingTheSideTabClearsIt) {
    EXPECT_EQ(afterErase(2, 2), NO_TAB);
    EXPECT_EQ(afterErase(NO_TAB, 0), NO_TAB);
}

TEST(SideTab, activatingTheSideTabSwapsItWithTheOneInTheWindow) {
    // tabs A, B, C with A in the window and B beside: activating B makes it the one in the window, and A goes beside
    EXPECT_EQ(afterSwap(1, 1, 0), 0u);
    // nothing in the window: nothing goes beside
    EXPECT_EQ(afterSwap(1, 1, NO_TAB), NO_TAB);
}

TEST(SideTab, activatingAnotherTabLeavesTheSideTabAlone) {
    EXPECT_EQ(afterSwap(1, 2, 0), 1u);
    EXPECT_EQ(afterSwap(NO_TAB, 2, 0), NO_TAB);
}

TEST(SideTab, theSideTabKeepsItsNotebookAcrossOpenAndClose) {
    std::vector<std::string> tabs = {"A", "B", "C"};
    size_t side = 1;  // B beside, C in the window
    ASSERT_EQ(tabs[side], "B");

    // a notebook opened before the others pushes the side one place right
    tabs.insert(tabs.begin(), "Z");
    side = afterInsert(side, 0);
    EXPECT_EQ(tabs[side], "B");

    // a notebook closed left of it pulls it back
    tabs.erase(tabs.begin());
    side = afterErase(side, 0);
    EXPECT_EQ(tabs[side], "B");

    // a notebook closed right of it does not move it
    tabs.erase(tabs.begin() + 2);
    side = afterErase(side, 2);
    EXPECT_EQ(tabs[side], "B");

    // the side notebook closed: nothing is beside any more
    tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(side));
    side = afterErase(side, side);
    EXPECT_EQ(side, NO_TAB);
}
