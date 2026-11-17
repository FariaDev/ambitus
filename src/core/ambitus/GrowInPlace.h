/*
 * Ambitus
 *
 * Growing a page in place. A board grows at its right and bottom edges and its strokes do not move, so the buffer of
 * the page keeps its pixels: they are copied into the buffer of the grown page, and only the areas that can differ are
 * rendered again. Those are the strip along the old edges, where a dotted grid is cut back to the old size, the part of
 * the page the buffer did not hold, and the rectangles that changed. Every other pixel is the one of the old buffer,
 * which is also what a whole render of the grown page gives there.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <vector>

#include <cairo.h>

#include "model/PageType.h"
#include "util/Range.h"

namespace ambitus::grow {

/// Width, in page units, of the strip along the old right and bottom edges that is rendered again. A dotted grid stops
/// half a line width short of the edge, so a dot that was cut off before may come back once the page is larger.
constexpr double EDGE_STRIP = 16.0;

/// What a buffer holds: the part of the page, the page size it was rendered for, and the scale of the render
struct Geometry {
    Range range;
    double pageWidth = 0.0;
    double pageHeight = 0.0;
    double zoom = 1.0;
    int dpi = 1;
};

/// Whether the pixels of the background stay the same when the page grows, apart from the strip: plain paper and
/// dotted grids. Other backgrounds (ruled, graph, PDF) are rendered whole.
bool backgroundKeepsPixels(const PageType& type);

/// Whether the pixels of `kept` can be copied into a buffer of `wanted`: the page has grown, and the buffer keeps the
/// same origin and scale, and covers at least what `kept` covers.
bool canKeep(const Geometry& kept, const Geometry& wanted);

/// The areas of wanted.range to render again after the kept pixels are copied, in page coordinates: the strip along the
/// old edges, the part of the page the kept buffer did not hold, and the changed rectangles, all within wanted.range.
std::vector<Range> dirtyAreas(const Geometry& kept, const Geometry& wanted, const std::vector<Range>& changed);

/// Renders each area into `target`, a context of a fresh buffer for `wanted`: an area is cleared, then rendered with
/// `render` clipped to its whole device pixels. The pixels inside an area are those of a render of the whole page,
/// except for the antialiasing of a stroke that the edge of the area cuts: cairo rounds that differently (a few pixels
/// of the strokes along the edges, by up to some levels of 255; see the unit tests).
void renderAreas(cairo_t* target, const Geometry& wanted, const std::vector<Range>& areas,
                 const std::function<void(cairo_t*)>& render);

}  // namespace ambitus::grow
