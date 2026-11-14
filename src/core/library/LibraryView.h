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

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "CoverRenderer.h"
#include "InkReader.h"
#include "LibraryStore.h"
#include "NotebookScanner.h"
#include "NotebookSearch.h"
#include "SummaCourses.h"
#include "filesystem.h"

namespace ambitus::library {

/// A notebook as the command box lists it
struct NotebookRef {
    fs::path path;
    std::string name;   ///< file name without the extension
    std::string place;  ///< course or folder name; may be empty
};

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
    /// Every notebook of the library, newest first, with the recent ones that are outside of it (as scanned by the last
    /// refresh); what the command box searches
    std::vector<NotebookRef> allNotebooks() const;
    /// The notebooks opened lately, as the "Recent" of the library shows them
    std::vector<NotebookRef> recentNotebooks() const;
    /// Colour of the course the file is in ("#rrggbb"), empty when none
    std::string colorOf(const fs::path& file) const;
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
    /// Adds the tiles to the body: boards (wide cards) in a grid of their own, then the notebooks
    void addTiles(const std::vector<const Entry*>& entries);
    bool isBoard(const fs::path& p) const;
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
    std::set<fs::path> shownBoards;                              ///< boards drawn as such by the last fillGrid
    std::shared_ptr<SearchIndex> index;                          ///< null until the first indexing ends
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);  ///< false once this view is gone
    bool indexing = false;
    bool indexAgain = false;

    /// The reading of the handwriting through Summa (InkReader), after each indexing
    std::shared_ptr<std::atomic<bool>> stopInk =
            std::make_shared<std::atomic<bool>>(false);    ///< set when the view goes
    InkStop inkStop = InkStop::None;                       ///< why the last run stopped
    std::chrono::steady_clock::time_point inkStoppedAt{};  ///< when; a stop is not retried for a while
    /// The line under the text hits that says how far the reading of the handwriting is
    std::string inkStatus() const;
};

}  // namespace ambitus::library
