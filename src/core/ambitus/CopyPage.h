/*
 * Ambitus
 *
 * Copies the page on screen as an image (PNG on the clipboard), to paste in a
 * chat, a message or another program. On a board, only the written part.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

class Control;

namespace ambitus {

void copyPageAsImage(Control* ctrl);

}  // namespace ambitus
