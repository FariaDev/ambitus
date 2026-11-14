#include <gtest/gtest.h>

#include "gui/Toast.h"

using namespace ambitus::toast;

TEST(Toast, theCountShowsWholeSecondsAndNeverGoesBelowZero) {
    EXPECT_EQ(secondsLeft(ACTION_MS), 25);
    EXPECT_EQ(secondsLeft(24999), 24);
    EXPECT_EQ(secondsLeft(1000), 1);
    EXPECT_EQ(secondsLeft(999), 0);
    EXPECT_EQ(secondsLeft(0), 0);
    EXPECT_EQ(secondsLeft(-300), 0);
}

TEST(Toast, theTimesAreTheOnesOfTheSumma) {
    EXPECT_EQ(STRIP_MS, 4000);
    EXPECT_EQ(ACTION_MS, 25000);
    EXPECT_EQ(TICK_MS, 500);
    EXPECT_EQ(FADE_IN_MS, 180);
    EXPECT_EQ(FADE_OUT_MS, 120);
}
