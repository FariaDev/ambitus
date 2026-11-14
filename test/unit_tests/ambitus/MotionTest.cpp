#include <gtest/gtest.h>

#include "gui/AmbitusMotion.h"

using namespace ambitus::motion;

TEST(Motion, easeOutStartsAtZeroAndEndsAtOne) {
    EXPECT_NEAR(easeOut(0), 0, 1e-6);
    EXPECT_NEAR(easeOut(1), 1, 1e-6);
}

TEST(Motion, easeOutIsFastAtFirstAndNeverGoesBack) {
    EXPECT_GT(easeOut(0.25), 0.25);  // ease-out: more than half way well before half of the time
    EXPECT_GT(easeOut(0.5), 0.75);
    double last = 0;
    for (int i = 1; i <= 20; i++) {
        double v = easeOut(i / 20.0);
        EXPECT_GE(v, last);
        last = v;
    }
}

TEST(Motion, tokensAreTheOnesOfTheSumma) {
    EXPECT_EQ(FAST_US, 120000);
    EXPECT_EQ(BASE_US, 180000);
    EXPECT_EQ(SLOW_US, 320000);
    EXPECT_DOUBLE_EQ(ZOOM_IN, 0.985);
}
