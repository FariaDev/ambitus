/*
 * Ambitus
 *
 * The logic of the "Export as PDF" card (docs/design/r6, section 8), apart from the widgets: which pages the choice
 * means (all, this one, or a range typed as "1-3, 5", read by the parser of Xournal++), the ranges the PDF export is
 * given, what "with the paper" is for the export, and how the file name is checked.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "control/jobs/ExportParameters.h"
#include "util/ElementRange.h"

#include "filesystem.h"

namespace ambitus::exportpdf {

/// The first choice of the card
enum class Pages { ALL, THIS, RANGE };

/// Why a range cannot be used
enum class RangeProblem {
    NONE,
    EMPTY,    ///< nothing typed yet: the card waits, it does not scold
    INVALID,  ///< not pages ("1-3, 5"), or beyond the end of the notebook, or backwards
};

struct Selection {
    std::vector<bool> chosen;  ///< one per page of the notebook: is it in the PDF
    RangeProblem problem = RangeProblem::NONE;

    size_t count() const;
    bool usable() const { return problem == RangeProblem::NONE && count() > 0; }
};

/// The pages the choice means. `current` is the index (from 0) of the page being shown; `rangeText` is read only for
/// Pages::RANGE, with the parser of the export dialog of Xournal++ (1-3, 5; 2-; -4), page numbers from 1.
Selection select(Pages which, const std::string& rangeText, size_t current, size_t pageCount);

/// The pages as the PDF export takes them: ascending, each page once, neighbours joined ({0,1,2,4} -> 0-2, 4-4)
PageRangeVector toRanges(const std::vector<bool>& chosen);

/// "With the paper" is every background; without it, the ruling and the margin go and the PDF or image under the
/// ink stays, which is the "No ruling" of the export dialog of Xournal++
ExportBackgroundType backgroundFor(bool withPaper);

/// The parameters of a PDF export of the card: the default backend, not progressive
ExportParameters parametersFor(const Selection& selection, bool withPaper);

/// Why a file name cannot be used
enum class NameCheck { OK, EMPTY, INVALID };

/// What was typed, as the file name goes: spaces trimmed from both ends and a ".pdf" at the end dropped (any case)
std::string cleanName(const std::string& typed);

/// INVALID: a path separator, a control character, or a leading dot
NameCheck checkName(const std::string& typed);

/// The name of the file: the cleaned name and ".pdf"
std::string fileNameOf(const std::string& typed);

/// Where the place of the PDF came from: a GtkFileChooser asks by itself before writing over a file that is there
/// (do-overwrite-confirmation), so the card must not ask a second time
enum class PathChoice { TYPED, FROM_FILE_CHOOSER };

/// Whether the export must ask before writing over the file: it is there and the path was typed in the card
bool asksBeforeOverwrite(const fs::path& target, PathChoice choice);

}  // namespace ambitus::exportpdf
