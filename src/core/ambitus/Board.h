/*
 * Ambitus
 *
 * Board ("quadro"): a notebook with one single page that grows as you write,
 * so the .xopp still opens in Xournal++. The page is marked as a board in its
 * background config ("ambitus=board"): Xournal++ ignores the unknown key and
 * keeps it when saving, and the dotted background draws as usual.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

#include "model/PageType.h"
#include "util/Range.h"

class Document;
class XojPage;
class PageTemplateSettings;

namespace ambitus::board {

/// Spacing of the dots, in points; the page grows by whole multiples of it so the grid never jumps
constexpr double RASTER = 18.0;
/// Size of a new board (about 56 x 35 cm), a whole number of dots
constexpr double START_WIDTH = 89 * RASTER;
constexpr double START_HEIGHT = 56 * RASTER;
/// Room kept free between the writing and the right and bottom edges of the page
constexpr double MARGIN = 28 * RASTER;

constexpr char KEY[] = "ambitus";
constexpr char VALUE[] = "board";

/// The background of a board: dotted, with the mark
PageType pageType();
bool isBoardPageType(const PageType& t);
/// A document is a board when it has one page and that page carries the mark. The caller locks the document.
bool isBoard(const Document& doc);
/// Template for "New board", from the user's template (keeps the paper colour)
PageTemplateSettings pageTemplate(const PageTemplateSettings& base);

/// New size so that content reaching (right, bottom) keeps MARGIN free; equal to the old size when it already does.
/// Grows in whole multiples of RASTER.
struct Size {
    double width, height;
};
Size grownToFit(Size page, double right, double bottom);

/// How far right and down the content of the page reaches (0, 0 when empty). The caller locks the document.
Size contentReach(const XojPage& page);

/// Box around everything on the page (an empty Range when there is nothing). The caller locks the document.
Range contentBounds(const XojPage& page);
/// Zoom (scale of the page) that shows `content` in a view of the given size with some air around,
/// never above maxZoom; maxZoom when content is empty
double zoomToFit(const Range& content, double viewWidth, double viewHeight, double maxZoom);

}  // namespace ambitus::board
