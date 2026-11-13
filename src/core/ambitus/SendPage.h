/*
 * Ambitus
 *
 * Sends the page on screen to the side chat of Summa, through the local bridge
 * (library/InkReader.h). Summa puts the notebook, the page and its path in the
 * draft of the chat, and its tutor reads the page again from the file: so what
 * is on screen is saved first.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

class Control;

namespace ambitus {

/// Saves the notebook when needed (asking where, if it has no file yet), then sends the current page
void sendPageToSumma(Control* ctrl);

}  // namespace ambitus
