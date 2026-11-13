#include <gtest/gtest.h>

#include "ambitus/Tabs.h"

using namespace ambitus;
using namespace ambitus::tabs;

TEST(Tabs, closingTheActiveTabGoesToTheRightThenToTheLeft) {
    // A [B] C: closing B shows C, which is now at index 1
    EXPECT_EQ(afterClose(3, 1, 1), 1u);
    // A B [C]: closing C shows B
    EXPECT_EQ(afterClose(3, 2, 2), 1u);
    // [A]: closing the last one leaves none
    EXPECT_EQ(afterClose(1, 0, 0), NO_TAB);
}

TEST(Tabs, closingAnotherTabKeepsTheActiveOne) {
    // A B [C]: closing A keeps C, which moves to index 1
    EXPECT_EQ(afterClose(3, 2, 0), 1u);
    // [A] B C: closing C keeps A
    EXPECT_EQ(afterClose(3, 0, 2), 0u);
    // no tab in the window (library) stays so
    EXPECT_EQ(afterClose(3, NO_TAB, 1), NO_TAB);
}

TEST(Tabs, aNewTabGoesNextToTheActiveOne) {
    EXPECT_EQ(insertAt(3, 0), 1u);
    EXPECT_EQ(insertAt(3, 2), 3u);
    EXPECT_EQ(insertAt(3, NO_TAB), 3u);
    EXPECT_EQ(insertAt(0, NO_TAB), 0u);
}
