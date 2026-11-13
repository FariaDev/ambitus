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
