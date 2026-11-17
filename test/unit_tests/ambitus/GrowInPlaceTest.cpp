#include <algorithm>
#include <cstdlib>
#include <vector>

#include <cairo.h>
#include <gtest/gtest.h>

#include "ambitus/GrowInPlace.h"
#include "model/PageType.h"
#include "util/Range.h"
#include "view/Mask.h"

using namespace ambitus::grow;
using xoj::view::Mask;

namespace {

constexpr double SQUARE = 14.17;
constexpr double LINE_WIDTH = 1.5;

struct Stroke {
    double x1, y1, x2, y2;
};

/// Paints a page the way a dotted page is painted: paper, the grid cut back by half a line width at the right and
/// bottom edges, and the strokes of ink on top, each one on its own
void paintPage(cairo_t* cr, double width, double height, const std::vector<Stroke>& ink) {
    cairo_save(cr);
    cairo_set_source_rgb(cr, 0.98, 0.97, 0.9);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    const double halfWidth = 0.5 * LINE_WIDTH;
    cairo_set_source_rgb(cr, 0.3, 0.35, 0.4);
    cairo_set_line_width(cr, LINE_WIDTH);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i * SQUARE <= width - halfWidth; ++i) {
        for (int j = 0; j * SQUARE <= height - halfWidth; ++j) {
            cairo_move_to(cr, i * SQUARE, j * SQUARE);
            cairo_line_to(cr, i * SQUARE, j * SQUARE);
        }
    }
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.8, 0.1, 0.1);
    cairo_set_line_width(cr, 2.5);
    for (const Stroke& s: ink) {
        cairo_move_to(cr, s.x1, s.y1);
        cairo_line_to(cr, s.x2, s.y2);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

/// A buffer of the page, rendered whole
Mask renderWhole(const Geometry& g, const std::vector<Stroke>& ink) {
    Mask mask(g.dpi, g.range, g.zoom, CAIRO_CONTENT_COLOR_ALPHA);
    paintPage(mask.get(), g.pageWidth, g.pageHeight, ink);
    return mask;
}

/// The buffer of the grown page, made as RenderJob makes it: the kept pixels copied, then the dirty areas rendered
Mask growKept(const Mask& kept, const Geometry& keptGeometry, const Geometry& wanted, const std::vector<Stroke>& ink,
              const std::vector<Range>& changed) {
    Mask grown(wanted.dpi, wanted.range, wanted.zoom, CAIRO_CONTENT_COLOR_ALPHA);
    kept.paintTo(grown.get());
    renderAreas(grown.get(), wanted, dirtyAreas(keptGeometry, wanted, changed),
                [&](cairo_t* cr) { paintPage(cr, wanted.pageWidth, wanted.pageHeight, ink); });
    return grown;
}

/// How two buffers of the same size differ: the largest difference of their bytes, and how many bytes differ
struct Difference {
    int largest = 0;
    int bytes = 0;
};

Difference differenceOf(Mask& a, Mask& b) {
    cairo_surface_t* sa = cairo_get_target(a.get());
    cairo_surface_t* sb = cairo_get_target(b.get());
    cairo_surface_flush(sa);
    cairo_surface_flush(sb);
    const int width = cairo_image_surface_get_width(sa);
    const int height = cairo_image_surface_get_height(sa);
    EXPECT_EQ(width, cairo_image_surface_get_width(sb));
    EXPECT_EQ(height, cairo_image_surface_get_height(sb));
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    const unsigned char* da = cairo_image_surface_get_data(sa);
    const unsigned char* db = cairo_image_surface_get_data(sb);
    const int strideA = cairo_image_surface_get_stride(sa);
    const int strideB = cairo_image_surface_get_stride(sb);
    Difference result;
    for (int y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < rowBytes; ++x) {
            const int d = std::abs(int(da[y * strideA + x]) - int(db[y * strideB + x]));
            result.largest = std::max(result.largest, d);
            result.bytes += d > 0;
        }
    }
    return result;
}

void expectRange(const Range& r, double minX, double minY, double maxX, double maxY) {
    EXPECT_DOUBLE_EQ(r.minX, minX);
    EXPECT_DOUBLE_EQ(r.minY, minY);
    EXPECT_DOUBLE_EQ(r.maxX, maxX);
    EXPECT_DOUBLE_EQ(r.maxY, maxY);
}

const std::vector<Stroke> INK = {{30, 40, 260, 180}, {280, 100, 300, 190}, {10, 150, 60, 195}};

}  // namespace

// The grown buffer is the whole render except for the antialiasing of the grid dots and strokes that the edges of the
// redrawn areas cut: cairo rounds a cut primitive differently. Measured: at most 1 level, in a few bytes, for a page
// that grows; at most 15 levels, in a few bytes, where a stroke crosses the edge of the kept window.
TEST(GrowInPlace, aGrownPageHasThePixelsOfAWholeRender) {
    for (int dpi: {1, 2}) {
        for (double zoom: {1.0, 1.5}) {
            SCOPED_TRACE(testing::Message() << "dpi " << dpi << ", zoom " << zoom);
            const Geometry before{Range(0, 0, 300, 200), 300, 200, zoom, dpi};
            const Geometry after{Range(0, 0, 420, 260), 420, 260, zoom, dpi};
            ASSERT_TRUE(canKeep(before, after));

            Mask kept = renderWhole(before, INK);
            Mask grown = growKept(kept, before, after, INK, {});
            Mask whole = renderWhole(after, INK);
            const Difference d = differenceOf(grown, whole);
            EXPECT_LE(d.largest, 1);
            EXPECT_LE(d.bytes, 16);
        }
    }
}

TEST(GrowInPlace, aChangedRectangleIsRenderedAgain) {
    // A stroke is added inside the old page, after the kept buffer was rendered
    const std::vector<Stroke> inkAfter = {
            {30, 40, 260, 180}, {280, 100, 300, 190}, {10, 150, 60, 195}, {100, 20, 150, 60}};
    const Geometry before{Range(0, 0, 300, 200), 300, 200, 1.0, 1};
    const Geometry after{Range(0, 0, 420, 260), 420, 260, 1.0, 1};
    Mask kept = renderWhole(before, INK);
    Mask whole = renderWhole(after, inkAfter);

    // Without the changed rectangle, the stroke is missing
    Mask withoutChange = growKept(kept, before, after, inkAfter, {});
    EXPECT_GT(differenceOf(withoutChange, whole).largest, 0);

    // With it, the grown buffer is the whole render. The rectangle covers the stroke with its half width and a pixel
    const Range changed(97, 17, 153, 63);
    Mask grown = growKept(kept, before, after, inkAfter, {changed});
    const Difference d = differenceOf(grown, whole);
    EXPECT_LE(d.largest, 1);
    EXPECT_LE(d.bytes, 16);
}

TEST(GrowInPlace, aWindowKeepsItsOriginAndGrowsItsEnd) {
    // A large page keeps only a window of itself: the window keeps its origin when the page grows
    const Geometry before{Range(40, 20, 220, 200), 300, 200, 1.5, 2};
    const Geometry after{Range(40, 20, 420, 260), 420, 260, 1.5, 2};
    ASSERT_TRUE(canKeep(before, after));

    Mask kept = renderWhole(before, INK);
    Mask grown = growKept(kept, before, after, INK, {});
    Mask whole = renderWhole(after, INK);
    const Difference d = differenceOf(grown, whole);
    EXPECT_LE(d.largest, 16);
    EXPECT_LE(d.bytes, 64);
}

TEST(GrowInPlace, onlyAGrownPageOfTheSameScaleAndOriginIsKept) {
    const Geometry page{Range(0, 0, 300, 200), 300, 200, 1.0, 1};
    EXPECT_TRUE(canKeep(page, page));

    Geometry shrunk = page;
    shrunk.pageWidth = 250;
    shrunk.range = Range(0, 0, 250, 200);
    EXPECT_FALSE(canKeep(page, shrunk));

    Geometry zoomed = page;
    zoomed.zoom = 1.5;
    EXPECT_FALSE(canKeep(page, zoomed));

    Geometry scaled = page;
    scaled.dpi = 2;
    EXPECT_FALSE(canKeep(page, scaled));

    Geometry moved{Range(40, 0, 420, 260), 420, 260, 1.0, 1};
    EXPECT_FALSE(canKeep(page, moved));

    // A window that grows with the page keeps what it held; one that would drop part of it is not kept
    const Geometry window{Range(0, 0, 200, 200), 300, 200, 1.0, 1};
    EXPECT_TRUE(canKeep(window, Geometry{Range(0, 0, 420, 260), 420, 260, 1.0, 1}));
    EXPECT_FALSE(canKeep(page, Geometry{Range(0, 0, 200, 200), 420, 260, 1.0, 1}));
}

TEST(GrowInPlace, theDirtyAreasAreTheEdgeStripsAndTheNewArea) {
    const Geometry before{Range(0, 0, 300, 200), 300, 200, 1.0, 1};
    const Geometry after{Range(0, 0, 420, 260), 420, 260, 1.0, 1};
    const auto areas = dirtyAreas(before, after, {});
    ASSERT_EQ(areas.size(), 4u);
    expectRange(areas[0], 300 - EDGE_STRIP, 0, 420, 260);  // strip along the old right edge
    expectRange(areas[1], 0, 200 - EDGE_STRIP, 420, 260);  // strip along the old bottom edge
    expectRange(areas[2], 300, 0, 420, 260);               // new area on the right
    expectRange(areas[3], 0, 200, 420, 260);               // new area at the bottom
}

TEST(GrowInPlace, anUnchangedSideHasNoNewArea) {
    const Geometry before{Range(0, 0, 300, 200), 300, 200, 1.0, 1};
    const Geometry after{Range(0, 0, 420, 200), 420, 200, 1.0, 1};
    const auto areas = dirtyAreas(before, after, {});
    EXPECT_EQ(areas.size(), 3u);  // no bottom new area: the height did not change
}

TEST(GrowInPlace, changedRectanglesAreClippedToTheWantedRange) {
    const Geometry before{Range(0, 0, 300, 200), 300, 200, 1.0, 1};
    const Geometry after{Range(0, 0, 420, 260), 420, 260, 1.0, 1};
    const auto areas = dirtyAreas(before, after, {Range(-10, -10, 50, 50), Range(500, 500, 600, 600)});
    ASSERT_EQ(areas.size(), 5u);  // the rectangle outside the range is dropped
    expectRange(areas[4], 0, 0, 50, 50);
}

TEST(GrowInPlace, aPlainOrDottedBackgroundKeepsItsPixels) {
    EXPECT_TRUE(backgroundKeepsPixels(PageType(PageTypeFormat::Plain)));
    EXPECT_TRUE(backgroundKeepsPixels(PageType(PageTypeFormat::Dotted)));
    EXPECT_FALSE(backgroundKeepsPixels(PageType(PageTypeFormat::Ruled)));
    EXPECT_FALSE(backgroundKeepsPixels(PageType(PageTypeFormat::Graph)));
    EXPECT_FALSE(backgroundKeepsPixels(PageType(PageTypeFormat::IsoDotted)));
    EXPECT_FALSE(backgroundKeepsPixels(PageType(PageTypeFormat::Pdf)));
}
