/*
 * Ambitus
 *
 * The rule that picks the default paper of a notebook: a pure function, with no files and no settings access, so the
 * library store and the settings can be handed to it by the caller and it can be tested on its own.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ambitus/NewNotebook.h"
#include "model/PageType.h"

namespace ambitus::notebookpaper {

/// Which of the five notebook papers a page background is; nothing for a PDF or image page, or a paper we do not know.
std::optional<newnotebook::Paper> paperFromBackground(const PageType& type);

/// The default paper: the stored choice first, else the first page that is not PDF/image and matches a known paper,
/// else the paper remembered by the New notebook dialog, else Summa.
newnotebook::Paper defaultPaper(const std::optional<std::string>& storedPaper, const std::vector<PageType>& pages,
                                const std::string& settingPaper);

/// The paper a plain new page gets after a page with the background `current`: the stored choice; else nothing, which
/// means the same background as `current` (also the kinds of paper we do not know); after a PDF or image page, which is
/// never repeated by itself, the default paper.
std::optional<newnotebook::Paper> paperForNewPage(const std::optional<std::string>& storedPaper,
                                                  const PageType& current, const std::vector<PageType>& pages,
                                                  const std::string& settingPaper);

}  // namespace ambitus::notebookpaper
