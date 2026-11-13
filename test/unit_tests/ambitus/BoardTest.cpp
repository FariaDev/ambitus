#include <cmath>

#include <gtest/gtest.h>

#include "ambitus/Board.h"

using namespace ambitus::board;

TEST(Board, theBackgroundCarriesTheMark) {
    PageType t = pageType();
    EXPECT_EQ(t.format, PageTypeFormat::Dotted);
    EXPECT_TRUE(isBoardPageType(t));
    EXPECT_FALSE(isBoardPageType(PageType(PageTypeFormat::Dotted)));
    PageType other(PageTypeFormat::Lined);
    other.config = "f1=0xffffff,ambitus=other";
    EXPECT_FALSE(isBoardPageType(other));
}

TEST(Board, roomLeftAlonePageStays) {
    Size s{START_WIDTH, START_HEIGHT};
    Size g = grownToFit(s, 100, 100);
    EXPECT_DOUBLE_EQ(g.width, s.width);
    EXPECT_DOUBLE_EQ(g.height, s.height);
}

TEST(Board, writingNearTheEdgeGrowsInWholeDots) {
    Size s{START_WIDTH, START_HEIGHT};
    Size g = grownToFit(s, START_WIDTH - 10, 100);
    EXPECT_GE(g.width, START_WIDTH - 10 + MARGIN);
    EXPECT_DOUBLE_EQ(std::fmod(g.width, RASTER), 0.0);
    EXPECT_DOUBLE_EQ(g.height, s.height);

    g = grownToFit(s, 0, START_HEIGHT + 300);  // pasted beyond the edge
    EXPECT_GE(g.height, START_HEIGHT + 300 + MARGIN);
    EXPECT_DOUBLE_EQ(std::fmod(g.height, RASTER), 0.0);
}
