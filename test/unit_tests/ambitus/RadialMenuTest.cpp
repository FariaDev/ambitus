#include <cmath>

#include <gtest/gtest.h>

#include "gui/RadialMenu.h"

using ambitus::RadialMenu;

TEST(RadialMenu, itemsGoClockwiseFromTheTop) {
    const double r = RadialMenu::RADIUS;
    EXPECT_EQ(RadialMenu::itemAt(0, -r, 8), 0);  // top
    EXPECT_EQ(RadialMenu::itemAt(r, 0, 8), 2);   // right
    EXPECT_EQ(RadialMenu::itemAt(0, r, 8), 4);   // bottom
    EXPECT_EQ(RadialMenu::itemAt(-r, 0, 8), 6);  // left
    double d = r / std::sqrt(2.0);
    EXPECT_EQ(RadialMenu::itemAt(-d, -d, 8), 7);  // top left, just before wrapping to 0
}

TEST(RadialMenu, theHoleAndFarAwayChooseNothing) {
    EXPECT_EQ(RadialMenu::itemAt(0, 0, 8), -1);
    EXPECT_EQ(RadialMenu::itemAt(RadialMenu::HOLE / 2 - 1, 0, 8), -1);
    EXPECT_EQ(RadialMenu::itemAt(RadialMenu::SIZE, 0, 8), -1);
    EXPECT_EQ(RadialMenu::itemAt(RadialMenu::RADIUS, 0, 0), -1);
}

TEST(RadialMenu, slightlyOffTheItemStillPicksIt) {
    const double r = RadialMenu::RADIUS;
    // 20 degrees off the right item, inside its 45 degree sector
    double a = 20.0 * M_PI / 180.0;
    EXPECT_EQ(RadialMenu::itemAt(r * std::cos(a), r * std::sin(a), 8), 2);
}
