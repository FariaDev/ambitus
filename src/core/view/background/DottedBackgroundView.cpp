#include "DottedBackgroundView.h"

#include <memory>  // for allocator

#include "ambitus/DotTiles.h"                        // for tiles::paintTiled, tiles::DotTileParams
#include "model/BackgroundConfig.h"                  // for BackgroundConfig
#include "view/background/BackgroundView.h"          // for view
#include "view/background/OneColorBackgroundView.h"  // for OneColorBackgrou...
#include "view/background/PlainBackgroundView.h"     // for PlainBackgroundView

using namespace background_config_strings;
using namespace xoj::view;

DottedBackgroundView::DottedBackgroundView(double pageWidth, double pageHeight, Color backgroundColor,
                                           const BackgroundConfig& config):
        OneColorBackgroundView(pageWidth, pageHeight, backgroundColor, config, DEFAULT_LINE_WIDTH, DEFAULT_LINE_COLOR,
                               ALT_DEFAULT_LINE_COLOR) {

    config.loadValue(CFG_RASTER, squareSize);
}

void DottedBackgroundView::draw(cairo_t* cr) const {
    // Paint the background color
    PlainBackgroundView::draw(cr);

    // Ambitus: on an image target the dots are copied from cached tiles (ambitus/DotTiles.h), which avoids stroking
    // every dot again at each repaint of a large page. Other targets (PDF, SVG) keep the dots as vectors.
    const ::ambitus::tiles::DotTileParams params{
            .squareSize = squareSize,
            .lineWidth = lineWidth,
            .color = static_cast<uint32_t>(foregroundColor),
            .pageWidth = pageWidth,
            .pageHeight = pageHeight,
            .edgeClearance = 0.5 * lineWidth,  // the clearance strokeDots() gives the page edges
    };
    const auto paintTile = [this](cairo_t* tileCr, const ::ambitus::tiles::PageRect& rect) {
        strokeDots(tileCr, rect.minX, rect.minY, rect.maxX, rect.maxY);
    };
    if (::ambitus::tiles::paintTiled(cr, params, paintTile)) {
        return;
    }

    // Get the bounds of the mask, in page coordinates
    double minX;
    double maxX;
    double minY;
    double maxY;
    cairo_clip_extents(cr, &minX, &minY, &maxX, &maxY);
    strokeDots(cr, minX, minY, maxX, maxY);
}

void DottedBackgroundView::strokeDots(cairo_t* cr, double minX, double minY, double maxX, double maxY) const {
    const double halfLineWidth = 0.5 * lineWidth;
    /*  1- Add a halfLineWidth padding in case the line is just outside the mask but its thickness still makes it
     *     (partially) visible
     *  2- Set clearance so no dot is half-drawn on the edge of the sheet
     */
    auto [indexMinX, indexMaxX] =
            getIndexBounds(minX - halfLineWidth, maxX + halfLineWidth, squareSize, halfLineWidth, pageWidth);
    auto [indexMinY, indexMaxY] =
            getIndexBounds(minY - halfLineWidth, maxY + halfLineWidth, squareSize, halfLineWidth, pageHeight);

    for (int i = indexMinX; i <= indexMaxX; ++i) {
        double x = i * squareSize;
        for (int j = indexMinY; j <= indexMaxY; ++j) {
            double y = j * squareSize;
            cairo_move_to(cr, x, y);
            cairo_line_to(cr, x, y);
        }
    }

    cairo_save(cr);
    Util::cairo_set_source_rgbi(cr, foregroundColor);
    cairo_set_line_width(cr, lineWidth);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_stroke(cr);
    cairo_restore(cr);
}
