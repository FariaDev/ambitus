/*
 * Ambitus
 *
 * "Export as PDF" as a card inside the window (docs/design/r6, section 8), in place of the file chooser and the dialog
 * of Xournal++: faded in over the page with a veil, as the "New notebook" card. At the left the pages of the notebook
 * as thumbnails, drawn from the open document one at a time while the card is idle, the ones that are not in the PDF
 * dimmed, and the count under them. At the right which pages (all, this one, a range like "1-3, 5"), whether the paper
 * (ruling, margin) goes, whether to open the PDF afterwards, the name of the file and the folder. Enter exports, Esc
 * cancels; "Advanced..." opens the export dialog of Xournal++ (layers, quality, other formats).
 *
 * What exports is the CustomExportJob of Xournal++, the same engine of the dialog it replaces; when it ends the notice
 * of the window says so, with "Open" (or "Open folder" when the PDF was already opened).
 *
 * The card owns itself: it goes away after it has faded out.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "ambitus/ExportPdf.h"
#include "util/raii/CairoWrappers.h"

#include "filesystem.h"

class Control;
class Document;

namespace ambitus {

class ExportPdfCard {
public:
    /// Opens the card over the window (`overlay` is the one over the whole window). `suggested` is the folder and the
    /// name of the file to start with: the folder of the notebook, or the last one used.
    static void open(Control* ctrl, GtkOverlay* overlay, GtkWindow* window, const fs::path& suggested);

private:
    ExportPdfCard(Control* ctrl, GtkOverlay* overlay, const fs::path& suggested);
    ~ExportPdfCard();

    void show(GtkWindow* window);  ///< fades in; `window` is where the focus goes back to when it closes
    void build(GtkOverlay* overlay);
    GtkWidget* buildPreview();
    GtkWidget* buildOptions();

    void choosePages(exportpdf::Pages which);
    void setPaper(bool on);
    void refresh();  ///< the choice changed: the thumbnails, the count, the lines and "Export" follow it
    void exportNow();
    void startExport(const fs::path& file);
    void advanced();
    void cancel();
    void chooseFolder();
    void close(bool restoreFocus);
    bool isOpen() const;
    bool onKey(GtkWidget* from, GdkEventKey* e);
    void watchKeys(GtkWidget* w);
    std::string typedName() const;
    fs::path targetFile() const;

    // the thumbnails: drawn one by one when the main loop is idle, for the paper as it is chosen
    void measurePages();
    void queueRender();
    bool renderNext();
    void stopRender();
    void drawThumb(size_t page, cairo_t* cr, int w, int h);

    void scheduleDismantle();
    void dismantle();  ///< takes the card out of the window and deletes it
    void onRevealerDestroyed();

    Control* ctrl;
    size_t pageCount = 0;
    size_t current = 0;
    fs::path folder;
    std::string suggestedName;
    fs::path backgroundPdf;  ///< the PDF under the notebook, which must not be overwritten

    exportpdf::Pages pages = exportpdf::Pages::ALL;
    bool withPaper = true;
    exportpdf::Selection selection;
    bool canExport = false;

    struct Thumb {
        int width = 0, height = 0;             ///< the box, in pixels
        xoj::util::CairoSurfaceSPtr drawn[2];  ///< without the paper [0] and with it [1]
        GtkWidget* area = nullptr;
    };
    std::vector<Thumb> thumbs;
    guint renderSource = 0;
    size_t renderNextPage = 0;

    GtkWidget* revealer = nullptr;
    GtkWidget* card = nullptr;
    GtkWidget* count = nullptr;
    std::vector<GtkWidget*> pageButtons;  ///< All, This page, Range
    GtkWidget* rangeEntry = nullptr;
    GtkWidget* rangeProblem = nullptr;
    GtkWidget* paperSwitch = nullptr;
    GtkWidget* openSwitch = nullptr;
    GtkWidget* nameEntry = nullptr;
    GtkWidget* folderLabel = nullptr;
    GtkWidget* folderButton = nullptr;
    GtkWidget* problem = nullptr;
    GtkWidget* advancedButton = nullptr;
    GtkWidget* cancelButton = nullptr;
    GtkWidget* exportButton = nullptr;
    GtkFileChooserNative* chooser = nullptr;
    GtkWidget* focusBack = nullptr;  ///< what had the focus before; weak
    guint dismantleSource = 0;
    bool settingPages = false;
    bool dismantling = false;
};

}  // namespace ambitus
