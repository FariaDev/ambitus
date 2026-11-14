/*
 * Ambitus
 *
 * The commands of the command palette: every item of the main menu, with the
 * way to it ("Editar › Organizar seleção") and its shortcut, found by any part
 * of the name, without minding accents or capitals.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <gio/gio.h>

#include "util/raii/GVariantSPtr.h"

namespace ambitus {

struct Command {
    std::string label;               ///< as in the menu, without the mnemonic
    std::string path;                ///< the submenus to it, joined by " › "
    std::string action;              ///< detailed name, with the "win." or "app." prefix
    xoj::util::GVariantSPtr target;  ///< may be null
    std::string accel;               ///< as in the menu file, like "<Ctrl>k"; may be empty
};

/// Every item of the menu that runs an action, in the order of the menu
std::vector<Command> collectCommands(GMenuModel* menu);

/// Lowercase ASCII, without accents: what the search compares
std::string foldForSearch(const std::string& text);

/// What the search compares: a name and where it is (the way to a command in the menu, the course of a notebook)
struct Candidate {
    std::string label;
    std::string extra;
};

/// The candidates that have every word of the query (in the name or in the place), best first: the name starting
/// with the word, then a word of the name starting with it, then anywhere. An empty query gives them all in order;
/// the same score keeps the order given.
std::vector<size_t> rankCandidates(const std::vector<Candidate>& candidates, const std::string& query, size_t limit);

/// The commands that match, as rankCandidates does with the name and the way to it
std::vector<size_t> matchCommands(const std::vector<Command>& commands, const std::string& query, size_t limit);

/// Where the words of the query are in the text, as byte ranges of the text itself, in order and not overlapping:
/// what the box marks in a name. Accents and capitals do not matter ("calc" marks "Cálc").
std::vector<std::pair<size_t, size_t>> highlightRanges(const std::string& text, const std::string& query);

/// The parts of the command box, in the order they are shown
enum class PaletteSection { Recent, Commands, Notebooks };

/// A row of the box: the index is into the commands (Commands) or into the notebooks (Recent, Notebooks)
struct PaletteRow {
    PaletteSection section;
    size_t index;
    bool operator==(const PaletteRow&) const = default;
};

struct PaletteLimits {
    size_t recent = 5;
    size_t commands = 7;
    size_t notebooks = 5;
};

/// The rows to show. With nothing typed: the recent notebooks (the indices of `recent` into `notebooks`, newest
/// first), or the first commands of the menu when there is no recent notebook. With a text: the commands that match,
/// then the notebooks that match.
std::vector<PaletteRow> arrangePalette(const std::vector<Candidate>& commands, const std::vector<Candidate>& notebooks,
                                       const std::vector<size_t>& recent, const std::string& query,
                                       const PaletteLimits& limits = {});

}  // namespace ambitus
