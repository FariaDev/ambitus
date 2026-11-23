#pragma once

#include <cairo.h>

#include "filesystem.h"

namespace PngExport {
cairo_status_t writeSurface(cairo_surface_t* surface, const fs::path& filename);
}
