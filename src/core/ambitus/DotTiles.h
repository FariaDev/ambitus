/*
 * Ambitus
 *
 * Dotted grids on image surfaces. Cairo spends most of the time stroking
 * thousands of zero-length round-capped segments, one per dot, and a board
 * page is repainted through the same grid for every stroke. The dots are
 * therefore painted once into square tiles of device pixels, which are kept
 * in a cache and copied onto the target at whole-pixel offsets.
 *
 * Each tile is painted with the target's matrix moved by a whole number of
 * device pixels, so every dot lands on the same sub-pixel position as when it
 * is painted directly, and the copy is exact. Vector targets (PDF, SVG,
 * recordings) and anything else that is not an image are not tiled: paintTiled
 * returns false and the caller paints directly.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include <cairo.h>

namespace ambitus::tiles {

/// Side of a tile, in device pixels (128 x 128 x 4 bytes = 64 KiB)
constexpr int DOT_TILE_SIZE = 128;
/// Bytes the cache may hold: 1024 tiles
constexpr std::size_t DOT_TILE_BUDGET = 64u * 1024u * 1024u;
/// Above this many tiles in one paint, the caller draws directly
constexpr long MAX_TILES_PER_PAINT = 4096;

/**
 * Everything that the pixels of a tile depend on, apart from its position. The tiles lie on the pixel grid of the
 * page, which starts at the page origin: a mask of the same page and zoom, at any whole-pixel offset, shares them.
 */
struct DotTileParams {
    // Scale from user space to the pixels of the page: pixel = xx * u, yy * v
    double xx = 1.0;
    double yy = 1.0;
    // Settings of the grid painted in the tile
    double squareSize = 0.0;
    double lineWidth = 0.0;
    uint32_t color = 0;
    // The grid is not painted within edgeClearance of the right and bottom edges of the page. The page size only counts
    // for the tiles that reach there: the others are the same whatever the page size.
    double pageWidth = 0.0;
    double pageHeight = 0.0;
    double edgeClearance = 0.0;
    // Settings of the context the tile is painted with
    int antialias = 0;
    double tolerance = 0.1;

    bool operator==(const DotTileParams& other) const = default;
};

struct DotTileKey {
    DotTileParams params;
    int tileX = 0;
    int tileY = 0;

    bool operator==(const DotTileKey& other) const = default;
};

struct DotTileKeyHash {
    auto operator()(const DotTileKey& key) const -> std::size_t;
};

/// A rectangle in page coordinates
struct PageRect {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
};

/// Paints into a tile the part of the grid that lies in rect. The context is in tile space: page coordinates map to
/// device pixels of the tile through the target's matrix, moved to the tile's origin.
using TilePainter = std::function<void(cairo_t* tileCr, const PageRect& rect)>;

/// The interval [min, max] of scale * v + offset for v in [a, b]
inline auto mapInterval(double a, double b, double scale, double offset) -> std::pair<double, double> {
    const double p = scale * a + offset;
    const double q = scale * b + offset;
    return {std::min(p, q), std::max(p, q)};
}

/// The interval of v for which scale * v + offset lies in [a, b]
inline auto unmapInterval(double a, double b, double scale, double offset) -> std::pair<double, double> {
    const double p = (a - offset) / scale;
    const double q = (b - offset) / scale;
    return {std::min(p, q), std::max(p, q)};
}

/// The tile indices, inclusive, whose tiles overlap [minDevice, maxDevice). First > last when that is empty.
inline auto tileIndexRange(double minDevice, double maxDevice, int tileSize) -> std::pair<int, int> {
    if (!(maxDevice > minDevice)) {
        return {0, -1};
    }
    const int first = static_cast<int>(std::floor(minDevice / tileSize));
    const int last = static_cast<int>(std::ceil(maxDevice / tileSize)) - 1;
    return {first, last};
}

/**
 * Paints the grid of `params` onto cr, for the part of cr's clip, from cached tiles.
 *
 * Returns false, and paints nothing, when cr does not draw into an image surface, when it uses another operator
 * than OVER, when its matrix is not axis-aligned, or when the clip covers too many tiles. The caller then paints
 * directly. The matrix fields of `params` are taken from cr.
 */
auto paintTiled(cairo_t* cr, DotTileParams params, const TilePainter& painter) -> bool;

}  // namespace ambitus::tiles
