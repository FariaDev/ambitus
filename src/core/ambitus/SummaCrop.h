/*
 * Ambitus
 *
 * What goes to Summa when the page is sent or checked: the whole page, or only the part of it the person is looking
 * at (the part on screen, or the bounds of a lasso selection with a margin). This file is the geometry alone, without
 * the window or the document, so it can be tested; SendPage.h reads the window and SummaPoll.h draws the part.
 *
 * Coordinates are page points (the page's own coordinates, the origin at the top left), as the view gives them.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ambitus::summacrop {

/// A rectangle in page points
struct Rect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

/// What the setting "ambitusSummaSend" chooses when there is no lasso selection
enum class SendMode {
    Screen,  ///< the part of the page on screen (the whole page when that is most of it)
    Page,    ///< the whole page
};

/// The setting's id, "screen" or "page"; an id that is not known is the screen
SendMode sendModeFromId(const std::string& id);
const char* sendModeId(SendMode mode);

/// The margin around a lasso selection, in points
constexpr double SELECTION_MARGIN = 12.0;
/// A visible part of at least this share of the page is the whole page
constexpr double WHOLE_PAGE_SHARE = 0.98;
/// The longest side of the picture of a part, in pixels: a board zoomed out is drawn at most this big
constexpr int PART_MAX_SIDE = 2400;

/// A page that has part of it on screen
struct OnScreen {
    size_t page = 0;
    double width = 0;
    double height = 0;
    Rect visible;  ///< the part of the page in the window, in page points
};

/// The bounds of a lasso selection on a page
struct SelectionOnPage {
    size_t page = 0;
    double width = 0;
    double height = 0;
    Rect bounds;  ///< the bounding box in page points, the rotation of the selection included
};

/// What to send: a page, and the part of it (nothing: the whole page)
struct Pick {
    size_t page = 0;
    std::optional<Rect> crop;
    bool fromSelection = false;
};

/// The bounding box of a rectangle turned by `rotation` (radians) around its centre
Rect rotatedBounds(const Rect& r, double rotation);

/// Rounds a rectangle outward to whole points and clamps it to the page (0..width, 0..height, the edges rounded
/// down). Nothing when nothing of it stays on the page.
std::optional<Rect> fitToPage(const Rect& r, double pageWidth, double pageHeight);

/// The part of the page a lasso selection takes: its bounds with the margin, clamped to the page, whatever the share of
/// the page that is. Nothing when none of it is on the page.
std::optional<Rect> selectionCrop(const SelectionOnPage& selection, double margin = SELECTION_MARGIN);

/// The part of the page on screen. Nothing (the whole page) when it covers at least WHOLE_PAGE_SHARE of the page, or
/// when nothing of it is on screen.
std::optional<Rect> screenCrop(const OnScreen& page);

/// The index of the page with the largest visible area (the first one on a tie); nothing when the list is empty
std::optional<size_t> largestVisible(const std::vector<OnScreen>& pages);

/// What to send. A lasso selection goes alone, whatever the mode (when `useSelection`). Else the mode decides: the
/// page with the largest visible area and its part on screen, or the current page whole. Without a page on screen,
/// the current page whole.
Pick pick(size_t currentPage, const std::vector<OnScreen>& onScreen, const std::optional<SelectionOnPage>& selection,
          SendMode mode, bool useSelection);

}  // namespace ambitus::summacrop
