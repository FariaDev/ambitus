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

TEST(Board, zoomToFitShowsTheContentWithAir) {
    // 2000 x 500 of content in a 1000 x 800 view: the width rules, 0.5, then 90 %
    EXPECT_DOUBLE_EQ(zoomToFit(Range(100, 100, 2100, 600), 1000, 800, 2.0), 0.45);
    // small content is not blown up past maxZoom
    EXPECT_DOUBLE_EQ(zoomToFit(Range(0, 0, 10, 10), 1000, 800, 1.5), 1.5);
    // nothing on the board
    EXPECT_DOUBLE_EQ(zoomToFit(Range(), 1000, 800, 1.2), 1.2);
}

TEST(Board, thePreviewShowsTheContentNotTheWholeBoard) {
    using namespace ambitus::board;
    auto a = previewArea(Range(500, 400, 900, 600), 3000, 2000);
    EXPECT_DOUBLE_EQ(a.minX, 464);
    EXPECT_DOUBLE_EQ(a.maxY, 636);
    // near the edge it stays inside the page
    a = previewArea(Range(10, 10, 2990, 50), 3000, 2000);
    EXPECT_DOUBLE_EQ(a.minX, 0);
    EXPECT_DOUBLE_EQ(a.maxX, 3000);
    // an empty board shows itself whole
    a = previewArea(Range(), 3000, 2000);
    EXPECT_DOUBLE_EQ(a.getWidth(), 3000);
}

TEST(Board, writingNearTheLeftOrTopMovesTheContentInWholeDots) {
    using namespace ambitus::board;
    auto s = shiftToFit(Range(MARGIN, MARGIN + 40, 2000, 900));
    EXPECT_DOUBLE_EQ(s.width, 0);
    EXPECT_DOUBLE_EQ(s.height, 0);
    s = shiftToFit(Range(MARGIN - 1, 10, 2000, 900));
    EXPECT_DOUBLE_EQ(s.width, RASTER);
    EXPECT_DOUBLE_EQ(s.height, std::ceil((MARGIN - 10) / RASTER) * RASTER);
    EXPECT_GE(10 + s.height, MARGIN);
    s = shiftToFit(Range());
    EXPECT_DOUBLE_EQ(s.width, 0);
}

TEST(Board, anExportedBoardHasACeilingOfPixels) {
    using namespace ambitus::board;
    // a small written part keeps the asked resolution
    EXPECT_DOUBLE_EQ(exportZoom(Range(0, 0, 800, 600), 120.0 / 72), 120.0 / 72);
    // a board written all over is scaled down to the ceiling
    Range big(0, 0, 20000, 12000);
    double z = exportZoom(big, 120.0 / 72);
    EXPECT_LT(z, 120.0 / 72);
    EXPECT_NEAR(big.getWidth() * z * big.getHeight() * z, MAX_EXPORT_PIXELS, 1);
}
