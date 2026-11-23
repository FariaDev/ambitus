#include "PngExport.h"

#include <cerrno>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include <glib/gstdio.h>
#include <png.h>

#include "util/StringUtils.h"

namespace {
void onError(png_structp png, png_const_charp) { png_longjmp(png, 1); }

void onWarning(png_structp, png_const_charp) {}

cairo_status_t encode(cairo_surface_t* surface, FILE* file) {
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    const auto* data = cairo_image_surface_get_data(surface);
    bool opaque = cairo_image_surface_get_format(surface) == CAIRO_FORMAT_RGB24;
    if (!opaque) {
        opaque = true;
        for (int y = 0; y < height && opaque; ++y) {
            for (int x = 0; x < width; ++x) {
                uint32_t pixel;
                std::memcpy(&pixel, data + size_t(y) * stride + size_t(x) * 4, sizeof(pixel));
                if ((pixel >> 24) != 255) {
                    opaque = false;
                    break;
                }
            }
        }
    }

    const int channels = opaque ? 3 : 4;
    auto row = std::unique_ptr<unsigned char[]>(new (std::nothrow) unsigned char[size_t(width) * channels]);
    if (!row) {
        return CAIRO_STATUS_NO_MEMORY;
    }
    auto* png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, onError, onWarning);
    if (!png) {
        return CAIRO_STATUS_NO_MEMORY;
    }
    auto* info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        return CAIRO_STATUS_NO_MEMORY;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        return CAIRO_STATUS_WRITE_ERROR;
    }
    png_init_io(png, file);
    png_set_compression_level(png, 1);
    png_set_filter(png, PNG_FILTER_TYPE_BASE, PNG_FILTER_NONE);
    png_set_IHDR(png, info, width, height, 8, opaque ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGB_ALPHA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_color_16 background{};
    background.red = background.green = background.blue = background.gray = 255;
    png_set_bKGD(png, info, &background);
    png_write_info(png, info);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t pixel;
            std::memcpy(&pixel, data + size_t(y) * stride + size_t(x) * 4, sizeof(pixel));
            auto* out = row.get() + size_t(x) * channels;
            if (opaque) {
                out[0] = (pixel >> 16) & 255;
                out[1] = (pixel >> 8) & 255;
                out[2] = pixel & 255;
            } else {
                const auto alpha = pixel >> 24;
                out[0] = static_cast<unsigned char>(alpha ? (((pixel >> 16) & 255) * 255 + alpha / 2) / alpha : 0);
                out[1] = static_cast<unsigned char>(alpha ? (((pixel >> 8) & 255) * 255 + alpha / 2) / alpha : 0);
                out[2] = static_cast<unsigned char>(alpha ? ((pixel & 255) * 255 + alpha / 2) / alpha : 0);
                out[3] = static_cast<unsigned char>(alpha);
            }
        }
        png_write_row(png, row.get());
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    return CAIRO_STATUS_SUCCESS;
}
}  // namespace

cairo_status_t PngExport::writeSurface(cairo_surface_t* surface, const fs::path& filename) {
    const auto path = filename.u8string();
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return cairo_surface_status(surface);
    }
    const auto format = cairo_image_surface_get_format(surface);
    if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE ||
        (format != CAIRO_FORMAT_ARGB32 && format != CAIRO_FORMAT_RGB24) ||
        cairo_image_surface_get_width(surface) == 0 || cairo_image_surface_get_height(surface) == 0) {
        return cairo_surface_write_to_png(surface, char_cast(path.c_str()));
    }
    auto* cr = cairo_create(surface);
    const auto contextStatus = cairo_status(cr);
    cairo_destroy(cr);
    if (contextStatus != CAIRO_STATUS_SUCCESS) {
        return contextStatus;
    }
    cairo_surface_flush(surface);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        return cairo_surface_status(surface);
    }
    auto* file = g_fopen(char_cast(path.c_str()), "wb");
    if (!file) {
        return errno == ENOMEM ? CAIRO_STATUS_NO_MEMORY : CAIRO_STATUS_WRITE_ERROR;
    }
    auto status = encode(surface, file);
    if (std::fclose(file) != 0 && status == CAIRO_STATUS_SUCCESS) {
        status = CAIRO_STATUS_WRITE_ERROR;
    }
    return status;
}
