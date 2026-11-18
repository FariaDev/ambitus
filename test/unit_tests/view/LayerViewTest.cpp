#include <memory>

#include <cairo.h>
#include <gtest/gtest.h>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "view/LayerView.h"
#include "view/View.h"

namespace {

constexpr int SURFACE_SIZE = 200;

struct Pixel {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

auto pixelAt(cairo_surface_t* surface, int x, int y) -> Pixel {
    cairo_surface_flush(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const unsigned char* p = data + y * stride + x * 4;
    return {p[2], p[1], p[0], p[3]};
}

auto drawLayer(const Layer& layer, double clipX = -1, double clipY = -1, double clipW = -1, double clipH = -1)
        -> cairo_surface_t* {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, SURFACE_SIZE, SURFACE_SIZE);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    if (clipW > 0 && clipH > 0) {
        cairo_rectangle(cr, clipX, clipY, clipW, clipH);
        cairo_clip(cr);
    }
    xoj::view::LayerView(&layer).draw(xoj::view::Context::createDefault(cr));
    cairo_destroy(cr);
    return surface;
}

auto makePen() -> std::unique_ptr<Stroke> {
    auto pen = std::make_unique<Stroke>();
    pen->setToolType(StrokeTool::PEN);
    pen->setColor(Color(0xff0000ff));
    pen->setWidth(6);
    pen->setFill(-1);
    pen->addPoint({20, 100});
    pen->addPoint({180, 100});
    return pen;
}

auto makeHighlighter() -> std::unique_ptr<Stroke> {
    auto highlighter = std::make_unique<Stroke>();
    highlighter->setToolType(StrokeTool::HIGHLIGHTER);
    highlighter->setColor(Color(0xffffff00));
    highlighter->setWidth(20);
    highlighter->setFill(-1);
    highlighter->addPoint({100, 40});
    highlighter->addPoint({100, 160});
    return highlighter;
}

void expectSamePixel(const Pixel& a, const Pixel& b) {
    EXPECT_EQ(a.r, b.r);
    EXPECT_EQ(a.g, b.g);
    EXPECT_EQ(a.b, b.b);
    EXPECT_EQ(a.a, b.a);
}

}  // namespace

TEST(LayerView, HighlighterIsDrawnUnderTheInk) {
    Layer layer;
    layer.addElement(makePen());
    layer.addElement(makeHighlighter());

    cairo_surface_t* both = drawLayer(layer);
    const Pixel crossing = pixelAt(both, 100, 100);
    const Pixel highlighterAlone = pixelAt(both, 100, 60);
    cairo_surface_destroy(both);

    EXPECT_EQ(crossing.r, 0);
    EXPECT_EQ(crossing.g, 0);
    EXPECT_EQ(crossing.b, 255);
    EXPECT_EQ(crossing.a, 255);

    Layer onlyHighlighter;
    onlyHighlighter.addElement(makeHighlighter());

    cairo_surface_t* hlOnly = drawLayer(onlyHighlighter);
    expectSamePixel(pixelAt(hlOnly, 100, 100), highlighterAlone);
    cairo_surface_destroy(hlOnly);
}

TEST(LayerView, ClippedRedrawMatchesFullRedraw) {
    Layer layer;
    layer.addElement(makePen());
    layer.addElement(makeHighlighter());

    cairo_surface_t* full = drawLayer(layer);
    cairo_surface_t* clipped = drawLayer(layer, 80, 80, 40, 40);

    for (int y = 80; y < 120; y++) {
        for (int x = 80; x < 120; x++) {
            expectSamePixel(pixelAt(clipped, x, y), pixelAt(full, x, y));
        }
    }

    cairo_surface_destroy(clipped);
    cairo_surface_destroy(full);
}
