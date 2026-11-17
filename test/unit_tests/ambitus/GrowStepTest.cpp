/*
 * Ambitus
 *
 * This file is part of the Ambitus UnitTests
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/Control.h"

// One grow of a page is 5 cm, the amount the "+5 cm" pill and the menu entries say (PageGrowHandle.cpp,
// mainmenubar.xml). 5 cm = 50 mm = 5 / 2.54 inch = 141.7322835 pt at 72 pt per inch.
TEST(GrowPage, oneStepIsFiveCentimetres) { EXPECT_NEAR(Control::GROW_STEP, 141.7322835, 1e-6); }
