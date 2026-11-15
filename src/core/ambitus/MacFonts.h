/*
 * Ambitus
 *
 * Registers a font file with Core Text on macOS, where Pango draws its text.
 * Kept apart from the rest of the code: the Core Text headers declare a global
 * `Point` that clashes with the one in model/Point.h.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

namespace ambitus::macfonts {

/// Registers the font in `file` for this process only. Returns false if it could not (always false off macOS).
bool registerFont(const std::string& file);

}  // namespace ambitus::macfonts
