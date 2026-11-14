/*
 * Ambitus
 *
 * The LaTeX card (docs/design/r6, section 8) shows an error in one line under the field, not the whole output of the
 * compiler. This takes that line out of the output: the first message of the compiler and the source it points at,
 * e.g. "Undefined control sequence. — \foo". Plain text in, plain text out, so it is tested apart from the widgets.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

namespace ambitus::latex {

/// The one line for the user out of the output of pdflatex (or xelatex, lualatex, tectonic...):
///  - "! Undefined control sequence." followed by "l.5 \foo": gives "Undefined control sequence. — \foo"
///  - "./tex.tex:5: Undefined control sequence." (-file-line-error): the message after the line number
///  - no "!" line: the first line that mentions an error, else the first line with text
/// Empty when there is no text at all. A line longer than `maxLength` (in bytes, cut on a character) ends in "…".
std::string firstErrorLine(const std::string& compilerOutput, size_t maxLength = 160);

}  // namespace ambitus::latex
