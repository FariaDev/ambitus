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
 * The favourite pens (up to four) come first: each keeps a tool (pen or marker), a colour and a thickness, is saved
 * from the options of the pen and of the marker, and is taken off with the right button. The keys 1-4 call them.
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

/// The most favourites the palette holds
constexpr size_t PALETTE_MAX_FAVORITES = 4;

/// A pen or a marker with a colour and a thickness
struct PaletteFavorite {
    std::string tool;    ///< "pencil" or "highlighter"
    uint32_t color = 0;  ///< 0xRRGGBB
    int size = 0;        ///< The index of a ToolSize: 0 (very fine) to 4 (very thick)

    bool operator==(const PaletteFavorite& o) const { return tool == o.tool && color == o.color && size == o.size; }
};

/// The saved favourites: "tool:RRGGBB:size" separated by semicolons. Anything invalid (unknown tool, bad colour, size
/// out of range, wrong number of fields) and any repeated favourite is dropped; at most PALETTE_MAX_FAVORITES are kept.
/// There is no default: nothing left (or an empty text) means no favourites
std::vector<PaletteFavorite> parsePaletteFavorites(const std::string& text);
std::string serializePaletteFavorites(const std::vector<PaletteFavorite>& favorites);

/// Whether a colour is light enough for the icon on it to be dark (else it is white)
bool paletteColorIsLight(uint32_t rgb);

/// Studio mode only: with at least one favourite saved, calls the favourite at `index` (0 to 3) and returns true, even
/// when nothing sits at that position (the key then does nothing). Without favourites, or out of the Studio mode,
/// returns false and does nothing: the caller goes on with what the key did before
bool callPaletteFavorite(Control* control, size_t index);

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
