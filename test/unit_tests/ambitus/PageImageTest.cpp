/*
 * Ambitus
 *
 * The page that leaves the program as an image (Summa's "Check", "Send to Summa", --create-img) is opaque and shows
 * the paper: a reader that drops the alpha must not see the ink on black.
 *
 * @license GNU GPLv2 or later
 */

#include <cstring>
#include <memory>
#include <string>

#include <cairo.h>
#include <glib.h>
#include <gtest/gtest.h>

#include "ambitus/SummaPoll.h"
#include "control/jobs/BaseExportJob.h"
#include "control/jobs/ImageExport.h"
#include "control/jobs/ProgressListener.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "util/ElementRange.h"

#include "filesystem.h"

namespace {
constexpr uint32_t PAPER = 0xfff6e8c8;  // a cream paper, so white would not pass by chance

struct Reader {
    const std::string* data;
    size_t pos = 0;
};

auto decode(const std::string& png) -> cairo_surface_t* {
    Reader r{&png};
    return cairo_image_surface_create_from_png_stream(
            [](void* closure, unsigned char* out, unsigned int length) -> cairo_status_t {
                auto* rd = static_cast<Reader*>(closure);
                if (rd->pos + length > rd->data->size()) {
                    return CAIRO_STATUS_READ_ERROR;
                }
                std::memcpy(out, rd->data->data() + rd->pos, length);
                rd->pos += length;
                return CAIRO_STATUS_SUCCESS;
            },
            &r);
}

auto pixel(cairo_surface_t* s, int x, int y) -> uint32_t {
    auto* row = cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s);
    return reinterpret_cast<uint32_t*>(row)[x];
}

struct Stats {
    bool opaque = true;
    int differentFromPaper = 0;
};

auto scan(cairo_surface_t* s) -> Stats {
    Stats st;
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint32_t p = pixel(s, x, y);
            if ((p >> 24) != 0xff) {
                st.opaque = false;
            }
            if (p != PAPER) {
                st.differentFromPaper++;
            }
        }
    }
    return st;
}

void addGraphPage(Document& doc) {
    auto page = std::make_shared<XojPage>(595, 842);
    page->setBackgroundType(PageType(PageTypeFormat::Graph));
    page->setBackgroundColor(Color(PAPER));
    doc.addPage(page);
}

auto readFile(const fs::path& p) -> std::string {
    gchar* contents = nullptr;
    gsize len = 0;
    std::string out;
    if (g_file_get_contents(p.string().c_str(), &contents, &len, nullptr)) {
        out.assign(contents, len);
        g_free(contents);
    }
    return out;
}
}  // namespace

TEST(PageImage, theCheckOfSummaGetsTheOpaquePaperWithItsRuling) {
    DocumentHandler dh;
    Document doc(&dh);
    addGraphPage(doc);
    auto png = ambitus::renderCurrentPage(&doc, 0);
    ASSERT_TRUE(png.has_value());
    cairo_surface_t* s = decode(*png);
    ASSERT_EQ(cairo_surface_status(s), CAIRO_STATUS_SUCCESS);
    const Stats st = scan(s);
    EXPECT_TRUE(st.opaque) << "a pixel with alpha < 255";
    EXPECT_EQ(pixel(s, 0, 0), PAPER) << "the corner is the paper";
    EXPECT_GT(st.differentFromPaper, 0) << "the grid of the page is drawn";
    cairo_surface_destroy(s);
}

TEST(PageImage, anExportWithoutTheRulingStillPaintsThePaper) {
    DocumentHandler dh;
    Document doc(&dh);
    addGraphPage(doc);
    gchar* dir = g_dir_make_tmp("ambitus-pageimage-XXXXXX", nullptr);
    ASSERT_NE(dir, nullptr);
    const fs::path out = fs::path(dir) / "page.png";
    g_free(dir);
    {
        PageRangeVector range;
        range.emplace_back(0, 0);
        DummyProgressListener progress;
        ImageExport exporter(&doc, out, EXPORT_GRAPHICS_PNG, EXPORT_BACKGROUND_UNRULED, range);
        exporter.setQualityParameter(EXPORT_QUALITY_DPI, 40);
        exporter.exportGraphics(&progress);
        ASSERT_EQ(exporter.getLastErrorMsg(), "");
    }
    cairo_surface_t* s = decode(readFile(out));
    ASSERT_EQ(cairo_surface_status(s), CAIRO_STATUS_SUCCESS);
    const Stats st = scan(s);
    EXPECT_TRUE(st.opaque) << "a pixel with alpha < 255";
    EXPECT_EQ(st.differentFromPaper, 0) << "only the paper colour, no grid";
    cairo_surface_destroy(s);
    std::error_code ec;
    fs::remove_all(out.parent_path(), ec);
}
