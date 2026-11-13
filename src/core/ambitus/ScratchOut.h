/*
 * Ambitus
 *
 * Scratch out to erase: a zigzag drawn with the pen over strokes erases them.
 * Only the geometry lives here, so it can be tested without a document.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>

#include "util/Point.h"

namespace ambitus::scratch {

using Pt = xoj::util::Point<double>;

/// The shape of a scribble: its main direction and the box it covers in that frame.
struct Scribble {
    Pt origin;                  ///< centroid
    Pt dir;                     ///< unit vector along the back-and-forth movement
    double minS = 0, maxS = 0;  ///< extent along dir, relative to origin
    double minT = 0, maxT = 0;  ///< extent across dir
    int reversals = 0;
};

/// A stroke counts as a scribble when it goes back and forth at least 4 times along its main
/// direction, each time over at least a third of its length. Handwriting moves forward along the
/// line, so it does not qualify. Returns false for strokes smaller than minSize (in points).
bool detectScribble(const std::vector<Pt>& points, Scribble& out, double minSize = 8.0);

/// True when the scribble crosses out the target: most of the target lies inside the box of the
/// scribble and the scribble passes over it (within tolerance, in points).
bool crossesOut(const Scribble& s, const std::vector<Pt>& scribble, const std::vector<Pt>& target, double tolerance);

}  // namespace ambitus::scratch
