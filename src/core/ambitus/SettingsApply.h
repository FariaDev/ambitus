/*
 * Ambitus
 *
 * What a change of a setting does besides being saved, shared by the settings card and the first-time setup, so the two
 * write the same thing: the theme, the language, the finger that moves the page, the button of the pen and the folder
 * of the notebooks (docs/design/r5, sections 1 and 4).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

#include <gtk/gtk.h>

#include "filesystem.h"

class Control;

namespace ambitus::settingsapply {

/// The language of the interface as the segmented control names it: 0 Portuguese, 1 English, -1 any other. An empty
/// `preferred` is the language of the system.
int languageIndex(const std::string& preferred);

/// The theme as the segmented control names it: 0 system, 1 light, 2 dark
int themeIndex(Control* ctrl);

/// The folder that Summa calls its vault, from its config; empty when Summa is not set up
std::string summaVault();
/// Whether Summa has been set up on this computer (it has a config)
bool summaInstalled();

/// The two dots of the accent (graphite, violet), as the settings draw them. The accent of Summa lives in the state of
/// that app, which Ambitus does not read, so the rows that hold them are shown off.
GtkWidget* makeAccentDots();

void applyTheme(Control* ctrl, int index);
/// Xournal++ keeps the locale and reads it when it starts: this applies the next time the program opens
void applyLanguage(Control* ctrl, int index);
/// The touchscreen, the tools of the buttons and the view follow, as the end of SettingsDialog::save
void inputChanged(Control* ctrl);
void applyFingerMovesPage(Control* ctrl, bool moves);
void applyPenRadial(Control* ctrl, bool radial);
/// The folder of the notebooks is `chosen`; the library shows it, as it shows a folder added by hand, and the one
/// chosen before goes out
void applyNotebooksFolder(Control* ctrl, const fs::path& chosen);

}  // namespace ambitus::settingsapply
