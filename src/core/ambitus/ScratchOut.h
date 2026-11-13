/*
 * Ambitus
 *
 * Scratch out to erase: a quick zigzag drawn with the pen over strokes erases them.
 * Only the geometry lives here, so it can be tested without a document.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <vector>

#include "util/Point.h"

namespace ambitus::scratch {

using Pt = xoj::util::Point<double>;

/// A scribble longer than this (in seconds) is ink: slow hatching must not erase
constexpr double MAX_DURATION = 3.0;

/// The shape of a scribble: its main direction, the box it covers in that frame and its passes.
struct Scribble {
    Pt origin;                  ///< centroid
    Pt dir;                     ///< unit vector along the back-and-forth movement
    double minS = 0, maxS = 0;  ///< extent along dir, relative to origin
    double minT = 0, maxT = 0;  ///< extent across dir
    /// pass k goes from point turns[k] to point turns[k + 1]
    std::vector<size_t> turns;

    size_t passes() const { return turns.empty() ? 0 : turns.size() - 1; }
};

/// A stroke counts as a scribble when it goes back and forth at least 5 times along its main
/// direction (each pass longer than 40% of the scribble), stays flat (across it, at most 65% of its
/// length) and is at least minSize points long. Handwriting moves forward along the line, so it
/// does not qualify.
bool detectScribble(const std::vector<Pt>& points, Scribble& out, double minSize = 12.0);

/// True when the scribble crosses out the target: most of the target lies inside the box of the
/// scribble, and at least two passes that are not next to each other go over it (within
/// tolerance, in points). Touching it with one end is not enough.
bool crossesOut(const Scribble& s, const std::vector<Pt>& scribble, const std::vector<Pt>& target, double tolerance);

}  // namespace ambitus::scratch
