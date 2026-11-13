/*
 * Ambitus
 *
 * Unit tests for the clean up of what the model read in a selection.
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "ambitus/ConvertInk.h"

using ambitus::tidyReading;

TEST(ConvertInk, theReadingKeepsItsLinesWithoutTheEdges) {
    EXPECT_EQ(tidyReading("  \n Teorema do confronto\n$g \\le f \\le h$\n\n"), "Teorema do confronto\n$g \\le f \\le h$");
    EXPECT_EQ(tidyReading(" \n\t "), "");
}

TEST(ConvertInk, aFenceAroundTheAnswerGoesAway) {
    EXPECT_EQ(tidyReading("```\n$\\int_0^1 x\\,dx$\n```"), "$\\int_0^1 x\\,dx$");
    EXPECT_EQ(tidyReading("```latex\n\\frac{1}{2}\n```\n"), "\\frac{1}{2}");
    EXPECT_EQ(tidyReading("```só uma linha```"), "```só uma linha```");  // not a fenced block
}
