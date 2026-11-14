/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/FocusReveal.h"

using ambitus::FocusReveal;
using ambitus::motion::BASE_US;
using ambitus::motion::FAST_US;

namespace {
constexpr gint64 MS = 1000;

/// Runs the machine from `from` to `to` (exclusive) in steps of 10 ms with the same inputs
double run(FocusReveal& r, gint64& now, gint64 ms, bool hot, bool busy = false, bool animate = true) {
    double level = r.level();
    for (gint64 t = 0; t < ms; t += 10) {
        now += 10 * MS;
        level = r.update(now, hot, busy, animate);
    }
    return level;
}
}  // namespace

TEST(FocusReveal, startsHiddenAndStaysSoWhileThePointerIsAway) {
    FocusReveal r;
    gint64 now = 1000 * MS;
    EXPECT_EQ(run(r, now, 3000, false), 0);
    EXPECT_TRUE(r.hidden());
}

TEST(FocusReveal, comesDownInBaseTimeWhenThePointerEntersTheStrip) {
    FocusReveal r;
    gint64 now = 0;
    r.update(now, true, false, true);
    EXPECT_TRUE(r.animating());
    EXPECT_NEAR(run(r, now, 90, true), 0.5, 1e-9);
    EXPECT_GT(run(r, now, 80, true), 0.9);
    EXPECT_EQ(run(r, now, 20, true), 1);
    EXPECT_FALSE(r.animating());
    EXPECT_FALSE(r.hidden());
    EXPECT_EQ(BASE_US, 180 * MS);
}

TEST(FocusReveal, staysWhilePointerIsThereAndLeavesOneAndAHalfSecondsAfter) {
    FocusReveal r;
    gint64 now = 0;
    run(r, now, 500, true);
    EXPECT_EQ(r.level(), 1);
    // the pointer leaves: still there at 1.49 s ...
    EXPECT_EQ(run(r, now, 1490, false), 1);
    EXPECT_FALSE(r.animating());
    // ... and fading right after, in FAST_US
    const double mid = run(r, now, 70, false);
    EXPECT_TRUE(r.animating());
    EXPECT_GT(mid, 0.3);
    EXPECT_LT(mid, 0.7);
    EXPECT_EQ(run(r, now, 100, false), 0);
    EXPECT_TRUE(r.hidden());
    EXPECT_EQ(FAST_US, 120 * MS);
}

TEST(FocusReveal, comingBackBeforeTheGraceCancelsTheExit) {
    FocusReveal r;
    gint64 now = 0;
    run(r, now, 500, true);
    run(r, now, 1400, false);
    run(r, now, 100, true);  // back
    EXPECT_EQ(run(r, now, 3000, true), 1);
    // the 1.5 s count again from the new exit
    EXPECT_EQ(run(r, now, 1400, false), 1);
    run(r, now, 400, false);
    EXPECT_TRUE(r.hidden());
}

TEST(FocusReveal, comingBackDuringTheFadeBringsItBackFromWhereItIs) {
    FocusReveal r;
    gint64 now = 0;
    run(r, now, 500, true);
    run(r, now, 1500 + 60, false);
    const double mid = r.level();
    EXPECT_GT(mid, 0);
    EXPECT_LT(mid, 1);
    const double after = run(r, now, 10, true);
    EXPECT_GE(after, mid);  // it goes up, not from zero
    EXPECT_EQ(run(r, now, 300, true), 1);
}

TEST(FocusReveal, aPressedButtonKeepsItFromShowingUntilReleased) {
    FocusReveal r;
    gint64 now = 0;
    // drawing along the top of the page
    EXPECT_EQ(run(r, now, 2000, true, true), 0);
    EXPECT_TRUE(r.hidden());
    // released with the pointer still on the strip: now it shows
    run(r, now, 300, true, false);
    EXPECT_EQ(r.level(), 1);
}

TEST(FocusReveal, aPressedButtonDoesNotTakeAwayWhatIsShown) {
    FocusReveal r;
    gint64 now = 0;
    run(r, now, 500, true);
    EXPECT_EQ(run(r, now, 2000, true, true), 1);
}

TEST(FocusReveal, withoutAnimationItJustAppearsAndDisappears) {
    FocusReveal r;
    gint64 now = 0;
    EXPECT_EQ(r.update(now += 10 * MS, true, false, false), 1);
    EXPECT_FALSE(r.animating());
    EXPECT_EQ(run(r, now, 1490, false, false, false), 1);
    EXPECT_EQ(run(r, now, 20, false, false, false), 0);
    EXPECT_TRUE(r.hidden());
}

TEST(FocusReveal, resetHidesAtOnce) {
    FocusReveal r;
    gint64 now = 0;
    run(r, now, 500, true);
    r.reset();
    EXPECT_TRUE(r.hidden());
    EXPECT_EQ(r.level(), 0);
    // and it starts over: the first update has no elapsed time
    EXPECT_EQ(r.update(now + 5000 * MS, false, false, true), 0);
}
