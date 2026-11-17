/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "GrowInPlace.h"

#include <cmath>

namespace ambitus::grow {

bool backgroundKeepsPixels(const PageType& type) {
    return !type.isPdfPage() && (type.format == PageTypeFormat::Plain || type.format == PageTypeFormat::Dotted);
}

bool canKeep(const Geometry& kept, const Geometry& wanted) {
    return kept.range.isValid() && wanted.range.isValid() && kept.pageWidth > 0.0 && kept.pageHeight > 0.0 &&
           kept.zoom == wanted.zoom && kept.dpi == wanted.dpi && wanted.pageWidth >= kept.pageWidth &&
           wanted.pageHeight >= kept.pageHeight && wanted.range.minX == kept.range.minX &&
           wanted.range.minY == kept.range.minY && wanted.range.maxX >= kept.range.maxX &&
           wanted.range.maxY >= kept.range.maxY;
}

std::vector<Range> dirtyAreas(const Geometry& kept, const Geometry& wanted, const std::vector<Range>& changed) {
    const Range& r = wanted.range;
    std::vector<Range> areas = {
            // The grid is cut back at the old edges, so its dots there may now be drawn
            Range(kept.pageWidth - EDGE_STRIP, r.minY, r.maxX, r.maxY),
            Range(r.minX, kept.pageHeight - EDGE_STRIP, r.maxX, r.maxY),
            // What the kept buffer does not hold
            Range(kept.range.maxX, r.minY, r.maxX, r.maxY),
            Range(r.minX, kept.range.maxY, r.maxX, r.maxY),
    };
    areas.insert(areas.end(), changed.begin(), changed.end());

    std::vector<Range> result;
    for (const Range& area: areas) {
        const Range inside = area.intersect(r);
        if (inside.maxX > inside.minX && inside.maxY > inside.minY) {
            result.push_back(inside);
        }
    }
    return result;
}

void renderAreas(cairo_t* target, const Geometry& wanted, const std::vector<Range>& areas,
                 const std::function<void(cairo_t*)>& render) {
    // The buffer's origin, as xoj::view::Mask places it: its context is translated by it and then scaled by the zoom
    const double xOffset = std::floor(wanted.range.minX * wanted.zoom);
    const double yOffset = std::floor(wanted.range.minY * wanted.zoom);
    const double scale = static_cast<double>(wanted.dpi);

    for (const Range& area: areas) {
        // Whole device pixels, outwards: the clip has no partly covered pixel
        const double x0 = std::floor((area.minX * wanted.zoom - xOffset) * scale);
        const double y0 = std::floor((area.minY * wanted.zoom - yOffset) * scale);
        const double x1 = std::ceil((area.maxX * wanted.zoom - xOffset) * scale);
        const double y1 = std::ceil((area.maxY * wanted.zoom - yOffset) * scale);

        cairo_save(target);
        cairo_save(target);
        // The path is recorded in device space, the logical space of the surface, whose pixels are a scale of it
        cairo_identity_matrix(target);
        cairo_new_path(target);
        cairo_rectangle(target, x0 / scale, y0 / scale, (x1 - x0) / scale, (y1 - y0) / scale);
        cairo_restore(target);
        cairo_clip(target);

        cairo_set_operator(target, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(target, 0.0, 0.0, 0.0, 0.0);
        cairo_paint(target);
        cairo_set_operator(target, CAIRO_OPERATOR_OVER);
        render(target);
        cairo_restore(target);
    }
}

}  // namespace ambitus::grow
