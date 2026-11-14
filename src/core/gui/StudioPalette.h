/*
 * Ambitus
 *
 * The floating palette of the Studio mode, above the page: pen, highlighter,
 * eraser, selection, text and LaTeX, and five colours. Every button is bound to
 * an action of Xournal++ (select-tool, tool-color), so GTK marks the active one
 * by itself. Touching the active tool again opens its options in a popover,
 * wired to the settings and actions Xournal++ already has.
 *
 * The tools and the colours are the user's: a "..." button (or the right button anywhere on the palette) opens the
 * panel with every tool, each with a pin to put it on the palette or take it off; a "+" asks for one more colour and
 * the right button on a colour removes it. Both lists live only in the settings of Ambitus (never in the .xopp).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtk/gtk.h>

class Control;

namespace ambitus {

/// The most colours the palette holds
constexpr size_t PALETTE_MAX_COLORS = 10;

/// The tools the palette starts with (and goes back to): pen, marker, eraser, lasso, text and LaTeX
std::vector<std::string> defaultPaletteTools();
/// The five colours the palette starts with (and goes back to), as 0xRRGGBB
std::vector<uint32_t> defaultPaletteColors();

/// The saved list of tools: ids separated by commas. Unknown and repeated ids are dropped; if nothing is left (or the
/// text is empty), the default tools
std::vector<std::string> parsePaletteTools(const std::string& text);
std::string serializePaletteTools(const std::vector<std::string>& tools);

/// The saved list of colours: RRGGBB (a leading # is accepted) separated by commas. Invalid and repeated ones are
/// dropped, at most PALETTE_MAX_COLORS are kept; if nothing is left (or the text is empty), the default colours
std::vector<uint32_t> parsePaletteColors(const std::string& text);
std::string serializePaletteColors(const std::vector<uint32_t>& colors);

/// The colour as "#RRGGBB" in capitals: the tip of a colour that has no name
std::string paletteColorHex(uint32_t rgb);

struct PaletteState;

class StudioPalette {
public:
    StudioPalette(Control* control, GtkOverlay* overlay);

    void setVisible(bool visible);

private:
    GtkWidget* root = nullptr;
    PaletteState* state = nullptr;  ///< Owned by `root`
};

}  // namespace ambitus
