#include <memory>
#include <stdexcept>
#include <utility>

#include <cairo-pdf.h>
#include <glib.h>
#include <gtest/gtest.h>

#include "control/ExportHelper.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Layer.h"
#include "model/LinkDestination.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfAction.h"
#include "pdf/base/XojPdfBookmarkIterator.h"
#include "pdf/base/XojPdfDocument.h"
#include "util/PathUtil.h"
#include "util/StringUtils.h"
#include "util/raii/CairoWrappers.h"

class PdfExportTest: public ::testing::Test {
protected:
    void SetUp() override {
        std::unique_ptr<char, decltype(&g_free)> directory(g_dir_make_tmp("xournalpp-pdf-export-XXXXXX", nullptr),
                                                          &g_free);
        ASSERT_NE(directory, nullptr);
        outputDirectory = Util::fromGFilename(directory.get());
    }

    void TearDown() override { fs::remove_all(outputDirectory); }

    fs::path outputDirectory;
};

TEST_F(PdfExportTest, ProgressiveLayerRange) {
    DocumentHandler handler;
    Document doc(&handler);
    auto page = std::make_shared<XojPage>(260, 120, true);
    auto addTextLayer = [&page](const char* label, double y) {
        auto* layer = new Layer();
        page->getLayers().push_back(layer);
        auto text = std::make_unique<Text>();
        text->setText(label);
        text->move(20, y);
        layer->addElement(std::move(text));
    };
    addTextLayer("FIRST", 20);
    addTextLayer("SECOND", 65);
    doc.addPage(page);

    const auto output = outputDirectory / "progressive.pdf";
    ASSERT_NO_THROW(ExportHelper::exportPdf(&doc, output, nullptr, "1-2", EXPORT_BACKGROUND_NONE, true,
                                            PdfExportBackend::CAIRO));

    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(output, "", nullptr));
    ASSERT_EQ(pdf.getPageCount(), 2U);
    EXPECT_FALSE(pdf.getPage(0)->findText("FIRST").empty());
    EXPECT_TRUE(pdf.getPage(0)->findText("SECOND").empty());
    EXPECT_FALSE(pdf.getPage(1)->findText("FIRST").empty());
    EXPECT_FALSE(pdf.getPage(1)->findText("SECOND").empty());

    const auto subsetOutput = outputDirectory / "progressive-subset.pdf";
    ASSERT_NO_THROW(ExportHelper::exportPdf(&doc, subsetOutput, nullptr, "2", EXPORT_BACKGROUND_NONE, true,
                                            PdfExportBackend::CAIRO));

    XojPdfDocument subset;
    ASSERT_TRUE(subset.load(subsetOutput, "", nullptr));
    ASSERT_EQ(subset.getPageCount(), 1U);
    EXPECT_TRUE(subset.getPage(0)->findText("FIRST").empty());
    EXPECT_FALSE(subset.getPage(0)->findText("SECOND").empty());

    const auto emptyOutput = outputDirectory / "progressive-empty.pdf";
    EXPECT_THROW(ExportHelper::exportPdf(&doc, emptyOutput, nullptr, "3", EXPORT_BACKGROUND_NONE, true,
                                        PdfExportBackend::CAIRO),
                 std::runtime_error);
    EXPECT_FALSE(fs::exists(emptyOutput));
}

#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 16, 0)
TEST_F(PdfExportTest, ProgressiveLayerRangeRemapsBookmarks) {
    const auto background = outputDirectory / "background.pdf";
    {
        xoj::util::CairoSurfaceSPtr surface(cairo_pdf_surface_create(char_cast(background.u8string().c_str()), 100, 100),
                                           xoj::util::adopt);
        xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
        const int first = cairo_pdf_surface_add_outline(surface.get(), CAIRO_PDF_OUTLINE_ROOT, "First", "page=1",
                                                        CAIRO_PDF_OUTLINE_FLAG_OPEN);
        cairo_pdf_surface_add_outline(surface.get(), first, "Second", "page=2", CAIRO_PDF_OUTLINE_FLAG_OPEN);
        cairo_show_page(cr.get());
        cairo_show_page(cr.get());
        cairo_surface_finish(surface.get());
        ASSERT_EQ(cairo_surface_status(surface.get()), CAIRO_STATUS_SUCCESS);
    }

    DocumentHandler handler;
    Document doc(&handler);
    ASSERT_TRUE(doc.readPdf(background, true, false));
    doc.getPage(1)->getLayers().push_back(new Layer());

    const auto output = outputDirectory / "progressive-bookmarks.pdf";
    ASSERT_NO_THROW(ExportHelper::exportPdf(&doc, output, nullptr, "2", EXPORT_BACKGROUND_ALL, true,
                                            PdfExportBackend::CAIRO));
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(output, "", nullptr));
    ASSERT_EQ(pdf.getPageCount(), 1U);

    std::unique_ptr<XojPdfBookmarkIterator> bookmark(pdf.getContentsIter());
    ASSERT_NE(bookmark, nullptr);
    std::unique_ptr<XojPdfAction> action(bookmark->getAction());
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->getTitle(), "Second");
    EXPECT_EQ(action->getDestination()->getPdfPage(), 0U);
    EXPECT_FALSE(bookmark->next());
}
#endif
