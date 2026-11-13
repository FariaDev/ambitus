#include "CoverRenderer.h"

#include <algorithm>  // for max, min
#include <array>      // for array
#include <cmath>      // for sin, cos, floor
#include <vector>     // for vector

#include <gdk/gdk.h>  // for gdk_cairo_set_source_pixbuf
#include <pango/pangocairo.h>

namespace ambitus::library {

namespace {
constexpr double TAU = 6.283185307179586;

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
        if (style == CoverStyle::Contour) {
            SeededRandom r(in.cover.seed ? in.title + "#" + std::to_string(in.cover.seed) : in.title);
            drawContour(cr, w, h, scale, r, isLight(c) ? 0 : 1);
        } else if (style == CoverStyle::Linen) {
            drawLinen(cr, w, h, scale);
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

}  // namespace ambitus::library
