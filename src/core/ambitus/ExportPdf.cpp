#include "ExportPdf.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <system_error>

#include "pdf/base/PdfExportBackend.h"

namespace ambitus::exportpdf {

size_t Selection::count() const { return static_cast<size_t>(std::count(chosen.begin(), chosen.end(), true)); }

Selection select(Pages which, const std::string& rangeText, size_t current, size_t pageCount) {
    Selection out;
    out.chosen.assign(pageCount, false);
    if (pageCount == 0) {
        out.problem = RangeProblem::EMPTY;
        return out;
    }
    switch (which) {
        case Pages::ALL:
            out.chosen.assign(pageCount, true);
            break;
        case Pages::THIS:
            out.chosen[std::min(current, pageCount - 1)] = true;
            break;
        case Pages::RANGE: {
            const bool blank = std::all_of(rangeText.begin(), rangeText.end(),
                                           [](unsigned char c) { return c == ' ' || c == '\t' || c == ','; });
            if (blank) {
                out.problem = RangeProblem::EMPTY;
                break;
            }
            try {
                for (const ElementRangeEntry& e: ElementRange::parse(rangeText, pageCount)) {
                    for (size_t i = e.first; i <= e.last && i < pageCount; i++) {
                        out.chosen[i] = true;
                    }
                }
            } catch (const std::invalid_argument&) {
                out.chosen.assign(pageCount, false);
                out.problem = RangeProblem::INVALID;
            }
            break;
        }
    }
    return out;
}

PageRangeVector toRanges(const std::vector<bool>& chosen) {
    PageRangeVector out;
    for (size_t i = 0; i < chosen.size(); i++) {
        if (!chosen[i]) {
            continue;
        }
        if (!out.empty() && out.back().last + 1 == i) {
            out.back().last = i;
        } else {
            out.emplace_back(i, i);
        }
    }
    return out;
}

ExportBackgroundType backgroundFor(bool withPaper) {
    return withPaper ? EXPORT_BACKGROUND_ALL : EXPORT_BACKGROUND_UNRULED;
}

ExportParameters parametersFor(const Selection& selection, bool withPaper) {
    ExportParameters p{};
    p.progressiveMode = false;
    p.backgroundType = backgroundFor(withPaper);
    p.pdfExportBackend = PdfExportBackend::fromString(PdfExportBackend::DEFAULT_ID_STRING);
    p.pageRanges = toRanges(selection.chosen);
    p.format = EXPORT_GRAPHICS_PDF;
    return p;
}

std::string cleanName(const std::string& typed) {
    const char* blanks = " \t\r\n";
    const size_t first = typed.find_first_not_of(blanks);
    if (first == std::string::npos) {
        return {};
    }
    std::string name = typed.substr(first, typed.find_last_not_of(blanks) - first + 1);
    if (name.size() >= 4) {
        std::string tail = name.substr(name.size() - 4);
        std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return std::tolower(c); });
        if (tail == ".pdf") {
            name.resize(name.size() - 4);
            const size_t last = name.find_last_not_of(blanks);
            name.resize(last == std::string::npos ? 0 : last + 1);
        }
    }
    return name;
}

NameCheck checkName(const std::string& typed) {
    const std::string name = cleanName(typed);
    if (name.empty()) {
        return NameCheck::EMPTY;
    }
    if (name.front() == '.') {
        return NameCheck::INVALID;
    }
    for (unsigned char c: name) {
        if (c == '/' || c == '\\' || c < 0x20 || c == 0x7f) {
            return NameCheck::INVALID;
        }
    }
    return NameCheck::OK;
}

std::string fileNameOf(const std::string& typed) { return cleanName(typed) + ".pdf"; }

bool asksBeforeOverwrite(const fs::path& target, PathChoice choice) {
    std::error_code ec;
    return choice == PathChoice::TYPED && fs::exists(target, ec);
}

}  // namespace ambitus::exportpdf
