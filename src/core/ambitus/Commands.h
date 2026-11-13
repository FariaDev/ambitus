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

/// The commands that have every word of the query (in the name or the way to it), best first: the name starting
/// with the word, then a word of the name starting with it, then anywhere. An empty query gives them all in order.
std::vector<size_t> matchCommands(const std::vector<Command>& commands, const std::string& query, size_t limit);

}  // namespace ambitus
