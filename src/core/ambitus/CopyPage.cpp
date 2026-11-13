/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "CopyPage.h"

#include <cmath>
#include <mutex>

#include <cairo.h>
#include <gtk/gtk.h>

#include "control/Control.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfPage.h"
#include "util/Range.h"
#include "util/Util.h"  // for DPI_NORMALIZATION_FACTOR
#include "util/i18n.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"

#include "Board.h"

namespace ambitus {

namespace {
/// Sharp enough to read formulas in a chat, small enough to paste anywhere
constexpr double COPY_DPI = 150.0;
}  // namespace

void copyPageAsImage(Control* ctrl) {
    Document* doc = ctrl->getDocument();
    GdkPixbuf* image = nullptr;
    {
        std::shared_lock<Document> lock(*doc);
        const size_t pageNr = ctrl->getCurrentPageNo();
        if (pageNr >= doc->getPageCount()) {
            return;
        }
        ConstPageRef page = doc->getPage(pageNr);
        Range area(0, 0, page->getWidth(), page->getHeight());
        double zoom = COPY_DPI / Util::DPI_NORMALIZATION_FACTOR;
        if (board::isBoard(*doc)) {
            area = board::previewArea(board::contentBounds(*page), page->getWidth(), page->getHeight());
            zoom = board::exportZoom(area, zoom);
        }
        const int width = std::max(1, static_cast<int>(std::round(area.getWidth() * zoom)));
        const int height = std::max(1, static_cast<int>(std::round(area.getHeight() * zoom)));
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
        cairo_t* cr = cairo_create(surface);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_scale(cr, zoom, zoom);
        cairo_translate(cr, -area.minX, -area.minY);
        if (page->getBackgroundType().isPdfPage()) {
            if (XojPdfPageSPtr pdf = doc->getPdfPage(page->getPdfPageNr())) {
                pdf->render(cr);
            }
        }
        // the PDF is drawn above; the ruling stays out, as Summa's tutor sees the page
        const xoj::view::BackgroundFlags flags = {xoj::view::HIDE_PDF_BACKGROUND, xoj::view::SHOW_IMAGE_BACKGROUND,
                                                  xoj::view::HIDE_RULING_BACKGROUND};
        DocumentView().drawPage(page, cr, true, flags);
        cairo_destroy(cr);
        image = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
        cairo_surface_destroy(surface);
    }
    if (!image) {
        return;
    }
    gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), image);
    g_object_unref(image);
    if (auto* win = ctrl->getWindow()) {
        win->showToast(_("Page copied as an image."));
    }
}

}  // namespace ambitus
