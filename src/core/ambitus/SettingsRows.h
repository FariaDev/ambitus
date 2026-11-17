/*
 * Ambitus
 *
 * The logic of the settings card (docs/design/r5, section 4), apart from the widgets: which rows a search shows, the
 * path as the person knows it, the folder that Summa calls its vault, and how the paper of a new notebook is saved.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>

#include "ambitus/NewNotebook.h"

namespace ambitus::settingsrows {

/// Whether a row answers the search: every word of the query is in its title, its description or the name of its
/// section, without minding accents or capitals. An empty query (or only spaces) matches every row.
bool matches(const std::string& query, const std::string& title, const std::string& description,
             const std::string& section);

/// The path with "~" for the home folder ("/home/me/Studies" gives "~/Studies"); any other path as it is
std::string shownPath(const std::string& path, const std::string& home);

/// "vaultPath" of the config.json of Summa, the folder of its notebooks; empty when there is none or the JSON is bad
std::string vaultPathOf(const std::string& summaConfigJson);

/// What the settings save for the paper of a new notebook ("summa", "ruled", "grid", "dotted", "plain")
const char* paperId(newnotebook::Paper paper);
/// The paper of a saved id; an id that is not known gives nothing
std::optional<newnotebook::Paper> paperFromId(const std::string& id);

/// What the settings save for the page size of a new notebook ("a4", "letter"; the infinite board is not a size to
/// keep)
const char* sizeId(newnotebook::PageSize size);
/// The size of a saved id; an id that is not known gives nothing
std::optional<newnotebook::PageSize> sizeFromId(const std::string& id);

/// What the settings save for the size of the last draft: as sizeId, and "infinite" for the board, which a draft can be
const char* draftSizeId(newnotebook::PageSize size);
/// The size of a saved draft size; an id that is not known gives nothing
std::optional<newnotebook::PageSize> draftSizeFromId(const std::string& id);

}  // namespace ambitus::settingsrows
