/*
 * Ambitus
 *
 * What goes to Summa: the selection with a margin, else the part on screen (the page with most of it on screen), else
 * the whole page. The geometry is in page points and is rounded outward to whole points.
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ambitus/SummaCrop.h"

using namespace ambitus::summacrop;

namespace {
constexpr double A4_W = 595.0;
constexpr double A4_H = 842.0;

void expectRect(const std::optional<Rect>& got, Rect want) {
    ASSERT_TRUE(got.has_value()) << "expected a crop, got the whole page";
    EXPECT_DOUBLE_EQ(got->x, want.x);
    EXPECT_DOUBLE_EQ(got->y, want.y);
    EXPECT_DOUBLE_EQ(got->width, want.width);
    EXPECT_DOUBLE_EQ(got->height, want.height);
}
}  // namespace

TEST(SummaCrop, theModeIsThePageOrTheScreenAndAnUnknownIdIsTheScreen) {
    EXPECT_EQ(sendModeFromId("page"), SendMode::Page);
    EXPECT_EQ(sendModeFromId("screen"), SendMode::Screen);
    EXPECT_EQ(sendModeFromId("qualquer"), SendMode::Screen);
    EXPECT_EQ(sendModeFromId(""), SendMode::Screen);
    EXPECT_STREQ(sendModeId(SendMode::Page), "page");
    EXPECT_STREQ(sendModeId(SendMode::Screen), "screen");
}

TEST(SummaCrop, aSelectionInsideThePageGoesWithItsBoundsAndTheMargin) {
    const SelectionOnPage sel{0, A4_W, A4_H, Rect{100, 200, 50, 40}};
    expectRect(selectionCrop(sel), Rect{88, 188, 74, 64});
}

TEST(SummaCrop, aSelectionAtTheEdgeIsClampedToThePage) {
    expectRect(selectionCrop(SelectionOnPage{0, A4_W, A4_H, Rect{0, 0, 50, 50}}), Rect{0, 0, 62, 62});
    expectRect(selectionCrop(SelectionOnPage{0, A4_W, A4_H, Rect{580, 820, 10, 10}}), Rect{568, 808, 27, 34});
}

TEST(SummaCrop, aSelectionThatCoversThePageIsStillCroppedToIt) {
    // a lasso always goes alone, even when it takes the whole page: its place is the page, whole points
    expectRect(selectionCrop(SelectionOnPage{0, A4_W, A4_H, Rect{0, 0, A4_W, A4_H}}), Rect{0, 0, 595, 842});
    expectRect(selectionCrop(SelectionOnPage{0, A4_W, A4_H, Rect{-50, -50, 800, 1000}}), Rect{0, 0, 595, 842});
}

TEST(SummaCrop, aSelectionOutsideThePageHasNoPart) {
    EXPECT_FALSE(selectionCrop(SelectionOnPage{0, A4_W, A4_H, Rect{700, 900, 20, 20}}).has_value());
    EXPECT_FALSE(selectionCrop(SelectionOnPage{0, 0, 0, Rect{1, 1, 1, 1}}).has_value());
}

TEST(SummaCrop, aPartIsRoundedOutwardToWholePoints) {
    expectRect(fitToPage(Rect{10.4, 20.2, 100.1, 50.0}, A4_W, A4_H), Rect{10, 20, 101, 51});
    expectRect(fitToPage(Rect{-3.5, -0.5, 10, 10}, A4_W, A4_H), Rect{0, 0, 7, 10});
}

TEST(SummaCrop, theEdgeOfAPageThatIsNotWholePointsEndsAtTheLastWholePoint) {
    // A4 is 595.276 x 841.890 pt: the part that reaches the edge stops at 595 x 841, never past the page
    expectRect(fitToPage(Rect{0, 0, 595.276, 841.890}, 595.276, 841.890), Rect{0, 0, 595, 841});
}

TEST(SummaCrop, aVisiblePartInsideThePageIsTheCropRoundedOutward) {
    // a third of the page on screen, from the middle of the sheet to the bottom of the window
    expectRect(screenCrop(OnScreen{0, A4_W, A4_H, Rect{0, 100.5, 595, 300}}), Rect{0, 100, 595, 301});
}

TEST(SummaCrop, aVisiblePartThatIsOutsideThePageIsClampedToIt) {
    expectRect(screenCrop(OnScreen{0, A4_W, A4_H, Rect{-10, -10, 100, 100}}), Rect{0, 0, 90, 90});
}

TEST(SummaCrop, aVisiblePartOfAtLeastNinetyEightPercentIsTheWholePage) {
    // 826 / 842 is 98.1 % of the sheet, 820 / 842 is 97.4 %
    EXPECT_FALSE(screenCrop(OnScreen{0, A4_W, A4_H, Rect{0, 0, 595, 826}}).has_value());
    EXPECT_FALSE(screenCrop(OnScreen{0, A4_W, A4_H, Rect{0, 0, 595, 842}}).has_value());
    expectRect(screenCrop(OnScreen{0, A4_W, A4_H, Rect{0, 0, 595, 820}}), Rect{0, 0, 595, 820});
}

TEST(SummaCrop, theVisibleAreaOfTheLargestPageWins) {
    // the current page is 3, but the screen shows more of page 4: that page and its part go
    const std::vector<OnScreen> onScreen{
            OnScreen{3, A4_W, A4_H, Rect{0, 600, 595, 242}},  // 242 pt visible
            OnScreen{4, A4_W, A4_H, Rect{0, 0, 595, 500}},    // 500 pt visible
    };
    const Pick p = pick(3, onScreen, std::nullopt, SendMode::Screen, true);
    EXPECT_EQ(p.page, 4u);
    expectRect(p.crop, Rect{0, 0, 595, 500});
    EXPECT_FALSE(p.fromSelection);
    EXPECT_EQ(largestVisible(onScreen), std::optional<size_t>(1));
}

TEST(SummaCrop, aTieKeepsTheFirstPage) {
    const std::vector<OnScreen> onScreen{
            OnScreen{2, A4_W, A4_H, Rect{0, 0, 595, 300}},
            OnScreen{5, A4_W, A4_H, Rect{0, 0, 595, 300}},
    };
    EXPECT_EQ(largestVisible(onScreen), std::optional<size_t>(0));
    EXPECT_FALSE(largestVisible({}).has_value());
}

TEST(SummaCrop, aBoardIsCroppedToWhatIsOnScreen) {
    // a board is one huge page: the window shows a corner of it
    const OnScreen board{0, 20000, 20000, Rect{5000, 5000, 1200, 900}};
    expectRect(screenCrop(board), Rect{5000, 5000, 1200, 900});
    const Pick p = pick(0, {board}, std::nullopt, SendMode::Screen, true);
    expectRect(p.crop, Rect{5000, 5000, 1200, 900});
}

TEST(SummaCrop, theModePageIsTheWholeCurrentPageEvenWhenAPartIsOnScreen) {
    const std::vector<OnScreen> onScreen{OnScreen{0, A4_W, A4_H, Rect{0, 100, 595, 300}}};
    const Pick p = pick(1, onScreen, std::nullopt, SendMode::Page, true);
    EXPECT_EQ(p.page, 1u);
    EXPECT_FALSE(p.crop.has_value());
    EXPECT_FALSE(p.fromSelection);
}

TEST(SummaCrop, aSelectionBeatsTheModePage) {
    const std::optional<SelectionOnPage> sel = SelectionOnPage{0, A4_W, A4_H, Rect{100, 200, 50, 40}};
    const Pick p = pick(0, {}, sel, SendMode::Page, true);
    EXPECT_EQ(p.page, 0u);
    expectRect(p.crop, Rect{88, 188, 74, 64});
    EXPECT_TRUE(p.fromSelection);
}

TEST(SummaCrop, aSelectionBeatsTheScreenAndItsPageIsTheSelectionsPage) {
    const std::vector<OnScreen> onScreen{OnScreen{2, A4_W, A4_H, Rect{0, 0, 595, 800}}};
    const std::optional<SelectionOnPage> sel = SelectionOnPage{1, A4_W, A4_H, Rect{100, 200, 50, 40}};
    const Pick p = pick(2, onScreen, sel, SendMode::Screen, true);
    EXPECT_EQ(p.page, 1u);
    expectRect(p.crop, Rect{88, 188, 74, 64});
    EXPECT_TRUE(p.fromSelection);
}

TEST(SummaCrop, withoutTheSelectionTheModeDecides) {
    const std::vector<OnScreen> onScreen{OnScreen{2, A4_W, A4_H, Rect{0, 0, 595, 300}}};
    const std::optional<SelectionOnPage> sel = SelectionOnPage{1, A4_W, A4_H, Rect{100, 200, 50, 40}};
    const Pick screen = pick(2, onScreen, sel, SendMode::Screen, false);
    EXPECT_EQ(screen.page, 2u);
    expectRect(screen.crop, Rect{0, 0, 595, 300});
    EXPECT_FALSE(screen.fromSelection);

    const Pick page = pick(2, onScreen, sel, SendMode::Page, false);
    EXPECT_EQ(page.page, 2u);
    EXPECT_FALSE(page.crop.has_value());
}

TEST(SummaCrop, withoutAnyPageOnScreenTheCurrentPageIsSentWhole) {
    const Pick p = pick(4, {}, std::nullopt, SendMode::Screen, true);
    EXPECT_EQ(p.page, 4u);
    EXPECT_FALSE(p.crop.has_value());
}

TEST(SummaCrop, aRotatedSelectionIsBoundedByItsTurnedBox) {
    // 10 x 4 turned a quarter turn is 4 wide and 10 high, around the same centre (5, 2)
    constexpr double QUARTER_TURN = 1.57079632679489661923;
    const Rect r = rotatedBounds(Rect{0, 0, 10, 4}, QUARTER_TURN);
    EXPECT_NEAR(r.x, 3, 1e-9);
    EXPECT_NEAR(r.y, -3, 1e-9);
    EXPECT_NEAR(r.width, 4, 1e-9);
    EXPECT_NEAR(r.height, 10, 1e-9);
    const Rect same = rotatedBounds(Rect{1, 2, 10, 4}, 0);
    EXPECT_DOUBLE_EQ(same.x, 1);
    EXPECT_DOUBLE_EQ(same.width, 10);
}
