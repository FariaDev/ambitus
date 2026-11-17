/*
 * Ambitus
 *
 * Sends what is on screen to the side chat of Summa, through the local bridge (library/InkReader.h). Summa puts the
 * notebook, the page and its path in the draft of the chat, and its tutor reads the page again from the file: so what
 * is on screen is saved first. What goes is chosen by SummaCrop.h: a lasso selection alone, else the part of the page
 * on screen or the whole page, as the setting "ambitusSummaSend" says.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>

#include "ambitus/SummaCrop.h"
#include "library/InkReader.h"  // for PageCrop

class Control;

namespace ambitus {

/// The setting: the part on screen ("screen") or the whole page ("page") when there is no selection
summacrop::SendMode currentSendMode(Control* ctrl);

/// UI thread. The page and the part of it to send (see summacrop::pick), read from the window and the document as
/// they are now. A lasso selection goes alone when `useSelection`.
summacrop::Pick choosePage(Control* ctrl, bool useSelection, summacrop::SendMode mode);

/// The part as the JSON of Summa has it; nothing for the whole page
std::optional<library::PageCrop> wireCrop(const std::optional<summacrop::Rect>& crop);

/// Saves the notebook when needed (asking where, if it has no file yet), then sends what is on screen (the setting
/// decides, a lasso selection goes alone)
void sendPageToSumma(Control* ctrl);

/// The second item of the menu: the other of the two (the setting's other choice), ignoring the selection
void sendOtherPartToSumma(Control* ctrl);

/// The label of that item: the whole page, or what is on screen when the setting is "page"
std::string sendOtherPartLabel(Control* ctrl);

/// Stores the setting and gives the menu item of the second choice its new label
void setSendMode(Control* ctrl, summacrop::SendMode mode);

}  // namespace ambitus
