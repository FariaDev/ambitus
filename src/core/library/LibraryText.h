/*
 * Ambitus
 *
 * The small texts of the library that are worth a test of their own: the line of the "Continue" card and the path
 * shown in the foot.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <ctime>
#include <string>

namespace ambitus::library {

/// "today, 14:30", "yesterday, 22:10", "12 Nov, 22:10"; with the year for another year ("12 Nov 2025, 22:10").
/// `now` is a parameter so that a test can say what day it is.
std::string whenText(std::time_t when, std::time_t now);

/// "Calculus I · page 4 of 6 · yesterday, 22:10". The place is left out when empty, and so is the page when
/// `total` is 0 (not counted yet). `page` is 1-based.
std::string resumeLine(const std::string& place, size_t page, size_t total, std::time_t when, std::time_t now);

/// The path as the foot of the library shows it: the home folder as "~". `home` may be empty.
std::string displayPath(const std::string& path, std::string home);

}  // namespace ambitus::library
