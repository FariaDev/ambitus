#include <memory>
#include <stdexcept>

#include <glib.h>
#include <gtest/gtest.h>

#include "control/ExportHelper.h"
#include "control/jobs/ImageExport.h"
#include "control/jobs/ProgressListener.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/XojPage.h"

class ImageExportTest: public ::testing::TestWithParam<ExportFormat> {
protected:
    void SetUp() override {
        doc.addPage(std::make_shared<XojPage>(100, 100));
        char* directory = g_dir_make_tmp("xournalpp-image-export-XXXXXX", nullptr);
        ASSERT_NE(directory, nullptr);
        outputDirectory = directory;
        g_free(directory);
    }

    void TearDown() override { fs::remove_all(outputDirectory); }

    auto outputPath() const -> fs::path {
        return outputDirectory / (GetParam() == EXPORT_GRAPHICS_SVG ? "out.svg" : "out.png");
    }

    DocumentHandler handler;
    Document doc{&handler};
    PageRangeVector range{{0, 0}};
    DummyProgressListener progress;
    fs::path outputDirectory;
};

TEST_P(ImageExportTest, ExportsBlankPage) {
    ImageExport exporter(&doc, outputPath(), GetParam(), EXPORT_BACKGROUND_ALL, range);
    exporter.exportGraphics(&progress);

    EXPECT_TRUE(exporter.getLastErrorMsg().empty()) << exporter.getLastErrorMsg();
    EXPECT_GT(fs::file_size(outputPath()), 0);
}

TEST_P(ImageExportTest, ReportsWriteError) {
#ifndef __linux__
    GTEST_SKIP() << "Requires /dev/full";
#else
    ASSERT_TRUE(fs::exists("/dev/full"));
    fs::create_symlink("/dev/full", outputPath());
    ImageExport exporter(&doc, outputPath(), GetParam(), EXPORT_BACKGROUND_ALL, range);
    exporter.exportGraphics(&progress);

    EXPECT_FALSE(exporter.getLastErrorMsg().empty());
    EXPECT_THROW(ExportHelper::exportImg(&doc, outputPath(), nullptr, nullptr, 0, 0, 0, EXPORT_BACKGROUND_ALL),
                 std::runtime_error);
#endif
}

INSTANTIATE_TEST_SUITE_P(Graphics, ImageExportTest, ::testing::Values(EXPORT_GRAPHICS_SVG, EXPORT_GRAPHICS_PNG));
