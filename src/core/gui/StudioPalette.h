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
 * The same panel has a second tab, "Radial menu": the eight positions of the ring that opens on the right button. Each
 * takes a tool or an action; the choice lives in the settings of Ambitus too and the ring reads it every time it opens.
 *
 * In the Focus mode the same palette is not on screen: it comes down when the pointer reaches the top of the page,
 * stays while the pointer is there or on it, and goes away 1.5 s after the pointer leaves (FocusReveal.h has the
 * times).
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

#include "control/ToolEnums.h"

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
/// The name of a colour of the Summa (translated), else its hex: what the tip of a colour shows
std::string paletteColorName(uint32_t rgb);

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

/// The positions of the radial menu
constexpr size_t RADIAL_SLOTS = 8;

enum class RadialKind {
    TOOL,    ///< Selects a tool
    SHAPES,  ///< The pen with the shape recognizer on
    ACTION,  ///< Calls an action of the window
};

/// What a position of the radial menu can hold. The ids are what the settings save: never change one
struct RadialOption {
    const char* id;
    RadialKind kind;
    ToolType tool;       ///< TOOL: the tool to select
    const char* action;  ///< ACTION: the action of the window, without "win."
    const char* icon;
    const char* name;  ///< In English: translate with _()
};

/// Every option, in the order the panel shows them
const std::vector<RadialOption>& radialOptions();
/// The option with this id, or null
const RadialOption* findRadialOption(const std::string& id);

/// The eight positions the ring starts with (and goes back to): pen, marker, eraser, lasso, undo, convert to text, send
/// to Summa and hand
std::vector<std::string> defaultRadialSlots();

/// The saved positions: eight ids separated by commas. An id that is not known gives its position the default one; text
/// that is empty, or does not have exactly eight positions, gives the default for all of them. The same option may
/// appear in more than one position
std::vector<std::string> parseRadialSlots(const std::string& text);
std::string serializeRadialSlots(const std::vector<std::string>& slots);

struct PaletteState;

class StudioPalette {
public:
    /// `area` is the widget with the pages: the Focus mode watches the top of it
    StudioPalette(Control* control, GtkOverlay* overlay, GtkWidget* area);

    /// Studio mode (true): always there. Classic mode (false): hidden. Both end any reveal in course
    void setVisible(bool visible);
    /// Focus mode: hidden until the pointer reaches the top of the page (see FocusReveal.h)
    void setRevealOnHover();
    /// Opens the panel with every tool (the "..." button), when the palette is on screen
    void openPanel();

private:
    GtkWidget* root = nullptr;
    PaletteState* state = nullptr;  ///< Owned by `root`
};

}  // namespace ambitus
