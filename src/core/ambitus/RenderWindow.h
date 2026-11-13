/*
 * Ambitus
 *
 * Render window: Xournal++ keeps each page in one buffer at the current zoom,
 * the whole page. A board grows to many times a sheet, and its buffer would
 * take hundreds of MB. For a page that large, the buffer covers only the part
 * on screen plus a screen's worth around it, and is redrawn when the view
 * leaves it.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "util/Range.h"

namespace ambitus::render {

/// Largest buffer, in device pixels, kept for a whole page (4096 x 4096, 64 MB)
constexpr double MAX_WHOLE_PAGE_PIXELS = 4096.0 * 4096.0;
/// Side of the first window, in device pixels, before the page has been painted once
constexpr double FIRST_WINDOW_PIXELS = 2048.0;

/// Whether the page is too large to keep whole in the buffer
bool isLarge(double pageWidth, double pageHeight, double zoom, int dpiScale);

/// The part of the page to keep in the buffer when the view shows `shown` (page coordinates):
/// `shown` plus its own size on each side, inside the page. With nothing shown yet, the top left corner.
Range window(const Range& shown, double pageWidth, double pageHeight, double zoom, int dpiScale);

}  // namespace ambitus::render
