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
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
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

/// A place for a new notebook: a course of Summa or a folder of the library
struct Subject {
    std::string name;
    fs::path path;
    std::string color;   ///< "#rrggbb"; empty when it has none
    std::string newest;  ///< name of the notebook changed last in it; empty when it has none
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
        /// whether the search may read the handwriting through Summa (the settings); unset: it may
        std::function<bool()> searchInk;
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
    /// The courses of Summa, then the folders of the library: where a new notebook can go (as scanned by the last
    /// refresh)
    std::vector<Subject> subjects() const;
    /// The folder the library is showing; empty for Recent and All
    fs::path currentFolder() const { return sourceFolder(current); }
    /// Colour of the course the file is in ("#rrggbb"), empty when none
    std::string colorOf(const fs::path& file) const;
    /// Brings the search index up to date in a worker thread.
    void reindex();
    /// Puts a folder in the library, as "Add folder" does (the library then shows it); false if it was already there.
    /// `replacing`, when it is one of the folders added by hand, goes out of the library.
    bool setFolder(const fs::path& folder, const fs::path& replacing = {});

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
        std::time_t seen = 0;  ///< when it was opened last (Recent only)
    };
    /// The page where the first recent notebook was left: the page counted, and its thumbnail, once read
    struct ResumeKey {
        fs::path path;
        size_t page = 0;  ///< 0-based, as the metadata of the editor keeps it
        std::time_t modified = 0;
        bool operator<(const ResumeKey& o) const {
            return std::tie(path, page, modified) < std::tie(o.path, o.page, o.modified);
        }
    };
    struct ResumePage {
        size_t page = 0;             ///< 0-based; the one asked for, kept inside the notebook
        size_t total = 0;            ///< 0 until counted
        GdkPixbuf* thumb = nullptr;  ///< owned
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
    /// The "Continue where you left off" card for the first recent notebook
    GtkWidget* makeResumeCard(const Entry& e);
    void startResume(const ResumeKey& key);
    void resumeReady(const ResumeKey& key, size_t total, const std::string& png);
    /// The path of the cover under the pointer (or focus) shows in the foot of the library
    void showPathOnHover(GtkWidget* w, const fs::path& path);
    void addFolderDialog();

    std::vector<Entry> entriesFor(Source s) const;
    std::vector<Entry> recentEntries() const;
    std::string sourceTitle(Source s) const;
    fs::path sourceFolder(Source s) const;
    bool inkSearchOn() const;  ///< the settings let the search read the handwriting

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
    GtkWidget* columns = nullptr;  ///< side and main, above the foot
    GtkWidget* pathLabel = nullptr;
    GtkWidget* resumeCard = nullptr;  ///< null when the card is not on screen
    ResumeKey resumeShown;            ///< what the card on screen asks for
    GtkWidget* resumeThumb = nullptr;
    GtkWidget* resumeMeta = nullptr;
    std::map<ResumeKey, ResumePage> resumes;  ///< what was counted and drawn; the pixbufs are owned
    std::set<ResumeKey> resumePending;
    std::function<std::string(const ResumePage&)> resumeText;  ///< the line of the card on screen

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
