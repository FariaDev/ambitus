#include "SummaCrop.h"

#include <algorithm>  // for max, min
#include <cmath>      // for ceil, cos, floor, sin, abs

namespace ambitus::summacrop {

namespace {
/// The part of the rectangle inside the page; nothing when there is none
std::optional<Rect> clampToPage(const Rect& r, double pageWidth, double pageHeight) {
    const double x0 = std::max(0.0, r.x);
    const double y0 = std::max(0.0, r.y);
    const double x1 = std::min(pageWidth, r.x + r.width);
    const double y1 = std::min(pageHeight, r.y + r.height);
    if (x1 <= x0 || y1 <= y0) {
        return std::nullopt;
    }
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

/// The whole page, or a part of it that is most of it
bool coversMostOfPage(const Rect& part, double pageWidth, double pageHeight) {
    return part.width * part.height >= WHOLE_PAGE_SHARE * pageWidth * pageHeight;
}
}  // namespace

SendMode sendModeFromId(const std::string& id) { return id == "page" ? SendMode::Page : SendMode::Screen; }

const char* sendModeId(SendMode mode) { return mode == SendMode::Page ? "page" : "screen"; }

Rect rotatedBounds(const Rect& r, double rotation) {
    const double s = std::abs(std::sin(rotation));
    const double c = std::abs(std::cos(rotation));
    const double w = r.width * c + r.height * s;
    const double h = r.width * s + r.height * c;
    const double cx = r.x + r.width / 2.0;
    const double cy = r.y + r.height / 2.0;
    return Rect{cx - w / 2.0, cy - h / 2.0, w, h};
}

std::optional<Rect> fitToPage(const Rect& r, double pageWidth, double pageHeight) {
    // the edges of the page are whole points too: a page that ends at 595.28 pt ends at 595 here
    const double x0 = std::max(0.0, std::floor(r.x));
    const double y0 = std::max(0.0, std::floor(r.y));
    const double x1 = std::min(std::ceil(r.x + r.width), std::floor(pageWidth));
    const double y1 = std::min(std::ceil(r.y + r.height), std::floor(pageHeight));
    if (x1 - x0 < 1 || y1 - y0 < 1) {
        return std::nullopt;
    }
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

std::optional<Rect> selectionCrop(const SelectionOnPage& selection, double margin) {
    if (selection.width <= 0 || selection.height <= 0) {
        return std::nullopt;
    }
    const Rect b = selection.bounds;
    const Rect grown{b.x - margin, b.y - margin, b.width + 2 * margin, b.height + 2 * margin};
    // always cropped, whatever the page share: the selection is what goes
    auto part = clampToPage(grown, selection.width, selection.height);
    if (!part) {
        return std::nullopt;
    }
    return fitToPage(*part, selection.width, selection.height);
}

std::optional<Rect> screenCrop(const OnScreen& page) {
    if (page.width <= 0 || page.height <= 0) {
        return std::nullopt;
    }
    auto part = clampToPage(page.visible, page.width, page.height);
    if (!part || coversMostOfPage(*part, page.width, page.height)) {
        return std::nullopt;
    }
    return fitToPage(*part, page.width, page.height);
}

std::optional<size_t> largestVisible(const std::vector<OnScreen>& pages) {
    std::optional<size_t> best;
    double bestArea = 0;
    for (size_t i = 0; i < pages.size(); i++) {
        const double area = pages[i].visible.width * pages[i].visible.height;
        if (area > bestArea) {
            best = i;
            bestArea = area;
        }
    }
    return best;
}

Pick pick(size_t currentPage, const std::vector<OnScreen>& onScreen, const std::optional<SelectionOnPage>& selection,
          SendMode mode, bool useSelection) {
    if (useSelection && selection) {
        return Pick{selection->page, selectionCrop(*selection), true};
    }
    if (mode == SendMode::Screen) {
        if (auto best = largestVisible(onScreen)) {
            const OnScreen& page = onScreen[*best];
            return Pick{page.page, screenCrop(page), false};
        }
    }
    return Pick{currentPage, std::nullopt, false};
}

}  // namespace ambitus::summacrop
