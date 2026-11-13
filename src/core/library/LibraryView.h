/*
 * Ambitus
 *
 * The notebook library: a side list (Recent, All, Summa courses, own folders)
 * and a grid of covers. It knows nothing of Control; MainWindow hands it the
 * actions it needs.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "CoverRenderer.h"
#include "LibraryStore.h"
#include "NotebookScanner.h"
#include "NotebookSearch.h"
#include "SummaCourses.h"
#include "filesystem.h"

namespace ambitus::library {

class LibraryView {
public:
    struct Actions {
        /// open a notebook in the editor
        std::function<void(const fs::path&)> open;
        /// open a notebook at a page (0-based), from a search hit
        std::function<void(const fs::path&, size_t page)> openAt;
        /// new notebook; the folder is the one being shown (empty for Recent/All)
        std::function<void(const fs::path& folder)> newNotebook;
        /// new board (one page that grows), same folder rule as newNotebook
        std::function<void(const fs::path& folder)> newBoard;
    };

    /// storeFile: covers and folders (config); indexFile: the search index (cache)
    LibraryView(fs::path storeFile, fs::path indexFile, Actions actions);
    ~LibraryView();
    LibraryView(const LibraryView&) = delete;
    LibraryView& operator=(const LibraryView&) = delete;

    GtkWidget* getWidget() const { return root; }

    /// Reads the Summa courses again and rescans every folder.
    void refresh();
    /// Puts the keyboard in the search field.
    void focusSearch();
    /// Brings the search index up to date in a worker thread.
    void reindex();

private:
    enum class SourceKind { Recent, All, Course, Folder };
    struct Source {
        SourceKind kind = SourceKind::Recent;
        size_t index = 0;
        bool operator==(const Source&) const = default;
    };
    struct Entry {
        Notebook nb;
        std::string courseColor;
        std::string place;  ///< course or folder name, shown in Recent/All
    };

    void buildSide();
    void fillSide();
    void showSource(Source s);
    void fillGrid();
    GtkWidget* makeTile(const Entry& e);
    void addTextHits(const std::vector<Entry>& everywhere);
    GtkWidget* makeHit(const SearchHit& hit, const Entry& e);
    void openCoverPopover(GtkWidget* tile, const Entry& e);
    void addFolderDialog();

    std::vector<Entry> entriesFor(Source s) const;
    std::vector<Entry> recentEntries() const;
    std::string sourceTitle(Source s) const;
    fs::path sourceFolder(Source s) const;

    CoverInput coverInput(const Entry& e);
    GdkPixbuf* previewOf(const fs::path& p);
    GdkPixbuf* imageOf(const fs::path& p);

    LibraryStore store;
    Actions actions;
    std::vector<Course> courses;
    std::vector<std::vector<Notebook>> courseBooks;
    std::vector<std::vector<Notebook>> folderBooks;
    Source current{SourceKind::Recent};
    std::string filter;

    std::map<fs::path, GdkPixbuf*> previews;  ///< owned; null when the file has none
    std::map<fs::path, GdkPixbuf*> images;    ///< owned

    GtkWidget* root = nullptr;
    GtkWidget* side = nullptr;
    GtkWidget* search = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* body = nullptr;
    GtkWidget* scroller = nullptr;
    GtkWidget* subtitle = nullptr;

    fs::path indexFile;
    std::shared_ptr<SearchIndex> index;                          ///< null until the first indexing ends
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);  ///< false once this view is gone
    bool indexing = false;
    bool indexAgain = false;
};

}  // namespace ambitus::library
