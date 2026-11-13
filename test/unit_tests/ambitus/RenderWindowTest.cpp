#include <gtest/gtest.h>

#include "ambitus/RenderWindow.h"

using namespace ambitus::render;

TEST(RenderWindow, aSheetStaysWhole) {
    EXPECT_FALSE(isLarge(595, 842, 1.0, 1));  // A4
    EXPECT_FALSE(isLarge(595, 842, 2.5, 2));  // A4 at 250 %, HiDPI: 12.5 Mpx
    EXPECT_TRUE(isLarge(595, 842, 3.0, 2));   // at 300 %, 18 Mpx: windowed too
}

TEST(RenderWindow, aGrownBoardIsLarge) {
    EXPECT_TRUE(isLarge(5000, 4000, 1.0, 1));
    EXPECT_FALSE(isLarge(1602, 1008, 1.0, 1));
    EXPECT_TRUE(isLarge(1602, 1008, 4.0, 1));  // 26 Mpx
}

TEST(RenderWindow, theWindowIsTheViewPlusAViewAround) {
    Range w = window(Range(1000, 1000, 1400, 1300), 5000, 4000, 1.0, 1);
    EXPECT_DOUBLE_EQ(w.minX, 600);
    EXPECT_DOUBLE_EQ(w.maxX, 1800);
    EXPECT_DOUBLE_EQ(w.minY, 700);
    EXPECT_DOUBLE_EQ(w.maxY, 1600);
}

TEST(RenderWindow, theWindowStaysInsideThePage) {
    Range w = window(Range(0, 3800, 400, 4000), 5000, 4000, 1.0, 1);
    EXPECT_DOUBLE_EQ(w.minX, 0);
    EXPECT_DOUBLE_EQ(w.maxY, 4000);
}

TEST(RenderWindow, beforeTheFirstPaintTheTopLeftCorner) {
    Range w = window(Range(), 5000, 4000, 2.0, 1);
    EXPECT_DOUBLE_EQ(w.minX, 0);
    EXPECT_DOUBLE_EQ(w.minY, 0);
    EXPECT_DOUBLE_EQ(w.maxX, FIRST_WINDOW_PIXELS / 2.0);
}
