#include "RenderWindow.h"

namespace ambitus::render {

bool isLarge(double pageWidth, double pageHeight, double zoom, int dpiScale) {
    double s = zoom * dpiScale;
    return pageWidth * s * pageHeight * s > MAX_WHOLE_PAGE_PIXELS;
}

Range window(const Range& shown, double pageWidth, double pageHeight, double zoom, int dpiScale) {
    const Range page(0, 0, pageWidth, pageHeight);
    if (!shown.isValid()) {
        double side = FIRST_WINDOW_PIXELS / (zoom * dpiScale);
        return Range(0, 0, side, side).intersect(page);
    }
    Range r = shown;
    r.minX -= shown.getWidth();
    r.maxX += shown.getWidth();
    r.minY -= shown.getHeight();
    r.maxY += shown.getHeight();
    return r.intersect(page);
}

}  // namespace ambitus::render
