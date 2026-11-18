/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "NotebookPaper.h"

#include "ambitus/SettingsRows.h"

namespace ambitus::notebookpaper {

auto paperFromBackground(const PageType& type) -> std::optional<newnotebook::Paper> {
    if (type.isPdfPage() || type.isImagePage()) {
        return std::nullopt;
    }
    switch (type.format) {
        case PageTypeFormat::Lined:
            if (type.config == newnotebook::SUMMA_CONFIG) {
                return newnotebook::Paper::SUMMA;
            }
            if (type.config.empty()) {
                return newnotebook::Paper::BLUE_RULED;
            }
            return std::nullopt;
        case PageTypeFormat::Graph:
            return newnotebook::Paper::GRID;
        case PageTypeFormat::Dotted:
            return newnotebook::Paper::DOTTED;
        case PageTypeFormat::Plain:
            return newnotebook::Paper::PLAIN;
        default:
            return std::nullopt;
    }
}

auto defaultPaper(const std::optional<std::string>& storedPaper, const std::vector<PageType>& pages,
                  const std::string& settingPaper) -> newnotebook::Paper {
    if (storedPaper) {
        if (auto p = settingsrows::paperFromId(*storedPaper)) {
            return *p;
        }
    }
    for (auto const& type: pages) {
        if (auto p = paperFromBackground(type)) {
            return *p;
        }
    }
    if (auto p = settingsrows::paperFromId(settingPaper)) {
        return *p;
    }
    return newnotebook::DEFAULT_PAPER;
}

auto paperForNewPage(const std::optional<std::string>& storedPaper, const PageType& current,
                     const std::vector<PageType>& pages, const std::string& settingPaper)
        -> std::optional<newnotebook::Paper> {
    if (storedPaper) {
        if (auto p = settingsrows::paperFromId(*storedPaper)) {
            return p;
        }
    }
    if (!current.isPdfPage() && !current.isImagePage()) {
        return std::nullopt;
    }
    return defaultPaper(std::nullopt, pages, settingPaper);
}

}  // namespace ambitus::notebookpaper
