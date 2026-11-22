#include <memory>
#include <utility>

#include <glib.h>
#include <gtest/gtest.h>

#include "control/ExportHelper.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "util/PathUtil.h"

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
}
