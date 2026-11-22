/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <fstream>

#include <config-test.h>
#include <gtest/gtest.h>

#include "model/Image.h"
#include "util/raii/GObjectSPtr.h"

#include "filesystem.h"

TEST(Image, testDownsampleLargeLandscapeImage) {
    xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(gdk_pixbuf_new(GDK_COLORSPACE_RGB, false, 8, 9000, 4000),
                                           xoj::util::adopt);
    ASSERT_NE(pixbuf.get(), nullptr);
    gdk_pixbuf_fill(pixbuf.get(), 0x336699ff);
    gchar* buffer = nullptr;
    gsize length = 0;
    ASSERT_TRUE(gdk_pixbuf_save_to_buffer(pixbuf.get(), &buffer, &length, "png", nullptr, nullptr));
    std::string data(buffer, length);
    g_free(buffer);
    pixbuf.reset();

    Image image;
    image.setImage(std::move(data));
    ASSERT_FALSE(image.renderBuffer().has_value());
    auto* surface = image.getImage();
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(cairo_image_surface_get_width(surface), 8687);
    EXPECT_EQ(cairo_image_surface_get_height(surface), 3861);
    EXPECT_EQ(image.getNaturalSize(), xoj::util::Size<double>(8687, 3861));
}

TEST(Image, clonePreservesFormat) {
    Image image;
    std::ifstream imageFile{fs::path(GET_TESTFILE(u8"images/r90.jpg")), std::ios::binary};
    ASSERT_TRUE(imageFile.is_open());
    image.setImage(std::string(std::istreambuf_iterator<char>(imageFile), {}));
    ASSERT_NE(image.getImageFormat(), nullptr);

    auto clone = image.clone();
    auto* clonedImage = static_cast<Image*>(clone.get());
    ASSERT_NE(clonedImage->getImageFormat(), nullptr);
    EXPECT_NE(clonedImage->getImageFormat(), image.getImageFormat());
    gchar* originalName = gdk_pixbuf_format_get_name(image.getImageFormat());
    gchar* clonedName = gdk_pixbuf_format_get_name(clonedImage->getImageFormat());
    EXPECT_STREQ(clonedName, originalName);
    g_free(originalName);
    g_free(clonedName);
}

TEST(Image, cloneWithoutFormat) {
    Image image;
    auto clone = image.clone();
    EXPECT_EQ(static_cast<Image*>(clone.get())->getImageFormat(), nullptr);
}


TEST(Image, testRenderBufferTruncatedPng) {
    gsize length = 0;
    auto* bytes = g_base64_decode("iVBORw0KGgoAAAANSUhEUgAAAgAAAAIACAYAAAD0eNT6AAAABmJLR0QA/wD/AP+gvac=", &length);
    auto image = Image();
    image.setImage(std::string(reinterpret_cast<const char*>(bytes), length));
    g_free(bytes);

    auto error = image.renderBuffer();

    ASSERT_TRUE(error.has_value());
    EXPECT_NE(error->find("Failed to close image stream"), std::string::npos);
}

TEST(Image, testGetImageApplyOrientation) {
    auto image = Image();

    // Image width is 500px and height 130px - but has exif data saying image should be
    // rotated 90 deg CW to have correct orientation.
    std::ifstream imageFile{fs::path(GET_TESTFILE(u8"images/r90.jpg")), std::ios::binary};
    auto imageData = std::string(std::istreambuf_iterator<char>(imageFile), {});

    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, reinterpret_cast<const guchar*>(imageData.c_str()), imageData.length(), nullptr);
    gdk_pixbuf_loader_close(loader, nullptr);

    GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);

    // Image size before  orientation
    auto origImageSize = xoj::util::Size<double>(gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
    auto rotatedImageSize = xoj::util::Size<double>(origImageSize.height, origImageSize.width);
    g_object_unref(loader);

    image.setImage(imageData);

    // getImage render the image in a cairo surface
    auto surface = image.getImage();

    // Test image now have the correct size - which is the image has been rotated.
    EXPECT_EQ(image.getNaturalSize(), rotatedImageSize);
    EXPECT_EQ(image.getNaturalSize(), xoj::util::Size<double>(130, 500));
    EXPECT_EQ(xoj::util::Size<double>(cairo_image_surface_get_width(surface), cairo_image_surface_get_height(surface)),
              rotatedImageSize);
}
