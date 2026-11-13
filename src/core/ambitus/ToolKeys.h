/*
 * Ambitus
 *
 * One key per tool, as in the best note apps: P pen, M marker (highlighter),
 * E eraser, S lasso, T text, X LaTeX. They act only on the page (not while a
 * text is being written, nor in a field), and without modifiers; h j k l stay
 * with the scrolling and the digits with the colours, as in Xournal++.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>

#include "control/ToolEnums.h"

namespace ambitus {

/// The tool of a key (a GDK keyval, small or capital letter), if it has one
std::optional<ToolType> toolForKey(unsigned int keyval);
/// The tooltip of a tool with its key, like "Pen (P)"
std::string withToolKey(const char* tip, ToolType type);

}  // namespace ambitus
