#include "CoverRenderer.h"

#include <algorithm>  // for max, min
#include <array>      // for array
#include <cmath>      // for sin, cos, floor
#include <utility>    // for pair
#include <vector>     // for vector

#include <gdk/gdk.h>  // for gdk_cairo_set_source_pixbuf
#include <pango/pangocairo.h>

namespace ambitus::library {

namespace {
constexpr double TAU = 6.283185307179586;

/// The reference sheet draws each generative style on a 120×160 viewBox.
constexpr double REF_W = 120;
constexpr double REF_H = 160;

/// Alpha values of the reference: two weights of the cover ink and two of its opposite.
constexpr double ST = .55;
constexpr double ST2 = .3;
constexpr double W1 = .07;
constexpr double W2 = .16;
constexpr double B1 = .1;
constexpr double B2 = .2;

struct Rgb {
    double r, g, b;
};

auto parseColor(const std::string& c) -> Rgb {
    unsigned v = 0x4a4a4a;
    if (c.size() == 7 && c[0] == '#') {
        v = static_cast<unsigned>(std::stoul(c.substr(1), nullptr, 16));
    }
    return {((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0, (v & 0xff) / 255.0};
}

/// Light covers (like the paper swatch) get dark ink instead of white.
auto isLight(const Rgb& c) -> bool { return .2126 * c.r + .7152 * c.g + .0722 * c.b > .6; }

/// Book shape: 3px corners at the spine, 6px at the fore edge (scaled to the cover width).
void bookPath(cairo_t* cr, double w, double h, double scale) {
    const double rs = 3 * scale, rf = 6 * scale;
    cairo_new_path(cr);
    cairo_move_to(cr, rs, 0);
    cairo_line_to(cr, w - rf, 0);
    cairo_arc(cr, w - rf, rf, rf, -TAU / 4, 0);
    cairo_line_to(cr, w, h - rf);
    cairo_arc(cr, w - rf, h - rf, rf, 0, TAU / 4);
    cairo_line_to(cr, rs, h);
    cairo_arc(cr, rs, h - rs, rs, TAU / 4, TAU / 2);
    cairo_line_to(cr, 0, rs);
    cairo_arc(cr, rs, rs, rs, TAU / 2, 3 * TAU / 4);
    cairo_close_path(cr);
}

/// Closed Catmull-Rom spline through pts.
void closedCurve(cairo_t* cr, const std::vector<std::array<double, 2>>& p) {
    const size_t n = p.size();
    cairo_move_to(cr, p[0][0], p[0][1]);
    for (size_t i = 0; i < n; i++) {
        const auto& p0 = p[(i + n - 1) % n];
        const auto& p1 = p[i];
        const auto& p2 = p[(i + 1) % n];
        const auto& p3 = p[(i + 2) % n];
        cairo_curve_to(cr, p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6, p2[0] - (p3[0] - p1[0]) / 6,
                       p2[1] - (p3[1] - p1[1]) / 6, p2[0], p2[1]);
    }
    cairo_close_path(cr);
}

/// The two inks of the generative covers: the cover's own light or dark ink, and its opposite.
struct Ink {
    double value;  // 1 on dark covers (white), 0 on light ones (near black)

    void light(cairo_t* cr, double alpha) const { cairo_set_source_rgba(cr, value, value, value, alpha); }
    void dark(cairo_t* cr, double alpha) const { cairo_set_source_rgba(cr, 1 - value, 1 - value, 1 - value, alpha); }
};

/// Scales the cairo context from the 120×160 reference sheet to the cover.
struct RefFrame {
    RefFrame(cairo_t* c, double w, double h): cr(c) {
        cairo_save(cr);
        cairo_scale(cr, w / REF_W, h / REF_H);
    }
    ~RefFrame() { cairo_restore(cr); }
    RefFrame(const RefFrame&) = delete;
    RefFrame& operator=(const RefFrame&) = delete;
    cairo_t* cr;
};

/// Adds a rounded rectangle to the current path (does not clear it).
void roundRectSub(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -TAU / 4, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, TAU / 4);
    cairo_arc(cr, x + r, y + h - r, r, TAU / 4, TAU / 2);
    cairo_arc(cr, x + r, y + r, r, TAU / 2, 3 * TAU / 4);
    cairo_close_path(cr);
}

/// A quarter circular arc as in an SVG path "A r r 0 0 sweep x y" (large-arc is always 0 here).
void quarterArc(cairo_t* cr, double x0, double y0, double x1, double y1, double r, bool sweep) {
    const double mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    const double dx = x1 - x0, dy = y1 - y0;
    const double d = std::hypot(dx, dy);
    const double h = d > 0 ? std::sqrt(std::max(0.0, r * r - d * d / 4)) : r;
    const double px = d > 0 ? -dy / d * h : 0, py = d > 0 ? dx / d * h : h;
    auto ccw = [](double a0, double a1) {
        double delta = a1 - a0;
        while (delta < 0) {
            delta += TAU;
        }
        while (delta >= TAU) {
            delta -= TAU;
        }
        return delta;
    };
    auto angles = [&](double cx, double cy) {
        return std::pair<double, double>{std::atan2(y0 - cy, x0 - cx), std::atan2(y1 - cy, x1 - cx)};
    };
    const auto [a0a, a1a] = angles(mx + px, my + py);
    const auto [a0b, a1b] = angles(mx - px, my - py);
    const double da = ccw(a0a, a1a);
    // the small arc is the one that turns less than half a circle
    const bool useA = sweep ? (da < M_PI) : (da > M_PI);
    const double cx = useA ? mx + px : mx - px;
    const double cy = useA ? my + py : my - py;
    const auto [a0, a1] = useA ? std::pair{a0a, a1a} : std::pair{a0b, a1b};
    if (sweep) {
        cairo_arc(cr, cx, cy, r, a0, a1);
    } else {
        cairo_arc_negative(cr, cx, cy, r, a0, a1);
    }
}

/// Paints an alpha mask (one byte per cell) scaled to the whole reference sheet.
void maskSurface(cairo_t* cr, cairo_surface_t* mask, double w, double h, const Ink& ink) {
    cairo_save(cr);
    cairo_scale(cr, w / cairo_image_surface_get_width(mask), h / cairo_image_surface_get_height(mask));
    cairo_set_source_rgba(cr, ink.value, ink.value, ink.value, 1);
    cairo_pattern_t* p = cairo_pattern_create_for_surface(mask);
    cairo_pattern_set_filter(p, CAIRO_FILTER_NEAREST);
    cairo_pattern_set_extend(p, CAIRO_EXTEND_NONE);
    cairo_mask(cr, p);
    cairo_pattern_destroy(p);
    cairo_restore(cr);
}

/// Level curves around one or two peaks: ∂Ω, the contour.
void drawContour(cairo_t* cr, double w, double h, double scale, SeededRandom& r, double ink) {
    std::vector<std::array<double, 2>> peaks = {{w * (.35 + r.next() * .5), h * (.15 + r.next() * .45)}};
    if (r.next() > .4) {
        peaks.push_back({w * (r.next() * .8), h * (.55 + r.next() * .4)});
    }
    for (const auto& [cx, cy]: peaks) {
        const double ph[3] = {r.next() * TAU, r.next() * TAU, r.next() * TAU};
        const double am[3] = {.10 + r.next() * .12, .05 + r.next() * .08, .02 + r.next() * .04};
        const int rings = 7 + static_cast<int>(std::floor(r.next() * 4));
        for (int k = 1; k <= rings; k++) {
            const double radius = k * (9 + r.next() * 1.5) * scale;
            std::vector<std::array<double, 2>> pts;
            for (int j = 0; j < 48; j++) {
                const double t = j / 48.0 * TAU;
                const double f = 1 + am[0] * std::sin(2 * t + ph[0] + k * .15) +
                                 am[1] * std::sin(3 * t + ph[1] - k * .1) + am[2] * std::sin(5 * t + ph[2]);
                pts.push_back({cx + std::cos(t) * radius * f * 1.05, cy + std::sin(t) * radius * f});
            }
            cairo_new_path(cr);
            closedCurve(cr, pts);
            const bool strong = k % 4 == 0;
            cairo_set_source_rgba(cr, ink, ink, ink, strong ? .42 : .2);
            cairo_set_line_width(cr, (strong ? 1.2 : .8) * scale);
            cairo_stroke(cr);
        }
    }
}

/// A field of short streamlines following sin(x) + cos(y).
void drawField(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const double p = r.next() * 4;
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 0; i < 46; i++) {
        double x = r.next() * REF_W, y = r.next() * REF_H;
        cairo_new_path(cr);
        cairo_move_to(cr, x, y);
        for (int s = 0; s < 28; s++) {
            const double a = (std::sin(x * .035 + p) + std::cos(y * .04) * 1.3) * 1.6;
            x += std::cos(a) * 3.2;
            y += std::sin(a) * 3.2;
            cairo_line_to(cr, x, y);
        }
        pen.light(cr, i % 4 ? ST2 : ST);
        cairo_set_line_width(cr, .8);
        cairo_stroke(cr);
    }
}

/// Interfering sine waves.
void drawWaves(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const double p = r.next() * 6;
    for (int k = 0; k < 22; k++) {
        cairo_new_path(cr);
        const double y0 = 8 + k * 7.2;
        for (double x = 0; x <= REF_W; x += 3) {
            const double y = y0 + 4 * std::sin(x * .07 + k * .35 + p) * std::sin(x * .021 + k * .2);
            if (x == 0) {
                cairo_move_to(cr, x, y);
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        pen.light(cr, k % 5 ? ST2 : ST);
        cairo_set_line_width(cr, .8);
        cairo_stroke(cr);
    }
}

/// Seeds placed at the golden angle, the sunflower.
void drawSunflower(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const double cx = REF_W * (.4 + .3 * r.next());
    const double cy = REF_H * (.3 + .15 * r.next());
    const double ga = M_PI * (3 - std::sqrt(5));
    for (int i = 0; i < 260; i++) {
        const double rr = 4.4 * std::sqrt(i), a = i * ga;
        cairo_new_path(cr);
        cairo_arc(cr, cx + rr * std::cos(a), cy + rr * std::sin(a), .6 + i / 260.0 * 1.7, 0, TAU);
        pen.light(cr, i % 13 ? ST2 : ST);
        cairo_fill(cr);
    }
}

/// Truchet tiles: two quarter arcs per square, in one of two arrangements.
void drawTruchet(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const double s = 14, half = s / 2;
    cairo_new_path(cr);
    for (double y = 0; y < REF_H; y += s) {
        for (double x = 0; x < REF_W; x += s) {
            if (r.next() < .5) {
                cairo_new_sub_path(cr);
                cairo_arc(cr, x, y, half, 0, M_PI / 2);
                cairo_new_sub_path(cr);
                cairo_arc_negative(cr, x + s, y + s, half, 3 * M_PI / 2, M_PI);
            } else {
                cairo_new_sub_path(cr);
                cairo_arc_negative(cr, x + s, y, half, M_PI, M_PI / 2);
                cairo_new_sub_path(cr);
                cairo_arc(cr, x, y + s, half, 3 * M_PI / 2, TAU);
            }
        }
    }
    pen.light(cr, ST2);
    cairo_set_line_width(cr, 1.1);
    cairo_stroke(cr);
}

/// Nested Lissajous figures.
void drawLissajous(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const std::pair<int, int> options[] = {{3, 4}, {3, 2}, {5, 4}, {1, 2}, {5, 6}};
    const auto [pa, pb] = options[static_cast<size_t>(std::floor(r.next() * 5))];
    const double dl = r.next() * 3;
    for (int k = 0; k < 5; k++) {
        const double s = 1 - k * .11;
        cairo_new_path(cr);
        for (int i = 0; i <= 240; i++) {
            const double t = i / 240.0 * TAU;
            const double x = REF_W / 2 + REF_W * .38 * s * std::sin(pa * t + dl);
            const double y = REF_H * .4 + REF_H * .3 * s * std::sin(pb * t);
            if (i == 0) {
                cairo_move_to(cr, x, y);
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        pen.light(cr, k ? ST2 : ST);
        cairo_set_line_width(cr, k ? .6 : 1);
        cairo_stroke(cr);
    }
}

/// A small graph: points and the near neighbours among them.
void drawGraph(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    std::array<std::array<double, 2>, 34> p;
    for (auto& [x, y]: p) {
        x = r.next() * REF_W;
        y = r.next() * REF_H * .8;
    }
    cairo_new_path(cr);
    for (size_t i = 0; i < p.size(); i++) {
        for (size_t j = i + 1; j < p.size(); j++) {
            const double dx = p[i][0] - p[j][0], dy = p[i][1] - p[j][1];
            if (dx * dx + dy * dy < 900) {
                cairo_move_to(cr, p[i][0], p[i][1]);
                cairo_line_to(cr, p[j][0], p[j][1]);
            }
        }
    }
    pen.light(cr, ST2);
    cairo_set_line_width(cr, .6);
    cairo_stroke(cr);
    for (bool strong: {false, true}) {
        cairo_new_path(cr);
        for (size_t i = 0; i < p.size(); i++) {
            if ((i % 6 == 0) != strong) {
                continue;
            }
            cairo_new_sub_path(cr);
            cairo_arc(cr, p[i][0], p[i][1], strong ? 2.3 : 1.3, 0, TAU);
        }
        pen.light(cr, strong ? ST : ST2);
        cairo_fill(cr);
    }
}

/// A grid deformed by a field around one centre.
void drawMesh(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const double cx = REF_W * (.3 + .4 * r.next()), cy = REF_H * .4;
    auto warp = [cx, cy](double x, double y) {
        const double dx = x - cx, dy = y - cy;
        const double d = std::hypot(dx, dy) + 1;
        const double f = 600 / (d * d / 30 + 40);
        return std::pair{x + dx / d * f, y + dy / d * f};
    };
    cairo_new_path(cr);
    for (int k = 0; k <= REF_W; k += 10) {
        for (double y = 0; y <= REF_H; y += 4) {
            const auto [x, yy] = warp(k, y);
            if (y == 0) {
                cairo_move_to(cr, x, yy);
            } else {
                cairo_line_to(cr, x, yy);
            }
        }
    }
    for (int k = 0; k <= REF_H; k += 10) {
        for (double x = 0; x <= REF_W; x += 4) {
            const auto [xx, y] = warp(x, k);
            if (x == 0) {
                cairo_move_to(cr, xx, y);
            } else {
                cairo_line_to(cr, xx, y);
            }
        }
    }
    pen.light(cr, ST2);
    cairo_set_line_width(cr, .6);
    cairo_stroke(cr);
}

/// Ford circles: one circle above each fraction p/q, on the base line.
void drawFord(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const double a = .05 + .35 * r.next(), s = REF_W / .6;
    auto gcd = [](int x, int y) {
        while (y) {
            const int t = x % y;
            x = y;
            y = t;
        }
        return x;
    };
    for (int q = 1; q <= 22; q++) {
        for (int p = 0; p <= q; p++) {
            if (gcd(p, q) != 1) {
                continue;
            }
            const double rr = s / (2.0 * q * q), cx = (p / double(q) - a) * s;
            if (cx + rr < 0 || cx - rr > REF_W) {
                continue;
            }
            cairo_new_path(cr);
            cairo_arc(cr, cx, rr, rr, 0, TAU);
            if (q < 4) {
                pen.light(cr, W1);
                cairo_fill_preserve(cr);
            }
            pen.light(cr, q < 3 ? ST : ST2);
            cairo_set_line_width(cr, q < 3 ? 1 : .6);
            cairo_stroke(cr);
        }
    }
}

/// The Pascal triangle modulo m, as dots.
void drawPascal(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const int mods[] = {2, 3, 5, 4};
    const int m = mods[static_cast<size_t>(std::floor(r.next() * 4))];
    const int N = 40;
    const double s = REF_W / (N + 2);
    struct Dot {
        double x, y, radius;
        bool strong;
    };
    std::vector<Dot> dots;
    std::vector<int> row{1};
    for (int n = 0; n < N; n++) {
        for (int k = 0; k <= n; k++) {
            const int v = row[k] % m;
            if (v) {
                dots.push_back({REF_W / 2 + (k - n / 2.0) * s, 10 + n * s * .95, s * .18 + s * .22 * v / (m - 1.0),
                                v == m - 1});
            }
        }
        std::vector<int> next;
        next.reserve(n + 3);
        next.push_back(1);
        for (int k = 1; k <= n; k++) {
            next.push_back((row[k - 1] + row[k]) % m);
        }
        next.push_back(1);
        row.swap(next);
    }
    for (bool strong: {false, true}) {
        cairo_new_path(cr);
        for (const auto& d: dots) {
            if (d.strong == strong) {
                cairo_new_sub_path(cr);
                cairo_arc(cr, d.x, d.y, d.radius, 0, TAU);
            }
        }
        pen.light(cr, strong ? ST : ST2);
        cairo_fill(cr);
    }
}

/// The Ulam spiral: the primes on a square spiral.
void drawUlam(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const double s = 4, cx = REF_W / 2 + (r.next() - .5) * 20, cy = REF_H * .42;
    auto isPrime = [](int n) {
        if (n < 2) {
            return false;
        }
        for (int i = 2; i * i <= n; i++) {
            if (n % i == 0) {
                return false;
            }
        }
        return true;
    };
    std::vector<std::array<double, 2>> strong, weak;
    const int dv[4][2] = {{1, 0}, {0, -1}, {-1, 0}, {0, 1}};
    int x = 0, y = 0, n = 1, step = 1, dir = 0;
    auto put = [&] {
        const double px = cx + x * s, py = cy + y * s;
        if (px > -2 && px < REF_W + 2 && py > -2 && py < REF_H + 2 && isPrime(n)) {
            (x == y || x == -y ? strong : weak).push_back({px, py});
        }
    };
    put();
    while (n < 2400) {
        for (int t = 0; t < 2; t++) {
            for (int i = 0; i < step; i++) {
                x += dv[dir][0];
                y += dv[dir][1];
                n++;
                put();
            }
            dir = (dir + 1) % 4;
        }
        step++;
    }
    for (bool isStrong: {false, true}) {
        cairo_new_path(cr);
        for (const auto& p: isStrong ? strong : weak) {
            roundRectSub(cr, p[0] - 1.2, p[1] - 1.2, 2.4, 2.4, .6);
        }
        pen.light(cr, isStrong ? ST : ST2);
        cairo_fill(cr);
    }
}

/// The Hilbert curve filling a square.
void drawHilbert(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const int ord = r.next() < .5 ? 4 : 5;
    const int N = 1 << ord;
    const double s = (REF_W - 12) / N, ox = 6 + s / 2, oy = 10 + s / 2;
    auto d2xy = [N](int d) {
        int x = 0, y = 0;
        unsigned t = static_cast<unsigned>(d);
        for (int k = 1; k < N; k *= 2) {
            const int rx = static_cast<int>(1 & (t / 2));
            const int ry = static_cast<int>(1 & (t ^ static_cast<unsigned>(rx)));
            if (ry == 0) {
                if (rx == 1) {
                    x = k - 1 - x;
                    y = k - 1 - y;
                }
                std::swap(x, y);
            }
            x += k * rx;
            y += k * ry;
            t /= 4;
        }
        return std::pair{x, y};
    };
    cairo_new_path(cr);
    for (int i = 0; i < N * N; i++) {
        const auto [x, y] = d2xy(i);
        if (i == 0) {
            cairo_move_to(cr, ox + x * s, oy + y * s);
        } else {
            cairo_line_to(cr, ox + x * s, oy + y * s);
        }
    }
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    pen.light(cr, ST2);
    cairo_set_line_width(cr, ord == 4 ? 1.2 : .8);
    cairo_stroke(cr);
}

/// A surface z = sin(ax)·cos(by), drawn as a wire frame in isometric projection.
void drawSurface(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const double a = 2 + r.next() * 2, b = 2 + r.next() * 2;
    const double amp = 14, sc = 34, c = std::cos(M_PI / 6), sn = std::sin(M_PI / 6);
    auto point = [&](double x, double y) {
        const double z = std::sin(a * x) * std::cos(b * y);
        return std::pair{REF_W / 2 + (x - y) * c * sc, REF_H * .42 + (x + y) * sn * sc - z * amp};
    };
    for (int k = 0; k <= 18; k++) {
        const double u = -1 + k / 9.0;
        cairo_new_path(cr);
        for (int i = 0; i <= 40; i++) {
            const double v = -1 + i / 20.0;
            const auto [x, y] = point(u, v);
            if (i == 0) {
                cairo_move_to(cr, x, y);
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        cairo_new_sub_path(cr);
        for (int i = 0; i <= 40; i++) {
            const double v = -1 + i / 20.0;
            const auto [x, y] = point(v, u);
            if (i == 0) {
                cairo_move_to(cr, x, y);
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        pen.light(cr, k % 6 ? ST2 : ST);
        cairo_set_line_width(cr, .6);
        cairo_stroke(cr);
    }
}

/// The Pythagoras tree: a square on the hypotenuse, twice over.
void drawPythagoras(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const double A = .55 + .35 * r.next();
    struct Square {
        std::array<double, 2> p0, p1, p2, p3;
        bool deep;
    };
    std::vector<Square> squares;
    auto addSquare = [&](auto&& self, std::array<double, 2> p0, std::array<double, 2> p1, int dep) -> void {
        const std::array<double, 2> v{p1[0] - p0[0], p1[1] - p0[1]};
        const std::array<double, 2> n{v[1], -v[0]};
        const std::array<double, 2> p2{p1[0] + n[0], p1[1] + n[1]};
        const std::array<double, 2> p3{p0[0] + n[0], p0[1] + n[1]};
        squares.push_back({p0, p1, p2, p3, dep > 6});
        if (dep <= 0) {
            return;
        }
        const double len = std::hypot(v[0], v[1]);
        const std::array<double, 2> u{v[0] / len, v[1] / len};
        const std::array<double, 2> dir{u[0] * std::cos(-A) - u[1] * std::sin(-A),
                                        u[0] * std::sin(-A) + u[1] * std::cos(-A)};
        const std::array<double, 2> m{p3[0] + len * std::cos(A) * dir[0], p3[1] + len * std::cos(A) * dir[1]};
        self(self, p3, m, dep - 1);
        self(self, m, p2, dep - 1);
    };
    const double s = 19;
    addSquare(addSquare, {REF_W / 2 - s / 2 + 6, REF_H * .78}, {REF_W / 2 + s / 2 + 6, REF_H * .78}, 8);
    for (bool deep: {true, false}) {
        cairo_new_path(cr);
        for (const auto& q: squares) {
            if (q.deep != deep) {
                continue;
            }
            cairo_move_to(cr, q.p0[0], q.p0[1]);
            cairo_line_to(cr, q.p1[0], q.p1[1]);
            cairo_line_to(cr, q.p2[0], q.p2[1]);
            cairo_line_to(cr, q.p3[0], q.p3[1]);
            cairo_close_path(cr);
        }
        pen.light(cr, deep ? W2 : W1);
        cairo_fill_preserve(cr);
        pen.light(cr, ST2);
        cairo_set_line_width(cr, .5);
        cairo_stroke(cr);
    }
}

/// Rose curve r = cos(kθ), three nested.
void drawRose(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const std::pair<int, int> options[] = {{5, 3}, {7, 4}, {4, 1}, {5, 2}, {7, 3}};
    const auto [num, den] = options[static_cast<size_t>(std::floor(r.next() * 5))];
    const double k = num / double(den), cx = REF_W / 2, cy = REF_H * .4;
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    for (int j = 0; j < 3; j++) {
        const double R = 46 - j * 13, ph = j * .4, T = TAU * den;
        cairo_new_path(cr);
        for (int i = 0; i <= 900; i++) {
            const double t = i / 900.0 * T, rr = R * std::cos(k * t);
            const double x = cx + rr * std::cos(t + ph), y = cy + rr * std::sin(t + ph);
            if (i == 0) {
                cairo_move_to(cr, x, y);
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        if (j == 0) {
            pen.light(cr, W1);
            cairo_fill_preserve(cr);
        }
        pen.light(cr, j ? ST2 : ST);
        cairo_set_line_width(cr, j ? .6 : .9);
        cairo_stroke(cr);
    }
}

/// The Fibonacci squares and the golden spiral over them.
void drawFibonacci(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    struct Square {
        double x, y, size;
    };
    struct Arc {
        double cx, cy, radius;
        double x0, y0, x1, y1;
    };
    std::vector<Square> squares{{0, 0, 1}};
    std::vector<Arc> arcs{{1, 1, 1, 0, 1, 1, 0}};
    std::array<double, 2> pe{1, 0};
    const int dv[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    double x0 = 0, y0 = 0, x1 = 1, y1 = 1;
    for (int i = 0; i < 10; i++) {
        const int d = i % 4;
        double sx = 0, sy = 0, f = 0, cx = 0, cy = 0;
        if (d == 0) {
            f = y1 - y0;
            sx = x1;
            sy = y0;
            cx = x1;
            cy = y0 + (pe[1] == y0 ? f : 0);
        } else if (d == 1) {
            f = x1 - x0;
            sx = x0;
            sy = y1;
            cx = x0 + (pe[0] == x0 ? f : 0);
            cy = y1;
        } else if (d == 2) {
            f = y1 - y0;
            sx = x0 - f;
            sy = y0;
            cx = x0;
            cy = y0 + (pe[1] == y0 ? f : 0);
        } else {
            f = x1 - x0;
            sx = x0;
            sy = y0 - f;
            cx = x0 + (pe[0] == x0 ? f : 0);
            cy = y0;
        }
        squares.push_back({sx, sy, f});
        const double nx = cx + f * dv[d][0], ny = cy + f * dv[d][1];
        arcs.push_back({cx, cy, f, pe[0], pe[1], nx, ny});
        pe = {nx, ny};
        x0 = std::min(x0, sx);
        y0 = std::min(y0, sy);
        x1 = std::max(x1, sx + f);
        y1 = std::max(y1, sy + f);
    }
    const double sc = (REF_W - 4) / (x1 - x0);
    const bool flip = r.next() < .5;
    cairo_save(cr);
    cairo_translate(cr, flip ? REF_W - 2 : 2, 8);
    cairo_scale(cr, flip ? -sc : sc, sc);
    cairo_translate(cr, -x0, -y0);
    for (size_t i = 0; i < squares.size(); i++) {
        const auto& q = squares[i];
        cairo_rectangle(cr, q.x, q.y, q.size, q.size);
        if (i % 2) {
            pen.light(cr, W1);
            cairo_fill_preserve(cr);
        }
        pen.light(cr, ST2);
        cairo_set_line_width(cr, .6 / sc);
        cairo_stroke(cr);
    }
    cairo_new_path(cr);
    for (size_t i = 0; i < arcs.size(); i++) {
        const auto& a = arcs[i];
        if (i == 0) {
            cairo_move_to(cr, a.x0, a.y0);
        } else {
            cairo_line_to(cr, a.x0, a.y0);
        }
        cairo_arc(cr, a.cx, a.cy, a.radius, std::atan2(a.y0 - a.cy, a.x0 - a.cx), std::atan2(a.y1 - a.cy, a.x1 - a.cx));
    }
    pen.light(cr, ST);
    cairo_set_line_width(cr, 1.1 / sc);
    cairo_stroke(cr);
    cairo_restore(cr);
}

/// Blocks of shapes in tones of the cover colour, after the Bauhaus tiles.
void drawBlocks(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    struct Tone {
        bool dark;
        double alpha;
    };
    const Tone tones[] = {{false, W2}, {false, W1}, {true, B1}, {true, B2}};
    const double s = 40;
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            const double x = i * s, y = j * s;
            const int t = static_cast<int>(std::floor(r.next() * 6));
            const Tone c = tones[static_cast<size_t>(std::floor(r.next() * 4))];
            const int q = static_cast<int>(std::floor(r.next() * 4));
            if (r.next() < .35) {
                const Tone bg = tones[static_cast<size_t>(std::floor(r.next() * 4))];
                cairo_new_path(cr);
                cairo_rectangle(cr, x, y, s, s);
                if (bg.dark) {
                    pen.dark(cr, bg.alpha);
                } else {
                    pen.light(cr, bg.alpha);
                }
                cairo_fill(cr);
            }
            const std::array<std::array<double, 2>, 4> cs{{{x, y}, {x + s, y}, {x + s, y + s}, {x, y + s}}};
            if (t == 0) {
                cairo_new_path(cr);
                cairo_arc(cr, x + s / 2, y + s / 2, s / 2 - 3, 0, TAU);
                if (c.dark) {
                    pen.dark(cr, c.alpha);
                } else {
                    pen.light(cr, c.alpha);
                }
                cairo_fill(cr);
            } else if (t == 1) {
                const double ex = x + s - (cs[q][0] - x), ey = y + s - (cs[q][1] - y);
                cairo_new_path(cr);
                cairo_move_to(cr, cs[q][0], cs[q][1]);
                cairo_line_to(cr, ex, cs[q][1]);
                quarterArc(cr, ex, cs[q][1], cs[q][0], ey, s, q % 2 == 1);
                cairo_close_path(cr);
                if (c.dark) {
                    pen.dark(cr, c.alpha);
                } else {
                    pen.light(cr, c.alpha);
                }
                cairo_fill(cr);
            } else if (t == 2) {
                cairo_new_path(cr);
                cairo_move_to(cr, x, y + s);
                cairo_line_to(cr, x + s / 2, y);
                cairo_line_to(cr, x + s, y + s);
                cairo_close_path(cr);
                if (c.dark) {
                    pen.dark(cr, c.alpha);
                } else {
                    pen.light(cr, c.alpha);
                }
                cairo_fill(cr);
            } else if (t == 3) {
                cairo_new_path(cr);
                if (q < 2) {
                    cairo_arc(cr, x + s / 2, y + s / 2, s / 2, M_PI, TAU);
                } else {
                    cairo_arc(cr, x + s / 2, y + s / 2, s / 2, 3 * M_PI / 2, 5 * M_PI / 2);
                }
                cairo_close_path(cr);
                if (c.dark) {
                    pen.dark(cr, c.alpha);
                } else {
                    pen.light(cr, c.alpha);
                }
                cairo_fill(cr);
            } else if (t == 4) {
                cairo_new_path(cr);
                for (int k = 0; k < 4; k++) {
                    cairo_move_to(cr, x + 5, y + 8 + k * 8);
                    cairo_line_to(cr, x + s - 5, y + 8 + k * 8);
                }
                pen.light(cr, ST2);
                cairo_set_line_width(cr, 1.5);
                cairo_stroke(cr);
            }
        }
    }
}

/// A relief map: levels of two peaks plus a gentle wave, one layer per level.
void drawRelief(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const int n = 60, m = 80;
    // the peaks are drawn in the order of the reference: x, y of each one
    const double x0 = REF_W * (.25 + .5 * r.next()), y0 = REF_H * (.2 + .3 * r.next());
    const double x1 = REF_W * r.next(), y1 = REF_H * (.4 + .3 * r.next());
    const double px[2] = {x0, x1}, py[2] = {y0, y1};
    const double strength[2] = {1, .7};
    auto height = [&](double x, double y) {
        double v = .15 * std::sin(x * .08) * std::cos(y * .07);
        for (int i = 0; i < 2; i++) {
            const double dx = x - px[i], dy = y - py[i];
            v += strength[i] * std::exp(-(dx * dx + dy * dy) / 1800);
        }
        return v;
    };
    std::vector<unsigned char> alpha(static_cast<size_t>(n) * m);
    for (int j = 0; j < m; j++) {
        for (int i = 0; i < n; i++) {
            const double hv = height((i + .5) * REF_W / n, (j + .5) * REF_H / m);
            int layers = 0;
            for (int L = 1; L < 9; L++) {
                if (hv > L * .12) {
                    layers++;
                }
            }
            const double a = 1 - std::pow(1 - .065, layers);
            alpha[static_cast<size_t>(j) * n + i] = static_cast<unsigned char>(a * 255 + .5);
        }
    }
    cairo_surface_t* mask = cairo_image_surface_create(CAIRO_FORMAT_A8, n, m);
    cairo_surface_flush(mask);
    unsigned char* data = cairo_image_surface_get_data(mask);
    const int stride = cairo_image_surface_get_stride(mask);
    for (int j = 0; j < m; j++) {
        std::copy_n(&alpha[static_cast<size_t>(j) * n], n, data + j * stride);
    }
    cairo_surface_mark_dirty(mask);
    maskSurface(cr, mask, REF_W, REF_H, pen);
    cairo_surface_destroy(mask);
}

/// The Julia set z² + c, as a small alpha image blown up to the cover.
void drawJulia(cairo_t* cr, double w, double h, double, SeededRandom& r, double ink) {
    const Ink pen{ink};
    RefFrame frame(cr, w, h);
    const std::array<std::array<double, 2>, 5> constants{
            {{-.8, .156}, {-.4, .6}, {.285, .01}, {-.70176, -.3842}, {-.835, -.2321}}};
    const auto [cr0, ci0] = constants[static_cast<size_t>(std::floor(r.next() * 5))];
    const int n = 80, m = 106;
    const double sc = 2.5 / n;
    cairo_surface_t* mask = cairo_image_surface_create(CAIRO_FORMAT_A8, n, m);
    cairo_surface_flush(mask);
    unsigned char* data = cairo_image_surface_get_data(mask);
    const int stride = cairo_image_surface_get_stride(mask);
    for (int j = 0; j < m; j++) {
        unsigned char* row = data + j * stride;
        for (int i = 0; i < n; i++) {
            double x = (i - n / 2.0) * sc, y = (j - m * .42) * sc;
            int k = 0;
            while (k < 60 && x * x + y * y < 4) {
                const double t = x * x - y * y + cr0;
                y = 2 * x * y + ci0;
                x = t;
                k++;
            }
            const double a = k >= 60 ? .5 : std::pow(k / 60.0, .7) * .55;
            // as the reference: the threshold on the exact value, then two decimals
            row[i] = a > .04 ? static_cast<unsigned char>(std::round(a * 100) / 100 * 255 + .5) : 0;
        }
    }
    cairo_surface_mark_dirty(mask);
    maskSurface(cr, mask, REF_W, REF_H, pen);
    cairo_surface_destroy(mask);
}

void drawLinen(cairo_t* cr, double w, double h, double scale) {
    const double step = 3 * scale;
    cairo_set_line_width(cr, 1);
    cairo_set_source_rgba(cr, 1, 1, 1, .05);
    for (double y = .5; y < h; y += step) {
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, w, y);
    }
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, .07);
    for (double x = .5; x < w; x += step) {
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, h);
    }
    cairo_stroke(cr);
    // elastic band
    cairo_set_source_rgba(cr, 0, 0, 0, .32);
    cairo_rectangle(cr, w - 23 * scale, 0, 7 * scale, h);
    cairo_fill(cr);
}

void drawPage(cairo_t* cr, double w, double h, double scale, const Rgb& c, GdkPixbuf* preview) {
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    if (preview) {
        const double pw = gdk_pixbuf_get_width(preview), phh = gdk_pixbuf_get_height(preview);
        const double s = std::max(w / pw, h / phh);  // fill the width, crop the bottom
        cairo_save(cr);
        cairo_scale(cr, s, s);
        gdk_cairo_set_source_pixbuf(cr, preview, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_set_source_rgb(cr, .89, .91, .93);
        cairo_set_line_width(cr, .6 * scale);
        for (double y = 34 * scale; y < h - 4 * scale; y += 10 * scale) {
            cairo_move_to(cr, 8 * scale, y);
            cairo_line_to(cr, w - 8 * scale, y);
        }
        cairo_stroke(cr);
    }
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_rectangle(cr, 0, 0, w, 6 * scale);
    cairo_fill(cr);
}

void drawImage(cairo_t* cr, double w, double h, GdkPixbuf* image) {
    const double iw = gdk_pixbuf_get_width(image), ih = gdk_pixbuf_get_height(image);
    const double s = std::max(w / iw, h / ih);
    cairo_save(cr);
    cairo_translate(cr, (w - iw * s) / 2, (h - ih * s) / 2);
    cairo_scale(cr, s, s);
    gdk_cairo_set_source_pixbuf(cr, image, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
    // darken the bottom so the title stays readable
    cairo_pattern_t* g = cairo_pattern_create_linear(0, h * .45, 0, h);
    cairo_pattern_add_color_stop_rgba(g, 0, 0, 0, 0, 0);
    cairo_pattern_add_color_stop_rgba(g, 1, 0, 0, 0, .55);
    cairo_set_source(cr, g);
    cairo_paint(cr);
    cairo_pattern_destroy(g);
}

void drawSpine(cairo_t* cr, double h, double scale) {
    cairo_pattern_t* g = cairo_pattern_create_linear(0, 0, 9 * scale, 0);
    cairo_pattern_add_color_stop_rgba(g, 0, 0, 0, 0, .28);
    cairo_pattern_add_color_stop_rgba(g, .7, 0, 0, 0, .08);
    cairo_pattern_add_color_stop_rgba(g, 1, 1, 1, 1, .12);
    cairo_set_source(cr, g);
    cairo_rectangle(cr, 0, 0, 9 * scale, h);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
}

/// Rounded rectangle of radius r.
void roundedPath(cairo_t* cr, double w, double h, double r) {
    cairo_new_path(cr);
    cairo_arc(cr, w - r, r, r, -TAU / 4, 0);
    cairo_arc(cr, w - r, h - r, r, 0, TAU / 4);
    cairo_arc(cr, r, h - r, r, TAU / 4, TAU / 2);
    cairo_arc(cr, r, r, r, TAU / 2, 3 * TAU / 4);
    cairo_close_path(cr);
}

/// Ink of the preview as an alpha mask (paper transparent, ink opaque), cut down to the part with ink.
/// Returns null when the preview has no ink.
auto inkMask(GdkPixbuf* preview, int& x0, int& y0) -> cairo_surface_t* {
    const int w = gdk_pixbuf_get_width(preview), h = gdk_pixbuf_get_height(preview);
    const int n = gdk_pixbuf_get_n_channels(preview), stride = gdk_pixbuf_get_rowstride(preview);
    const guchar* px = gdk_pixbuf_read_pixels(preview);
    std::vector<unsigned char> alpha(static_cast<size_t>(w) * h);
    int minX = w, minY = h, maxX = -1, maxY = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const guchar* p = px + y * stride + x * n;
            const double lum = (.2126 * p[0] + .7152 * p[1] + .0722 * p[2]) / 255;
            const double opacity = n == 4 ? p[3] / 255.0 : 1;
            // the paper and the faint dots of its background stay out
            // the square root lifts the thin, antialiased strokes of a small preview
            const double a = std::sqrt(std::clamp((.85 - lum) / .55, 0.0, 1.0)) * opacity;
            alpha[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(a * 255);
            if (a > .25) {
                minX = std::min(minX, x), maxX = std::max(maxX, x);
                minY = std::min(minY, y), maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < 0) {
        return nullptr;
    }
    const int mw = maxX - minX + 1, mh = maxY - minY + 1;
    cairo_surface_t* mask = cairo_image_surface_create(CAIRO_FORMAT_A8, mw, mh);
    cairo_surface_flush(mask);
    unsigned char* data = cairo_image_surface_get_data(mask);
    const int ms = cairo_image_surface_get_stride(mask);
    for (int y = 0; y < mh; y++) {
        std::copy_n(&alpha[static_cast<size_t>(y + minY) * w + minX], mw, data + y * ms);
    }
    cairo_surface_mark_dirty(mask);
    x0 = minX, y0 = minY;
    return mask;
}

auto titleLayout(cairo_t* cr, const std::string& title, double sizePx, double width) -> PangoLayout* {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* font = pango_font_description_from_string("STIX Two Text");
    pango_font_description_set_absolute_size(font, sizePx * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_width(layout, static_cast<int>(width * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_height(layout, -3);  // at most three lines, ellipsized
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_spacing(layout, static_cast<int>(sizePx * .12 * PANGO_SCALE));
    pango_layout_set_text(layout, title.c_str(), -1);
    return layout;
}

/// Draws a generative style. Except for the contour (whose pixels must not change), the drawing goes to a surface
/// the size of the cover first: many strokes through the rounded clip are much slower than one paint through it.
/// That surface uses the fast antialiasing: the thousands of thin strokes of these styles cost about half as much,
/// and at the size of a cover the difference is not visible.
void drawGenerative(CoverStyle style, cairo_t* cr, double w, double h, double scale, SeededRandom& r, double ink) {
    if (style == CoverStyle::Contour) {
        drawContour(cr, w, h, scale, r, ink);
        return;
    }
    double dx = 1, dy = 1;
    cairo_user_to_device_distance(cr, &dx, &dy);
    dx = std::clamp(dx, 1.0, 3.0);
    dy = std::clamp(dy, 1.0, 3.0);
    cairo_surface_t* off = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(std::ceil(w * dx)),
                                                      static_cast<int>(std::ceil(h * dy)));
    cairo_t* oc = cairo_create(off);
    cairo_scale(oc, dx, dy);
    cairo_set_antialias(oc, CAIRO_ANTIALIAS_FAST);
    switch (style) {
        case CoverStyle::Field:
            drawField(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Waves:
            drawWaves(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Sunflower:
            drawSunflower(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Truchet:
            drawTruchet(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Lissajous:
            drawLissajous(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Graph:
            drawGraph(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Mesh:
            drawMesh(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Ford:
            drawFord(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Pascal:
            drawPascal(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Ulam:
            drawUlam(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Hilbert:
            drawHilbert(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Surface:
            drawSurface(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Pythagoras:
            drawPythagoras(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Rose:
            drawRose(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Fibonacci:
            drawFibonacci(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Blocks:
            drawBlocks(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Relief:
            drawRelief(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Julia:
            drawJulia(oc, w, h, scale, r, ink);
            break;
        case CoverStyle::Contour:
        case CoverStyle::Linen:
        case CoverStyle::Page:
        case CoverStyle::Plain:
            break;
    }
    cairo_destroy(oc);
    cairo_surface_flush(off);
    cairo_save(cr);
    cairo_scale(cr, 1 / dx, 1 / dy);
    cairo_set_source_surface(cr, off, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(off);
}
}  // namespace

SeededRandom::SeededRandom(const std::string& seed): h(1779033703u ^ static_cast<uint32_t>(seed.size())) {
    for (unsigned char ch: seed) {
        h = (h ^ ch) * 3432918353u;
        h = (h << 13) | (h >> 19);
    }
}

auto SeededRandom::next() -> double {
    h = (h ^ (h >> 16)) * 2246822507u;
    h = (h ^ (h >> 13)) * 3266489909u;
    h ^= h >> 16;
    return h / 4294967296.0;
}

auto effectiveColor(const CoverInput& in) -> std::string {
    if (!in.cover.color.empty()) {
        return in.cover.color;
    }
    return in.courseColor.empty() ? DEFAULT_COVER_COLOR : in.courseColor;
}

auto suggestedStyle(const std::string& notebookName) -> CoverStyle {
    static constexpr CoverStyle generative[] = {
            CoverStyle::Contour, CoverStyle::Field,      CoverStyle::Waves, CoverStyle::Sunflower,
            CoverStyle::Truchet, CoverStyle::Lissajous,  CoverStyle::Graph, CoverStyle::Mesh,
            CoverStyle::Ford,    CoverStyle::Pascal,     CoverStyle::Ulam,  CoverStyle::Hilbert,
            CoverStyle::Surface, CoverStyle::Pythagoras, CoverStyle::Rose,  CoverStyle::Fibonacci,
            CoverStyle::Blocks,  CoverStyle::Relief,     CoverStyle::Julia};
    constexpr size_t count = sizeof(generative) / sizeof(generative[0]);
    SeededRandom r(notebookName);
    return generative[static_cast<size_t>(r.next() * count) % count];
}

void drawCover(cairo_t* cr, double w, double h, const CoverInput& in) {
    const double scale = w / 152.0;  // the prototype draws covers at 152×200
    const Rgb c = parseColor(effectiveColor(in));
    const auto style = in.cover.style;

    cairo_save(cr);
    bookPath(cr, w, h, scale);
    cairo_clip(cr);

    if (in.image) {
        drawImage(cr, w, h, in.image);
    } else if (style == CoverStyle::Page) {
        drawPage(cr, w, h, scale, c, in.preview);
    } else {
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        cairo_paint(cr);
        if (style == CoverStyle::Linen) {
            drawLinen(cr, w, h, scale);
        } else if (style != CoverStyle::Plain) {
            SeededRandom r(in.cover.seed ? in.title + "#" + std::to_string(in.cover.seed) : in.title);
            drawGenerative(style, cr, w, h, scale, r, isLight(c) ? 0 : 1);
        }
    }
    if (style != CoverStyle::Page || in.image) {
        drawSpine(cr, h, scale);
    }

    // title: none on the page style (the page speaks for itself)
    if (!in.showTitle || in.title.empty()) {
        // mini covers (style picker) carry no title
    } else if (style == CoverStyle::Linen && !in.image) {
        const double left = 22 * scale, right = 30 * scale, pad = 10 * scale;
        PangoLayout* layout = titleLayout(cr, in.title, 15 * scale, w - left - right - 2 * pad);
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(layout, &lw, &lh);
        cairo_set_source_rgb(cr, .965, .953, .925);  // #f6f3ec
        cairo_rectangle(cr, left, 52 * scale, w - left - right, lh + 2 * pad);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, .114, .114, .114);
        cairo_move_to(cr, left + pad, 52 * scale + pad);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    } else if (style != CoverStyle::Page || in.image) {
        const double left = 20 * scale, right = 14 * scale, bottom = 16 * scale;
        PangoLayout* layout = titleLayout(cr, in.title, 19 * scale, w - left - right);
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(layout, &lw, &lh);
        const double ink = !in.image && isLight(c) ? .11 : 1;
        cairo_set_source_rgb(cr, ink, ink, ink);
        cairo_move_to(cr, left, h - bottom - lh);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }
    cairo_restore(cr);
}

void drawBoardCover(cairo_t* cr, double w, double h, const CoverInput& in, const std::string& kicker) {
    const Rgb c = parseColor(effectiveColor(in));
    const double ink = isLight(c) ? .11 : 1;
    const double paper = isLight(c) ? .11 : .965;

    cairo_save(cr);
    roundedPath(cr, w, h, 8);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_paint(cr);

    // the dots of the board
    cairo_set_source_rgba(cr, ink, ink, ink, .16);
    for (double y = 9; y < h; y += 14) {
        for (double x = 9; x < w; x += 14) {
            cairo_arc(cr, x, y, .9, 0, TAU);
            cairo_fill(cr);
        }
    }

    // the written part, between the kicker and the title
    const double top = 34, bottom = 46, side = 18;
    int x0 = 0, y0 = 0;
    if (cairo_surface_t* mask = in.preview ? inkMask(in.preview, x0, y0) : nullptr) {
        const double mw = cairo_image_surface_get_width(mask), mh = cairo_image_surface_get_height(mask);
        const double bw = w - 2 * side, bh = h - top - bottom;
        // small content is not blown up past twice the preview size
        const double s = std::min({bw / mw, bh / mh, 2.0});
        cairo_save(cr);
        cairo_translate(cr, side + (bw - mw * s) / 2, top + (bh - mh * s) / 2);
        cairo_scale(cr, s, s);
        cairo_set_source_rgba(cr, paper, paper, paper * .97, .92);
        cairo_mask_surface(cr, mask, 0, 0);
        cairo_restore(cr);
        cairo_surface_destroy(mask);
    }

    // kicker
    PangoLayout* k = pango_cairo_create_layout(cr);
    PangoFontDescription* font = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(font, 10 * PANGO_SCALE);
    pango_layout_set_font_description(k, font);
    pango_font_description_free(font);
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(static_cast<int>(1.4 * PANGO_SCALE)));
    pango_layout_set_attributes(k, attrs);
    pango_attr_list_unref(attrs);
    pango_layout_set_text(k, kicker.c_str(), -1);
    cairo_set_source_rgba(cr, ink, ink, ink, .75);
    cairo_move_to(cr, side, 14);
    pango_cairo_show_layout(cr, k);
    g_object_unref(k);

    if (in.showTitle && !in.title.empty()) {
        PangoLayout* layout = titleLayout(cr, in.title, 19, w - 2 * side);
        pango_layout_set_height(layout, -1);  // one line on a board
        int lw = 0, lh = 0;
        pango_layout_get_pixel_size(layout, &lw, &lh);
        cairo_set_source_rgb(cr, ink, ink, ink);
        cairo_move_to(cr, side, h - 14 - lh);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
    }
    cairo_restore(cr);
}

}  // namespace ambitus::library
