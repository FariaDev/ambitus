/*
 * Ambitus
 *
 * Draws notebook covers with Cairo: the same four styles as the prototype in
 * docs/design/r1 (contour, linen, page, plain), plus an own image.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>
#include <string>

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "LibraryStore.h"  // for Cover

namespace ambitus::library {

/// Fallback cover colour when neither the notebook nor its course has one.
constexpr auto DEFAULT_COVER_COLOR = "#4a4a4a";

/// Deterministic random numbers seeded by a string, so a cover looks the same every time.
class SeededRandom {
public:
    explicit SeededRandom(const std::string& seed);
    /// Uniform in [0, 1)
    double next();

private:
    uint32_t h;
};

struct CoverInput {
    Cover cover;
    std::string title;
    /// false for the small covers of the style picker (the title still seeds the contour)
    bool showTitle = true;
    /// colour of the course; used when cover.color is empty
    std::string courseColor;
    /// first page preview (Page style); may be null
    GdkPixbuf* preview = nullptr;
    /// own image (cover.image already loaded); may be null
    GdkPixbuf* image = nullptr;
};

/**
 * Draws the cover into the rectangle (0, 0, width, height) of cr. The title uses STIX Two Text through Pango.
 * The caller clips nothing: the cover draws its own rounded shape.
 */
void drawCover(cairo_t* cr, double width, double height, const CoverInput& in);

/**
 * Draws the card of a board into (0, 0, width, height): the course colour with a dot grid, the written part of the
 * preview in light ink, the kicker (e.g. "BOARD") at the top and the title at the bottom.
 */
void drawBoardCover(cairo_t* cr, double width, double height, const CoverInput& in, const std::string& kicker);

/// The colour actually used: the notebook's, else the course's, else DEFAULT_COVER_COLOR.
std::string effectiveColor(const CoverInput& in);

}  // namespace ambitus::library
