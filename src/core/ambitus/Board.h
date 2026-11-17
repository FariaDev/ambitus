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
/// Longest side of a board, a whole number of dots (about 14 m); past it scrolling stops
constexpr double MAX_SIDE = 2222 * RASTER;
/// Lowest zoom on a board, relative to 100 %
constexpr double MIN_ZOOM = 0.05;

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
/// How far the content must move right and down so the margin is free on the left and on top too;
/// whole dots, (0, 0) when there is room or no content
Size shiftToFit(const Range& content);
/// `room` cut back so that a side of `size` stays within MAX_SIDE (whole dots, never negative)
double roomWithinCap(double size, double room);

/// The part of the board on screen, in page points (it may reach past the page: the view can scroll beyond it).
/// Its width and height are the size of the view.
/// New size when the view comes near the right or bottom edge of the page: blank board stays ahead of it, one view
/// (at most a starting board, see the .cpp). Grows in whole multiples of RASTER, never past MAX_SIDE; `page` itself
/// when there is room already.
Size growAhead(Size page, const Range& view);
/// How far the content must move right and down when the view comes near the left or top edge of the page, so that
/// blank board stays ahead of it (as growAhead); whole dots, never past MAX_SIDE, (0, 0) when there is room
Size moveAhead(Size page, const Range& view);
/// Whether the content may move at all: not while something is selected or typed into (they hold its coordinates),
/// and not with a redo history (a redo would put its action back at the old coordinates)
bool mayMoveContent(bool selectionOrText, bool canRedo);

/// How far right and down the content of the page reaches (0, 0 when empty). The caller locks the document.
Size contentReach(const XojPage& page);

/// Box around everything on the page (an empty Range when there is nothing). The caller locks the document.
Range contentBounds(const XojPage& page);
/// Zoom (scale of the page) that shows `content` in a view of the given size with some air around,
/// never above maxZoom; maxZoom when content is empty
double zoomToFit(const Range& content, double viewWidth, double viewHeight, double maxZoom);

/// Longest side, in pixels, of the preview saved in a board file
constexpr int PREVIEW_SIZE = 384;
/// Part of the page the saved preview shows: the content with two dots of air, inside the page;
/// the whole page when there is no content
Range previewArea(const Range& content, double pageWidth, double pageHeight);

/// Most pixels of an image exported from a board (--create-img, export dialog)
constexpr double MAX_EXPORT_PIXELS = 4096.0 * 4096.0;
/// The export zoom, lowered so `area` fits in MAX_EXPORT_PIXELS
double exportZoom(const Range& area, double zoom);

}  // namespace ambitus::board
