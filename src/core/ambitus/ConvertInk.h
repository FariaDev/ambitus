/*
 * Ambitus
 *
 * Turns the handwriting selected with the lasso into text: the selection goes
 * as a PNG to Summa's bridge (/ler-tinta, the vision model of the person) and
 * comes back as text, formulas in LaTeX between $. The strokes give place to a
 * text box at the same spot; both steps can be undone.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

class Control;

namespace ambitus {

/// Converts the strokes of the current selection (blocking work runs in a thread; the answer comes on the UI thread)
void convertSelectionToText(Control* ctrl);

/// Removes the ``` fences and blank edges a model may wrap its answer in
std::string tidyReading(std::string text);

}  // namespace ambitus
