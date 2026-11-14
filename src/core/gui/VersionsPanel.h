/*
 * Ambitus
 *
 * File → Earlier versions: a panel at the right of the page with the copies kept before each save (see Versions.h),
 * grouped by day. Clicking a copy shows it in place of the page, read only, with a strip at the top ("Back to the
 * current", "Restore this one"); the editable page stays hidden below it. Nothing opens in a tab of its own.
 *
 * A copy is loaded and drawn with the same code as the page (LoadHandler and DocumentView), away from the window's
 * thread: the rows are shown at once and get their page count and thumbnail as each copy is read, one at a time.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "ambitus/Versions.h"
#include "util/raii/CairoWrappers.h"

#include "filesystem.h"

class Control;
class Document;
class PdfCache;

namespace ambitus {

class VersionsPanel {
public:
    /// overlay: the one of the editor, where the preview of a copy goes, over `area` (the widget of the pages);
    /// focusBack: what gets the keyboard back when the preview is left
    VersionsPanel(Control* control, GtkOverlay* overlay, GtkWidget* area, GtkWidget* focusBack);
    ~VersionsPanel();

    /// The panel itself, for the window to put at the right of the pages (starts hidden)
    GtkWidget* getWidget() const { return panel; }

    /// Opens the panel, or closes it (leaving the preview)
    void toggle();
    void close();
    bool isOpen() const;
    bool isPreviewing() const { return shown.has_value(); }

    /// The tabs changed (opened, closed, switched, saved): the list follows the notebook of the active tab, and the
    /// preview of a notebook that is no longer the active one is left
    void tabsChanged();
    /// The notebook was put back to a version: out of the preview, the list with the copy kept before it
    void restored();
    /// Back to the editable page
    void leavePreview();

private:
    /// What is known of a copy once it was read: the pages and the first of them, small
    struct Info {
        int pages = -1;  ///< -1 not read yet, 0 could not be read
        xoj::util::CairoSurfaceSPtr thumb;
    };
    struct Row {
        fs::path file;  ///< empty for "Now"
        GtkWidget* sub = nullptr;
        GtkWidget* thumb = nullptr;
    };

    fs::path activeNotebook() const;
    void sync();
    void refresh();
    void rebuild();
    void showEmpty(const std::string& text);
    void selectRow(const fs::path& file);

    void pumpLoader();
    void infoRead(const fs::path& file, std::shared_ptr<Info> info);
    void updateRow(const Row& row);
    void readCurrent();

    void show(const versions::Version& v);
    void documentRead(uint64_t serial, const versions::Version& v, std::unique_ptr<Document> doc, std::string error);
    void layoutPages(int width);
    void drawPreview(cairo_t* cr);
    cairo_surface_t* pageSurface(size_t i);
    static gboolean onChildPosition(GtkOverlay* overlay, GtkWidget* w, GdkRectangle* alloc, VersionsPanel* self);

    Control* control = nullptr;
    GtkOverlay* overlay = nullptr;
    GtkWidget* area = nullptr;
    GtkWidget* focusBack = nullptr;
    gulong positionId = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    // the panel
    GtkWidget* panel = nullptr;
    GtkWidget* listScroll = nullptr;
    GtkWidget* list = nullptr;
    GtkWidget* emptyLabel = nullptr;
    fs::path notebook;
    std::vector<versions::Version> copies;
    std::vector<Row> rows;
    std::map<fs::path, std::shared_ptr<Info>> infos;
    bool built = false;  ///< the rows are those of the notebook and the copies above
    std::vector<fs::path> toRead;
    bool reading = false;

    // the preview
    GtkWidget* preview = nullptr;
    GtkWidget* banner = nullptr;
    GtkWidget* bannerText = nullptr;
    GtkWidget* stage = nullptr;
    GtkWidget* pages = nullptr;
    std::optional<versions::Version> shown;
    uint64_t serial = 0;  ///< which request for a copy is the current one; an older answer is dropped
    std::unique_ptr<Document> doc;
    std::unique_ptr<PdfCache> cache;
    double scale = 1;
    int layoutWidth = 0;
    double totalHeight = 0;
    std::vector<double> tops;
    std::map<size_t, xoj::util::CairoSurfaceSPtr> surfaces;  ///< the pages on screen, drawn at the current scale
    guint sizeIdle = 0;
};

}  // namespace ambitus
