#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <tuple>

#include <glib.h>
#include <gtest/gtest.h>

#include "control/ExportHelper.h"
#include "control/jobs/ImageExport.h"
#include "control/jobs/PngExport.h"
#include "control/jobs/ProgressListener.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/XojPage.h"
#include "util/StringUtils.h"
#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"

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
    EXPECT_EQ(exporter.getOutputFiles(), std::vector<fs::path>{outputPath()});
    exporter.exportGraphics(&progress);

    EXPECT_TRUE(exporter.getLastErrorMsg().empty()) << exporter.getLastErrorMsg();
    EXPECT_GT(fs::file_size(outputPath()), 0);
}

TEST_P(ImageExportTest, ListsSelectedOutputFiles) {
    doc.addPage(std::make_shared<XojPage>(100, 100));
    doc.addPage(std::make_shared<XojPage>(100, 100));
    range.clear();
    ImageExport exporter(&doc, outputPath(), GetParam(), EXPORT_BACKGROUND_ALL, range);
    EXPECT_TRUE(exporter.getOutputFiles().empty());
    range = {{2, 2}};
    EXPECT_EQ(exporter.getOutputFiles(), std::vector<fs::path>{outputPath()});
    range = {{2, 2}, {0, 0}, {2, 2}};
    const std::vector<fs::path> expected{
            outputDirectory / ("out-1" + outputPath().extension().string()),
            outputDirectory / ("out-3" + outputPath().extension().string())};
    EXPECT_EQ(exporter.getOutputFiles(), expected);
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

class PngExportTest: public ::testing::TestWithParam<std::tuple<cairo_format_t, bool>> {
protected:
    void SetUp() override {
        char* directory = g_dir_make_tmp("xournalpp-png-export-XXXXXX", nullptr);
        ASSERT_NE(directory, nullptr);
        outputDirectory = directory;
        g_free(directory);
    }

    void TearDown() override { fs::remove_all(outputDirectory); }

    fs::path outputDirectory;
};

TEST_P(PngExportTest, MatchesCairoPixelsAndHeader) {
    const auto [format, opaque] = GetParam();
    std::vector<unsigned char> storage(48 * 257, 0xab);
    xoj::util::CairoSurfaceSPtr surface(
            format == CAIRO_FORMAT_ARGB32 || format == CAIRO_FORMAT_RGB24 ?
                    cairo_image_surface_create_for_data(storage.data(), format, 9, 257, 48) :
                    cairo_image_surface_create(format, 9, 257),
            xoj::util::adopt);
    ASSERT_EQ(cairo_surface_status(surface.get()), CAIRO_STATUS_SUCCESS);
    if (format == CAIRO_FORMAT_ARGB32 || format == CAIRO_FORMAT_RGB24) {
        for (int y = 0; y < 257; ++y) {
            const uint32_t alpha = opaque ? 255 : y % 256;
            for (int x = 0; x < 9; ++x) {
                const uint32_t pixel = alpha ? (alpha << 24) | (((x * 37 + y) % (alpha + 1)) << 16) |
                                                       (((x * 53 + y * 7) % (alpha + 1)) << 8) |
                                                       ((x * 97 + y * 13) % (alpha + 1)) :
                                               0x00123456;
                std::memcpy(storage.data() + y * 48 + x * 4, &pixel, sizeof(pixel));
            }
        }
        cairo_surface_mark_dirty(surface.get());
    } else {
        xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
        for (int y = 0; y < 257; ++y) {
            cairo_set_source_rgba(cr.get(), y / 256.0, 0.5, 0.25, y / 256.0);
            cairo_rectangle(cr.get(), y % 9, y, 9, 1);
            cairo_fill(cr.get());
        }
    }
    const auto original = storage;
    auto before = outputDirectory / "before.png";
    auto after = outputDirectory / "after.png";
    const auto status = cairo_surface_write_to_png(surface.get(), char_cast(before.u8string().c_str()));
    ASSERT_EQ(status, CAIRO_STATUS_SUCCESS);
    ASSERT_EQ(PngExport::writeSurface(surface.get(), after), CAIRO_STATUS_SUCCESS);
    EXPECT_EQ(storage, original);

    std::array<unsigned char, 26> beforeHeader{}, afterHeader{};
    std::ifstream(before, std::ios::binary).read(reinterpret_cast<char*>(beforeHeader.data()), beforeHeader.size());
    std::ifstream(after, std::ios::binary).read(reinterpret_cast<char*>(afterHeader.data()), afterHeader.size());
    EXPECT_EQ(beforeHeader, afterHeader);
    xoj::util::GObjectSPtr<GdkPixbuf> beforePixels(
            gdk_pixbuf_new_from_file(char_cast(before.u8string().c_str()), nullptr), xoj::util::adopt);
    xoj::util::GObjectSPtr<GdkPixbuf> afterPixels(
            gdk_pixbuf_new_from_file(char_cast(after.u8string().c_str()), nullptr), xoj::util::adopt);
    ASSERT_NE(beforePixels.get(), nullptr);
    ASSERT_NE(afterPixels.get(), nullptr);
    const int channels = gdk_pixbuf_get_n_channels(beforePixels.get());
    ASSERT_EQ(channels, gdk_pixbuf_get_n_channels(afterPixels.get()));
    for (int y = 0; y < 257; ++y) {
        EXPECT_EQ(std::memcmp(
                          gdk_pixbuf_read_pixels(beforePixels.get()) + y * gdk_pixbuf_get_rowstride(beforePixels.get()),
                          gdk_pixbuf_read_pixels(afterPixels.get()) + y * gdk_pixbuf_get_rowstride(afterPixels.get()),
                          9 * channels),
                  0);
    }
}

TEST_P(PngExportTest, ReportsEmptySurfaceAndOpenErrors) {
    const auto format = std::get<0>(GetParam());
    xoj::util::CairoSurfaceSPtr empty(cairo_image_surface_create(format, 0, 0), xoj::util::adopt);
    EXPECT_EQ(PngExport::writeSurface(empty.get(), outputDirectory / "empty.png"), CAIRO_STATUS_WRITE_ERROR);
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(format, 9, 9), xoj::util::adopt);
    EXPECT_EQ(PngExport::writeSurface(surface.get(), outputDirectory / "missing" / "out.png"),
              CAIRO_STATUS_WRITE_ERROR);
    cairo_surface_finish(surface.get());
    EXPECT_EQ(PngExport::writeSurface(surface.get(), outputDirectory / "finished.png"), CAIRO_STATUS_SURFACE_FINISHED);
}

TEST(PngExport, PreservesSurfaceErrors) {
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_INVALID, 9, 9), xoj::util::adopt);
    EXPECT_EQ(PngExport::writeSurface(surface.get(), {}), CAIRO_STATUS_INVALID_FORMAT);
}

TEST(PngExport, ReportsStreamWriteError) {
#ifndef __linux__
    GTEST_SKIP() << "Requires /dev/full";
#else
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 256, 256), xoj::util::adopt);
    auto* data = cairo_image_surface_get_data(surface.get());
    const auto stride = cairo_image_surface_get_stride(surface.get());
    uint32_t value = 1;
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            value = value * 1664525 + 1013904223;
            const uint32_t pixel = 0xff000000 | (value & 0xffffff);
            std::memcpy(data + y * stride + x * 4, &pixel, sizeof(pixel));
        }
    }
    cairo_surface_mark_dirty(surface.get());
    EXPECT_EQ(PngExport::writeSurface(surface.get(), "/dev/full"), CAIRO_STATUS_WRITE_ERROR);
#endif
}

INSTANTIATE_TEST_SUITE_P(
        Formats, PngExportTest,
        ::testing::Values(std::make_tuple(CAIRO_FORMAT_ARGB32, false), std::make_tuple(CAIRO_FORMAT_ARGB32, true),
                          std::make_tuple(CAIRO_FORMAT_RGB24, false), std::make_tuple(CAIRO_FORMAT_A8, false),
                          std::make_tuple(CAIRO_FORMAT_A1, false), std::make_tuple(CAIRO_FORMAT_RGB16_565, false),
                          std::make_tuple(CAIRO_FORMAT_RGB30, false)));
