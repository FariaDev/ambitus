/*
 * Ambitus
 *
 * The palette of a toolbar preset: the presets of the house bring the palette they were made for, and a palette chosen
 * by the person (neither of the two) is never replaced.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

namespace ambitus {

/// The palette file (name only) to take when the toolbar preset `toolbarId` is chosen, "" to keep the one in use
inline std::string paletteFileForToolbar(const std::string& toolbarId, const std::string& currentPaletteFile) {
    const bool ambitus = currentPaletteFile == "ambitus.gpl";
    const bool xournal = currentPaletteFile == "xournal.gpl";
    if (toolbarId == "Xournal++" && ambitus) {
        return "xournal.gpl";
    }
    if (toolbarId == "Ambitus" && xournal) {
        return "ambitus.gpl";
    }
    return "";
}

}  // namespace ambitus
