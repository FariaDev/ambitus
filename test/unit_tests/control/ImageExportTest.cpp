#include <fstream>
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

TEST_P(ImageExportTest, RejectsInputAsOutput) {
    std::ofstream(outputPath()) << "input bytes";
    doc.setFilepath(outputPath());
    EXPECT_THROW(ExportHelper::exportImg(&doc, outputPath(), nullptr, nullptr, 0, 0, 0, EXPORT_BACKGROUND_ALL),
                 std::invalid_argument);
    EXPECT_EQ(fs::file_size(outputPath()), 11);
}

TEST_P(ImageExportTest, RejectsInputAliases) {
    auto input = outputDirectory / "input.xopp";
    std::ofstream(input) << "input bytes";
    doc.setFilepath(input);
    fs::create_symlink(input, outputPath());
    EXPECT_THROW(ExportHelper::exportImg(&doc, outputPath(), nullptr, nullptr, 0, 0, 0, EXPORT_BACKGROUND_ALL),
                 std::invalid_argument);
    fs::remove(outputPath());
    fs::create_hard_link(input, outputPath());
    EXPECT_THROW(ExportHelper::exportImg(&doc, outputPath(), nullptr, nullptr, 0, 0, 0, EXPORT_BACKGROUND_ALL),
                 std::invalid_argument);
    EXPECT_EQ(fs::file_size(input), 11);
}

TEST_P(ImageExportTest, ChecksAllGeneratedNamesBeforeWriting) {
    doc.addPage(std::make_shared<XojPage>(100, 100));
    auto input = outputPath().parent_path() / ("out-2" + outputPath().extension().string());
    std::ofstream(input) << "input bytes";
    doc.setFilepath(input);
    EXPECT_THROW(ExportHelper::exportImg(&doc, outputPath(), nullptr, nullptr, 0, 0, 0, EXPORT_BACKGROUND_ALL),
                 std::invalid_argument);
    EXPECT_EQ(fs::file_size(input), 11);
    EXPECT_FALSE(fs::exists(outputPath().parent_path() / ("out-1" + outputPath().extension().string())));
}

TEST_P(ImageExportTest, RejectsPdfOverInput) {
    std::ofstream(outputPath()) << "input bytes";
    doc.setFilepath(outputPath());
    EXPECT_THROW(ExportHelper::exportPdf(&doc, outputPath(), nullptr, nullptr, EXPORT_BACKGROUND_ALL, false),
                 std::invalid_argument);
    EXPECT_EQ(fs::file_size(outputPath()), 11);
}

TEST_P(ImageExportTest, AllowsDistinctSameNamedFile) {
    fs::create_directory(outputDirectory / "other");
    auto input = outputDirectory / "other" / outputPath().filename();
    std::ofstream(input) << "input bytes";
    doc.setFilepath(input);
    EXPECT_NO_THROW(ExportHelper::exportImg(&doc, outputPath(), nullptr, nullptr, 0, 0, 0, EXPORT_BACKGROUND_ALL));
    EXPECT_GT(fs::file_size(outputPath()), 0);
    EXPECT_EQ(fs::file_size(input), 11);
}

TEST_P(ImageExportTest, ChecksSavePaths) {
    std::ofstream(outputPath()) << "input bytes";
    EXPECT_THROW(ExportHelper::rejectInputOverwrite(outputPath(), outputDirectory / "." / outputPath().filename()),
                 std::invalid_argument);
    EXPECT_NO_THROW(ExportHelper::rejectInputOverwrite({}, outputPath()));
    EXPECT_NO_THROW(ExportHelper::rejectInputOverwrite(outputPath(), outputDirectory / "new.xopp"));
    auto loop = outputDirectory / "loop";
    fs::create_symlink(loop, loop);
    EXPECT_THROW(ExportHelper::rejectInputOverwrite(loop, outputPath()), std::runtime_error);
}

INSTANTIATE_TEST_SUITE_P(Graphics, ImageExportTest, ::testing::Values(EXPORT_GRAPHICS_SVG, EXPORT_GRAPHICS_PNG));
