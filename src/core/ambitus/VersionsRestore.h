/*
 * Ambitus
 *
 * Putting a copy kept before a save (see Versions.h) back in place of the notebook. What is on disk now is kept as a
 * version first, so the notice that follows can offer "Undo".
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "Versions.h"
#include "filesystem.h"

class Control;

namespace ambitus {

/// The notebook goes back to the version, reloaded in its tab; with unsaved changes it is saved first (and nothing
/// happens if that does not go through). Ends with a notice, with "Undo" when there was something to keep aside, and
/// MainWindow::versionsRestored(). Failures show an error card and leave the notebook as it was.
void restoreVersion(Control* ctrl, const fs::path& notebook, const versions::Version& v);

}  // namespace ambitus
