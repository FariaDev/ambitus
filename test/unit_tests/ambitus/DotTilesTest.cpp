#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include <cairo.h>
#include <gtest/gtest.h>

#include "ambitus/DotTiles.h"

using namespace ambitus::tiles;

namespace {

constexpr double SQUARE = 14.17;
constexpr double LINE_WIDTH = 1.5;

/// Strokes the dots of a grid that lie in rect (the same dots as DottedBackgroundView for that rectangle)
void strokeDots(cairo_t* cr, const PageRect& rect) {
    const int firstI = static_cast<int>(std::ceil(rect.minX / SQUARE)) - 1;
    const int lastI = static_cast<int>(std::floor(rect.maxX / SQUARE)) + 1;
    const int firstJ = static_cast<int>(std::ceil(rect.minY / SQUARE)) - 1;
    const int lastJ = static_cast<int>(std::floor(rect.maxY / SQUARE)) + 1;
    for (int i = firstI; i <= lastI; ++i) {
        for (int j = firstJ; j <= lastJ; ++j) {
            cairo_move_to(cr, i * SQUARE, j * SQUARE);
            cairo_line_to(cr, i * SQUARE, j * SQUARE);
        }
    }
    cairo_save(cr);
    cairo_set_source_rgb(cr, 0.2, 0.3, 0.4);
    cairo_set_line_width(cr, LINE_WIDTH);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_stroke(cr);
    cairo_restore(cr);
}

DotTileParams gridParams() {
    DotTileParams params;
    params.squareSize = SQUARE;
    params.lineWidth = LINE_WIDTH;
    params.color = 0xff405060;
    params.pageWidth = 1602;
    params.pageHeight = 1008;
    return params;
}

enum class Clip { None, WholePixels, Fractional };

struct Render {
    std::vector<unsigned char> bytes;
    int nonWhite = 0;
    bool tiled = false;
};

/**
 * A page painted as RenderJob paints a board page: white paper, then the grid, on an ARGB32 image of dpi-scaled
 * pixels, with the matrix of Mask (translate by -offset, then scale by zoom). With useTiles, the grid goes through
 * paintTiled, and falls back to the direct paint as the caller does.
 */
Render paintPage(bool useTiles, int dpi, double zoom, int xOffset, int yOffset, Clip clip) {
    constexpr int DEVICE_W = 500;
    constexpr int DEVICE_H = 400;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, DEVICE_W * dpi, DEVICE_H * dpi);
    cairo_surface_set_device_scale(surface, dpi, dpi);
    cairo_t* cr = cairo_create(surface);
    cairo_translate(cr, -xOffset, -yOffset);
    cairo_scale(cr, zoom, zoom);

    if (clip != Clip::None) {
        // Edges in logical device pixels, mapped back to user space
        const double left = clip == Clip::WholePixels ? 8.0 : 7.6;
        const double top = clip == Clip::WholePixels ? 5.0 : 5.1;
        const double right = clip == Clip::WholePixels ? 480.0 : 480.2;
        const double bottom = clip == Clip::WholePixels ? 390.0 : 390.7;
        cairo_rectangle(cr, (left + xOffset) / zoom, (top + yOffset) / zoom, (right - left) / zoom,
                        (bottom - top) / zoom);
        cairo_clip(cr);
    }

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    Render result;
    if (useTiles) {
        result.tiled =
                paintTiled(cr, gridParams(), [](cairo_t* tileCr, const PageRect& rect) { strokeDots(tileCr, rect); });
    }
    if (!result.tiled) {
        double minX = 0;
        double minY = 0;
        double maxX = 0;
        double maxY = 0;
        cairo_clip_extents(cr, &minX, &minY, &maxX, &maxY);
        strokeDots(cr, PageRect{minX, minY, maxX, maxY});
    }

    cairo_surface_flush(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const int rows = cairo_image_surface_get_height(surface);
    const auto* data = cairo_image_surface_get_data(surface);
    result.bytes.assign(data, data + static_cast<std::size_t>(stride) * rows);
    for (int row = 0; row < rows; ++row) {
        for (int x = 0; x < stride / 4; ++x) {
            uint32_t pixel = 0;
            std::memcpy(&pixel, data + row * stride + 4 * x, 4);
            result.nonWhite += pixel != 0xffffffffu;
        }
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return result;
}

/// The largest difference of a channel between two renders of the same size
int maxDifference(const Render& a, const Render& b) {
    int maxDiff = 0;
    for (std::size_t i = 0; i < a.bytes.size() && i < b.bytes.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(int(a.bytes[i]) - int(b.bytes[i])));
    }
    return maxDiff;
}

}  // namespace

TEST(TileIndexRange, coversTheTilesOverlappingTheRange) {
    EXPECT_EQ(tileIndexRange(0, 128, 128), (std::pair<int, int>{0, 0}));
    EXPECT_EQ(tileIndexRange(0, 128.5, 128), (std::pair<int, int>{0, 1}));
    EXPECT_EQ(tileIndexRange(127.9, 128.1, 128), (std::pair<int, int>{0, 1}));
    EXPECT_EQ(tileIndexRange(-1, 1, 128), (std::pair<int, int>{-1, 0}));
    EXPECT_EQ(tileIndexRange(-300, -200, 128), (std::pair<int, int>{-3, -2}));
}

TEST(TileIndexRange, anEmptyRangeHasNoTile) {
    EXPECT_EQ(tileIndexRange(256, 256, 128), (std::pair<int, int>{0, -1}));
    EXPECT_EQ(tileIndexRange(300, 200, 128), (std::pair<int, int>{0, -1}));
}

TEST(UnmapInterval, isTheInverseOfMapInterval) {
    const auto [minD, maxD] = mapInterval(0, 10, -2, 5);
    EXPECT_EQ(minD, -15);
    EXPECT_EQ(maxD, 5);
    const auto [minP, maxP] = unmapInterval(minD, maxD, -2, 5);
    EXPECT_EQ(minP, 0);
    EXPECT_EQ(maxP, 10);
}

TEST(DotTileKey, aKeyIsItsParamsAndPosition) {
    const DotTileParams params = gridParams();
    const DotTileKey a{params, 3, -2};
    const DotTileKey same{params, 3, -2};
    EXPECT_EQ(a, same);
    EXPECT_EQ(DotTileKeyHash{}(a), DotTileKeyHash{}(same));

    EXPECT_NE(a, (DotTileKey{params, 4, -2}));
    DotTileParams zoomed = params;
    zoomed.xx = 0.73;
    EXPECT_NE(a, (DotTileKey{zoomed, 3, -2}));
    DotTileParams shifted = params;
    shifted.x0 = 1.0;
    EXPECT_NE(a, (DotTileKey{shifted, 3, -2}));
    DotTileParams recolored = params;
    recolored.color = 0xff000000;
    EXPECT_NE(a, (DotTileKey{recolored, 3, -2}));
}

TEST(DotTiles, theTiledPaintMatchesTheDirectPaint) {
    struct Case {
        int dpi;
        double zoom;
        int xOffset;
        int yOffset;
    };
    // Board pages at 100 %, 29 % and 73 %, on screen and on HiDPI, with offsets that are not on tile boundaries
    const std::vector<Case> cases = {
            {1, 1.0, 0, 0}, {1, 0.73, 137, -53}, {1, 0.29, 37, 512}, {2, 1.0, 1000, 0}, {2, 0.29, 37, 512},
    };
    for (const Case& c: cases) {
        for (const Clip clip: {Clip::None, Clip::WholePixels}) {
            const Render direct = paintPage(false, c.dpi, c.zoom, c.xOffset, c.yOffset, clip);
            const Render tiled = paintPage(true, c.dpi, c.zoom, c.xOffset, c.yOffset, clip);
            // The tiles are used, and the grid is really there to compare
            EXPECT_TRUE(tiled.tiled);
            EXPECT_GT(direct.nonWhite, 0);
            ASSERT_EQ(direct.bytes.size(), tiled.bytes.size());
            // Equal up to the rounding of a single channel value by cairo
            EXPECT_LE(maxDifference(direct, tiled), 1)
                    << "dpi " << c.dpi << ", zoom " << c.zoom << ", clip " << static_cast<int>(clip);
        }
    }
}

TEST(DotTiles, aSecondPaintFromTheCacheIsTheSame) {
    const Render first = paintPage(true, 1, 0.73, 137, -53, Clip::None);
    const Render second = paintPage(true, 1, 0.73, 137, -53, Clip::None);
    EXPECT_EQ(maxDifference(first, second), 0);
}

TEST(DotTiles, aFractionalClipIsLeftToTheDirectPaint) {
    const Render tiled = paintPage(true, 1, 0.73, 137, -53, Clip::Fractional);
    EXPECT_FALSE(tiled.tiled);
}

TEST(DotTiles, aVectorTargetIsNotTiled) {
    cairo_surface_t* recording = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, nullptr);
    cairo_t* cr = cairo_create(recording);
    cairo_rectangle(cr, 0, 0, 100, 100);
    cairo_clip(cr);
    bool painted = false;
    EXPECT_FALSE(paintTiled(cr, gridParams(), [&painted](cairo_t*, const PageRect&) { painted = true; }));
    EXPECT_FALSE(painted);
    cairo_destroy(cr);
    cairo_surface_destroy(recording);
}

TEST(DotTiles, anotherOperatorOrARotationIsNotTiled) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 100);
    cairo_t* cr = cairo_create(surface);
    bool painted = false;
    const auto painter = [&painted](cairo_t*, const PageRect&) { painted = true; };

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    EXPECT_FALSE(paintTiled(cr, gridParams(), painter));
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_rotate(cr, 0.1);
    EXPECT_FALSE(paintTiled(cr, gridParams(), painter));
    EXPECT_FALSE(painted);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
