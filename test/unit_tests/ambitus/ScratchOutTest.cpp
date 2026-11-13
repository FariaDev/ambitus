/*
 * Ambitus
 *
 * Unit tests for scratch out to erase.
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "ambitus/ScratchOut.h"

using namespace ambitus::scratch;

namespace {
/// back and forth `passes` times between x0 and x1, slowly going down from y0 to y1
std::vector<Pt> zigzag(double x0, double x1, double y0, double y1, int passes) {
    std::vector<Pt> pts;
    for (int i = 0; i <= passes * 10; i++) {
        double t = i / 10.0;
        double phase = t - std::floor(t);
        bool forward = static_cast<int>(std::floor(t)) % 2 == 0;
        double x = forward ? x0 + (x1 - x0) * phase : x1 - (x1 - x0) * phase;
        pts.push_back({x, y0 + (y1 - y0) * t / passes});
    }
    return pts;
}
std::vector<Pt> line(double x0, double y0, double x1, double y1, int n = 20) {
    std::vector<Pt> pts;
    for (int i = 0; i <= n; i++) {
        double t = static_cast<double>(i) / n;
        pts.push_back({x0 + (x1 - x0) * t, y0 + (y1 - y0) * t});
    }
    return pts;
}
/// handwriting-like: small loops going forward along the line ("mmmm")
std::vector<Pt> cursive(double x0, double y, double width) {
    std::vector<Pt> pts;
    for (int i = 0; i <= 400; i++) {
        double t = i / 400.0;
        double a = t * 8 * 2 * M_PI;
        pts.push_back({x0 + width * t + 4 * std::cos(a), y + 6 * std::sin(a)});
    }
    return pts;
}
}  // namespace

TEST(ScratchOut, zigzagIsAScribble) {
    Scribble s;
    ASSERT_TRUE(detectScribble(zigzag(100, 160, 100, 112, 6), s));
    EXPECT_EQ(s.passes(), 6u);
    EXPECT_NEAR(std::abs(s.dir.x), 1.0, 0.1);  // it moves along x
}

TEST(ScratchOut, verticalZigzagToo) {
    Scribble s;
    auto pts = zigzag(100, 160, 100, 112, 6);
    for (auto& p: pts) {
        std::swap(p.x, p.y);
    }
    EXPECT_TRUE(detectScribble(pts, s));
}

TEST(ScratchOut, writingAndLinesAreNotScribbles) {
    Scribble s;
    EXPECT_FALSE(detectScribble(line(0, 0, 100, 20), s));
    EXPECT_FALSE(detectScribble(cursive(0, 0, 120), s));
    EXPECT_FALSE(detectScribble(zigzag(100, 160, 100, 112, 4), s));  // a "W": too few passes
    EXPECT_FALSE(detectScribble(zigzag(100, 108, 100, 101, 8), s));  // too small
    EXPECT_FALSE(detectScribble(zigzag(100, 160, 100, 160, 6), s));  // not flat: a big "M"
    EXPECT_FALSE(detectScribble({{0, 0}, {10, 0}}, s));
}

TEST(ScratchOut, crossesOutWhatIsUnder) {
    auto z = zigzag(100, 160, 100, 130, 6);
    Scribble s;
    ASSERT_TRUE(detectScribble(z, s));
    EXPECT_TRUE(crossesOut(s, z, cursive(108, 115, 40), 3));    // a word under it
    EXPECT_FALSE(crossesOut(s, z, cursive(108, 200, 40), 3));   // far below
    EXPECT_FALSE(crossesOut(s, z, cursive(0, 115, 300), 3));    // a long sentence only partly under it
    EXPECT_FALSE(crossesOut(s, z, line(110, 99, 150, 99), 3));  // touched by the first pass only
    EXPECT_FALSE(crossesOut(s, z, {}, 3));
}
