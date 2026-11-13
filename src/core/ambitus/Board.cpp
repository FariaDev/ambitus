#include "Board.h"

#include <cmath>

#include "control/settings/PageTemplateSettings.h"
#include "model/BackgroundConfig.h"
#include "model/Document.h"
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

}  // namespace ambitus::board
