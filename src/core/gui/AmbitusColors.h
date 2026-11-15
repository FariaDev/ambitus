/*
 * Ambitus
 *
 * The few colours of the Summa that are painted by cairo (not by ui/ambitus.css): the ring of the current page in the
 * sidebar and the grey behind the pages. They are the tokens of ambitus.css, one for the light and one for the dark
 * theme.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "control/settings/Settings.h"
#include "util/Color.h"

namespace ambitus {

/// amb_accent / amb_dk_accent: the ring of the current page and its number
constexpr Color accentColor(bool dark) { return dark ? Color{0xffe6e6e6U} : Color{0xff303030U}; }
/// amb_border / amb_dk_border_strong: the 1px outline of the colour swatches of the toolbar (the dark border alone
/// is too faint for the black swatch on the dark bar)
constexpr Color borderColor(bool dark) { return dark ? Color{0xff6f6f6fU} : Color{0xffe0e0e0U}; }
/// amb_on_accent / amb_dk_on_accent: what is written over the accent
constexpr Color onAccentColor(bool dark) { return dark ? Color{0xff161616U} : Color{0xffffffffU}; }

/// The grey behind the pages: #f4f4f4 on the light theme, #262626 (amb_dk_layer) on the dark one. A colour the user
/// chose in the settings is kept; the old default (and the one of the Summa in the light theme) is replaced
inline Color stageColor(const Settings* settings, bool dark) {
    Color c = settings->getBackgroundColor();
    return dark && c == Color{0xfff4f4f4U} ? Color{0xff262626U} : c;
}

}  // namespace ambitus
