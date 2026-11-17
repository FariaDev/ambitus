/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "DotTiles.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "LruCache.h"

namespace ambitus::tiles {

namespace {

using TileCache = LruCache<DotTileKey, std::shared_ptr<cairo_surface_t>, DotTileKeyHash>;

constexpr std::size_t TILE_BYTES = static_cast<std::size_t>(DOT_TILE_SIZE) * DOT_TILE_SIZE * 4;

auto cacheMutex() -> std::mutex& {
    static std::mutex mutex;
    return mutex;
}

auto tileCache() -> TileCache& {
    static TileCache cache(DOT_TILE_BUDGET);
    return cache;
}

auto lookup(const DotTileKey& key) -> std::shared_ptr<cairo_surface_t> {
    std::lock_guard<std::mutex> lock(cacheMutex());
    if (auto hit = tileCache().get(key)) {
        return *hit;
    }
    return {};
}

void store(const DotTileKey& key, std::shared_ptr<cairo_surface_t> tile) {
    std::lock_guard<std::mutex> lock(cacheMutex());
    tileCache().put(key, std::move(tile), TILE_BYTES);
}

/// Paints one tile. Rendering happens outside the lock, so that other threads can read the cache meanwhile.
auto renderTile(const DotTileKey& key, const TilePainter& painter) -> std::shared_ptr<cairo_surface_t> {
    const DotTileParams& p = key.params;
    const double tileMinX = static_cast<double>(key.tileX) * DOT_TILE_SIZE;
    const double tileMinY = static_cast<double>(key.tileY) * DOT_TILE_SIZE;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, DOT_TILE_SIZE, DOT_TILE_SIZE);
    std::shared_ptr<cairo_surface_t> owned(surface, cairo_surface_destroy);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return {};
    }

    cairo_t* tileCr = cairo_create(surface);
    cairo_set_antialias(tileCr, static_cast<cairo_antialias_t>(p.antialias));
    cairo_set_tolerance(tileCr, p.tolerance);
    // The target's matrix, moved by the tile's origin, which is a whole number of device pixels: every dot keeps its
    // sub-pixel position, and so its pixels.
    cairo_matrix_t matrix;
    cairo_matrix_init(&matrix, p.xx, 0.0, 0.0, p.yy, p.x0 - tileMinX, p.y0 - tileMinY);
    cairo_set_matrix(tileCr, &matrix);

    // The page rectangle of the tile, with the margin a dot needs to reach into it (half its width and an antialiasing
    // pixel): the dots outside it cannot touch the tile.
    const double margin = 0.5 * std::abs(p.lineWidth) * std::max(std::abs(p.xx), std::abs(p.yy)) + 2.0;
    const auto [minX, maxX] = unmapInterval(tileMinX - margin, tileMinX + DOT_TILE_SIZE + margin, p.xx, p.x0);
    const auto [minY, maxY] = unmapInterval(tileMinY - margin, tileMinY + DOT_TILE_SIZE + margin, p.yy, p.y0);
    painter(tileCr, PageRect{minX, minY, maxX, maxY});

    cairo_destroy(tileCr);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return {};
    }
    return owned;
}

}  // namespace

auto DotTileKeyHash::operator()(const DotTileKey& key) const -> std::size_t {
    std::size_t seed = 0;
    const auto combine = [&seed](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        seed ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    };
    const DotTileParams& p = key.params;
    combine(p.xx);
    combine(p.yy);
    combine(p.x0);
    combine(p.y0);
    combine(p.squareSize);
    combine(p.lineWidth);
    combine(p.color);
    combine(p.pageWidth);
    combine(p.pageHeight);
    combine(p.antialias);
    combine(p.tolerance);
    combine(key.tileX);
    combine(key.tileY);
    return seed;
}

auto paintTiled(cairo_t* cr, DotTileParams params, const TilePainter& painter) -> bool {
    // Only the pixels of an image surface can be copied; vector output keeps the dots as they are.
    if (cairo_surface_get_type(cairo_get_target(cr)) != CAIRO_SURFACE_TYPE_IMAGE ||
        cairo_surface_get_type(cairo_get_group_target(cr)) != CAIRO_SURFACE_TYPE_IMAGE) {
        return false;
    }
    if (cairo_get_operator(cr) != CAIRO_OPERATOR_OVER) {
        return false;
    }

    // The matrix of cr maps user space to logical device space; the device scale of the surface maps that to pixels.
    // Everything below works in pixels, which the tiles are made of.
    cairo_surface_t* surface = cairo_get_group_target(cr);
    double scaleX = 0.0;
    double scaleY = 0.0;
    cairo_surface_get_device_scale(surface, &scaleX, &scaleY);
    cairo_matrix_t matrix;
    cairo_get_matrix(cr, &matrix);
    if (!(scaleX > 0.0 && scaleY > 0.0 && std::isfinite(scaleX) && std::isfinite(scaleY)) || matrix.xy != 0.0 ||
        matrix.yx != 0.0 || matrix.xx == 0.0 || matrix.yy == 0.0 || !std::isfinite(matrix.xx) ||
        !std::isfinite(matrix.yy) || !std::isfinite(matrix.x0) || !std::isfinite(matrix.y0)) {
        return false;
    }
    params.xx = scaleX * matrix.xx;
    params.yy = scaleY * matrix.yy;
    params.x0 = scaleX * matrix.x0;
    params.y0 = scaleY * matrix.y0;
    params.antialias = static_cast<int>(cairo_get_antialias(cr));
    params.tolerance = cairo_get_tolerance(cr);

    double clipMinX = 0.0;
    double clipMinY = 0.0;
    double clipMaxX = 0.0;
    double clipMaxY = 0.0;
    cairo_clip_extents(cr, &clipMinX, &clipMinY, &clipMaxX, &clipMaxY);
    if (!(clipMinX < clipMaxX && clipMinY < clipMaxY)) {
        return true;  // Nothing is visible, so nothing to paint
    }
    // A clip with partial pixels along its edge is combined with the dots in another order by the tiles, which
    // changes the edge pixels: such a clip is left to the direct paint.
    cairo_rectangle_list_t* clipRects = cairo_copy_clip_rectangle_list(cr);
    const bool wholePixels = clipRects->status == CAIRO_STATUS_SUCCESS;
    cairo_rectangle_list_destroy(clipRects);
    if (!wholePixels) {
        return false;
    }

    const auto [devMinX, devMaxX] = mapInterval(clipMinX, clipMaxX, params.xx, params.x0);
    const auto [devMinY, devMaxY] = mapInterval(clipMinY, clipMaxY, params.yy, params.y0);
    const auto [firstX, lastX] = tileIndexRange(devMinX, devMaxX, DOT_TILE_SIZE);
    const auto [firstY, lastY] = tileIndexRange(devMinY, devMaxY, DOT_TILE_SIZE);
    if (lastX < firstX || lastY < firstY) {
        return true;
    }
    const long count = static_cast<long>(lastX - firstX + 1) * static_cast<long>(lastY - firstY + 1);
    if (count > MAX_TILES_PER_PAINT) {
        return false;
    }

    // Collect every tile before painting anything: if one fails, the caller paints the whole grid directly.
    std::vector<std::pair<DotTileKey, std::shared_ptr<cairo_surface_t>>> tiles;
    tiles.reserve(static_cast<std::size_t>(count));
    for (int ty = firstY; ty <= lastY; ++ty) {
        for (int tx = firstX; tx <= lastX; ++tx) {
            const DotTileKey key{params, tx, ty};
            auto tile = lookup(key);
            if (!tile) {
                tile = renderTile(key, painter);
                if (!tile) {
                    return false;
                }
                store(key, tile);
            }
            tiles.emplace_back(key, std::move(tile));
        }
    }

    // Pixel space: one user unit is one pixel of the target, so the tiles land on whole pixels
    cairo_save(cr);
    cairo_matrix_t toPixels;
    cairo_matrix_init_scale(&toPixels, 1.0 / scaleX, 1.0 / scaleY);
    cairo_set_matrix(cr, &toPixels);
    for (const auto& [key, tile]: tiles) {
        const double x = static_cast<double>(key.tileX) * DOT_TILE_SIZE;
        const double y = static_cast<double>(key.tileY) * DOT_TILE_SIZE;
        cairo_set_source_surface(cr, tile.get(), x, y);
        cairo_rectangle(cr, x, y, DOT_TILE_SIZE, DOT_TILE_SIZE);
        cairo_fill(cr);
    }
    cairo_restore(cr);
    return true;
}

}  // namespace ambitus::tiles
