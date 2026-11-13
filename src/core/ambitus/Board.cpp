#include "Board.h"

#include <algorithm>
#include <cmath>

#include "control/settings/PageTemplateSettings.h"
#include "model/BackgroundConfig.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/XojPage.h"

namespace ambitus::board {

PageType pageType() {
    PageType t(PageTypeFormat::Dotted);
    t.config = std::string("r1=") + std::to_string(static_cast<int>(RASTER)) + "," + KEY + "=" + VALUE;
    return t;
}

bool isBoardPageType(const PageType& t) {
    std::string v;
    return BackgroundConfig(t.config).loadValue(KEY, v) && v == VALUE;
}

bool isBoard(const Document& doc) {
    return doc.getPageCount() == 1 && isBoardPageType(doc.getPage(0)->getBackgroundType());
}

PageTemplateSettings pageTemplate(const PageTemplateSettings& base) {
    PageTemplateSettings t = base;
    t.setCopyLastPageSettings(false);
    t.setCopyLastPageSize(false);
    t.setPageWidth(START_WIDTH);
    t.setPageHeight(START_HEIGHT);
    t.setBackgroundType(pageType());
    return t;
}

namespace {
double grow(double size, double reach) {
    if (reach + MARGIN <= size) {
        return size;
    }
    return std::ceil((reach + MARGIN) / RASTER) * RASTER;
}
}  // namespace

Size grownToFit(Size page, double right, double bottom) { return {grow(page.width, right), grow(page.height, bottom)}; }

Size shiftToFit(const Range& content) {
    if (!content.isValid()) {
        return {0, 0};
    }
    auto room = [](double from) { return from >= MARGIN ? 0.0 : std::ceil((MARGIN - from) / RASTER) * RASTER; };
    return {room(content.minX), room(content.minY)};
}

Range contentBounds(const XojPage& page) {
    Range r;
    for (const Layer* layer: page.getLayersView()) {
        for (const Element* e: layer->getElementsView()) {
            r = r.unite(Range(e->getBoundingBox()));
        }
    }
    return r;
}

double zoomToFit(const Range& content, double viewWidth, double viewHeight, double maxZoom) {
    if (!content.isValid() || viewWidth <= 0 || viewHeight <= 0) {
        return maxZoom;
    }
    constexpr double AIR = 0.9;  // the content fills 90 % of the view
    double z = std::min(viewWidth / std::max(content.getWidth(), 1.0), viewHeight / std::max(content.getHeight(), 1.0));
    return std::min(z * AIR, maxZoom);
}

Range previewArea(const Range& content, double pageWidth, double pageHeight) {
    if (!content.isValid()) {
        return Range(0, 0, pageWidth, pageHeight);
    }
    constexpr double AIR = 2 * RASTER;
    return Range(std::max(content.minX - AIR, 0.0), std::max(content.minY - AIR, 0.0),
                 std::min(content.maxX + AIR, pageWidth), std::min(content.maxY + AIR, pageHeight));
}

double exportZoom(const Range& area, double zoom) {
    const double pixels = area.getWidth() * area.getHeight() * zoom * zoom;
    if (pixels <= MAX_EXPORT_PIXELS || pixels <= 0) {
        return zoom;
    }
    return std::sqrt(MAX_EXPORT_PIXELS / (area.getWidth() * area.getHeight()));
}

Size contentReach(const XojPage& page) {
    Size r{0, 0};
    for (const Layer* layer: page.getLayersView()) {
        for (const Element* e: layer->getElementsView()) {
            const auto& b = e->getBoundingBox();
            r.width = std::max(r.width, b.x + b.width);
            r.height = std::max(r.height, b.y + b.height);
        }
    }
    return r;
}

}  // namespace ambitus::board
