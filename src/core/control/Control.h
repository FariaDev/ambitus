/*
 * Xournal++
 *
 * The main Control
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>   // for size_t
#include <functional>  // for function
#include <memory>    // for unique_ptr
#include <optional>  // for optional
#include <string>    // for string, allocator
#include <vector>    // for vector

#include <gdk-pixbuf/gdk-pixbuf.h>  // for GdkPixbuf
#include <gio/gio.h>                // for GApplication
#include <glib.h>                   // for guint
#include <gtk/gtk.h>                // for GtkLabel

#include "ambitus/ClosedTabs.h"                     // for ClosedTabs
#include "ambitus/RestoreTabs.h"                    // for restoretabs::SavedTab
#include "ambitus/Tabs.h"                           // for Tab, TabInfo
#include "control/ToolEnums.h"                      // for ToolSize, ToolType
#include "control/jobs/ProgressListener.h"          // for ProgressListener
#include "control/settings/ViewModes.h"             // for ViewModeId
#include "control/tools/EditSelection.h"            // for OrderChange
#include "enums/Action.enum.h"                      // for Action
#include "gui/toolbarMenubar/model/ColorPalette.h"  // for ColorPalette
#include "model/DocumentHandler.h"                  // for DocumentHandler
#include "model/DocumentListener.h"                 // for DocumentListener
#include "model/GeometryTool.h"                     // for GeometryTool
#include "model/PageRef.h"                          // for PageRef
#include "undo/UndoRedoHandler.h"                   // for UndoRedoHandler (ptr only)

#include "ClipboardHandler.h"  // for ClipboardListener
#include "ToolHandler.h"       // for ToolListener
#include "filesystem.h"        // for path

class LoadHandler;
class GeometryToolController;
class AudioController;
class FullscreenHandler;
class Sidebar;
class GladeSearchpath;
class MetadataManager;
class XournalppCursor;
class ToolbarDragDropHandler;
class MetadataEntry;
class MetadataCallbackData;
class PageBackgroundChangeController;
class PageTemplateSettings;
class PageTypeHandler;
class BaseExportJob;
class LayerController;
class PluginController;
class Document;
class EditSelection;
class Element;
class MainWindow;
class ObjectInputStream;
class ScrollHandler;
class SearchBar;
class Settings;
class TextEditor;
class XournalScheduler;
class ZoomControl;
namespace ambitus {
class SummaPoller;
}
class ToolMenuHandler;
class XojFont;
class XojPdfRectangle;
class Callback;
class ActionDatabase;
class NavigationHistory;

class Control:
        public ToolListener,
        public DocumentHandler,
        public UndoRedoListener,
        public ClipboardListener,
        public ProgressListener {
public:
    Control(GApplication* gtkApp, GladeSearchpath* gladeSearchPath);
    Control(Control const&) = delete;
    Control(Control&&) = delete;
    auto operator=(Control const&) -> Control& = delete;
    auto operator=(Control&&) -> Control& = delete;
    ~Control() override;

    void initWindow(MainWindow* win, bool disableAudio);

public:
    /// Ambitus: a new document in a tab of its own (Xournal++ closed the current one)
    void newFile(fs::path filepath = {});
    /// the same, with the size and paper of the first page
    void newFile(fs::path filepath, const std::optional<PageTemplateSettings>& pageTemplate);
    /// Ambitus: a new board, one page that grows (see ambitus/Board.h)
    void newBoard(fs::path filepath = {});
    /// Ambitus: on a board, makes the page bigger when the writing gets near its right or bottom edge
    void growBoardToFit();
    /// Ambitus: the view on a board moved, zoomed or was resized: the page grows ahead of it, after the main loop
    void boardViewChanged();
    /// Ambitus: the open document is a board (cached, updated on load and after every edit)
    bool isBoardOpen() const { return boardOpen; }

    /// Ambitus tabs (code in ambitus/ControlTabs.cpp). One document is in the window; the others wait aside with
    /// their pages and undo history. Opens the file in a tab of its own, or goes to the tab that has it.
    void openInTab(fs::path filepath, int scrollToPage = -1, bool beside = false);
    void activateTab(size_t index);
    /// Asks to save when needed, like closing a file
    void closeTab(size_t index);
    /// Ambitus: closes the tab in the window (Cmd+W on macOS); nothing when the library is shown
    void closeActiveTab();
    std::vector<ambitus::TabInfo> getTabs() const;
    /// Ambitus: a notebook was renamed, its file is at `to` now. The document on screen and the tabs aside that show
    /// it, their autosaves and the page where it was left (the metadata) follow the file.
    void followRenamedFile(const fs::path& from, const fs::path& to);
    /// Ambitus: bring back the notebook closed most recently (Ctrl+Shift+T)
    void reopenClosedTab();
    bool canReopenClosedTab() const;
    /// Ambitus: open the tabs saved from last time; false when there was nothing to open (the library shows)
    bool restoreTabsFromLastTime();

    /// Ambitus: opens the file beside the notebook in the window, which stays where it is (ambitus/SideTab.h)
    void openBeside(fs::path filepath);
    /// Ambitus: shows the tab beside the one in the window; for the one in the window itself, nothing
    void openBesideTab(size_t index);
    /// Ambitus: the side pane closes; its notebook stays a tab
    void closeSideTab();
    /// Ambitus: the tab shown beside the one in the window, NO_TAB when none
    size_t getSideTab() const { return sideTab; }
    /// Ambitus: the notebook beside, for the side pane (its document is owned by its tab)
    ambitus::SideNotebook getSideNotebook() const;
    /// Ambitus: the side pane turned to `page` (kept in memory: the notebook is written with its page when it leaves
    /// the side, see flushSidePage)
    void storeSidePage(int page);
    /// Ambitus: writes the page of the side notebook with the metadata, as the notebook in the window has its page
    void flushSidePage();
    /// Ambitus: a notebook is in the window
    bool hasNotebookInWindow() const { return activeTab != ambitus::NO_TAB; }

    /// Ambitus: the open document is a draft (see ambitus/Drafts.h)
    bool isDraftOpen() const;
    /// Ambitus: called in place of the "save changes?" dialog when a draft closes. The window asks what to do (it may
    /// answer later) and calls the callback with whether the draft may be closed.
    void setDraftCloseHandler(std::function<void(std::function<void(bool)>)> handler);

    /// @brief Shows an open file dialog and opens the selected file after closing the previously opened file
    void askToOpenFile();
    /**
     * @brief Asynchronously opens the provided path, safely closes the current opened document and replaces it with the
     * newly parsed file. Calls callback afterwards, with boolean parameter true on success. Does nothing to this->doc
     * in case of failure at any point.
     */
    void openFile(
            fs::path filepath, std::function<void(bool)> callback = [](bool) {}, int scrollToPage = -1,
            bool forceOpen = false);
    /// Shows an open file dialog and opens the selected file
    void askToAnnotatePdf();

    /**
     * (Potentially asynchronously) Opens the given file without saving any previously opened Document. Calls callback
     * afterwards, with boolean parameter true if success. WARNING: may lead to data loss if the current Document has
     * not been saved yet.
     */
    void openFileWithoutSavingTheCurrentDocument(fs::path filepath, bool attachToDocument, int scrollToPage,
                                                 std::function<void(bool)> callback);

    void print();
    void exportAsPdf();
    void exportAs();
    void quit(bool allowCancel = true);

    /**
     * @brief Asynchronously saves the document and calls callback afterwards with boolean parameter true on success.
     * May ask the user for a place to save if necessary.
     */
    void save(std::function<void(bool)> callback = [](bool) {});
    /**
     * @brief Asks the user for a new location, asynchronously saves the document there and calls callback afterwards.
     */
    void saveAs(std::function<void(bool)> callback = [](bool) {});

    /**
     * Marks the current document as saved if it is currently marked as unsaved.
     */
    void resetSavedStatus();

    /**
     * Close the current document, prompting to save unsaved changes.
     *
     * @param callback Called after trying to close the document, with param true in case of success, false otherwise.
     * @param allowDestroy Whether clicking "Discard" should destroy the current document.
     * @param allowCancel Whether the user should be able to cancel closing the document.
     * @param forceClose Whether to skip the save dialog and unconditionally discard unsaved changes.
     * @return true if the user closed the document, otherwise false.
     */
    void close(std::function<void(bool)> callback, bool allowDestroy = false, bool allowCancel = true,
               bool forceClose = false);

    // Menu edit
    void showSettings();

    // Menu Help
    void showAbout();
    void showGtkDemo();

    /**
     * @brief Update the Cursor and the Toolbar based on the active color
     *
     */
    void toolColorChanged() override;
    /**
     * @brief Change the color of the current selection based on the active Tool
     *
     */
    void changeColorOfSelection() override;
    void toolChanged() override;
    void toolSizeChanged() override;
    void toolFillChanged() override;
    void toolLineStyleChanged() override;

    void selectTool(ToolType type);
    void selectDefaultTool();

    void fontChanged(const XojFont& font);      ///< Set the font after the user selected a font

    void updatePageNumbers(size_t page, size_t pdfPage);

    /**
     * Save current state (selected tool etc.)
     */
    void saveSettings();

    void updateWindowTitle();
    void setViewPairedPages(bool enabled);
    void setViewFullscreenMode(bool enabled);
    void setViewPresentationMode(bool enabled);
    void setPairsOffset(int numOffset);
    void setViewColumns(int numColumns);
    void setViewRows(int numRows);
    void setViewLayoutVert(bool vert);
    void setViewLayoutR2L(bool r2l);
    void setViewLayoutB2T(bool b2t);

    void manageToolbars();
    void customizeToolbars();
    void setFullscreen(bool enabled);
    void setShowSidebar(bool enabled);
    void setShowToolbar(bool enabled);
    void setShowMenubar(bool enabled);

    void gotoPage();

    void setToolDrawingType(DrawingType type);

    void paperTemplate();
    void paperFormat();
    /// Ambitus: makes the current page wider/taller by the given amount (in points), with undo.
    /// Pages with a PDF background are left as they are.
    void growPage(double dw, double dh);
    void growPage(const PageRef& page, double dw, double dh);
    static constexpr double GROW_STEP = 5.0 / 2.54 * 72.0;  ///< 5 cm in points
    void changePageBackgroundColor();
    void updateBackgroundSizeButton();

    /**
     * Loads the view mode (hide/show menu-,tool-&sidebar)
     */
    bool loadViewMode(ViewModeId mode);

    /**
     * @brief Search text on the given page. The matches (if any) are stored in the XojPageView::SearchControl instance.
     * @param occurrences If not nullptr, the pointed variable will contain the number of matches on the page
     * @param matchRect If not nullptr, will contain the topleft point of the first match on the page
     *                          (Used for scrolling to the first match)
     * @return true if at least one match was found
     */
    bool searchTextOnPage(const std::string& text, size_t pageNumber, size_t index, size_t* occurrences,
                          XojPdfRectangle* matchRect);

    /**
     * Fire page selected, but first check if the page Number is valid
     *
     * @return the page ID or size_t_npos if the page is not found
     */
    size_t firePageSelected(const PageRef& page);
    void firePageSelected(size_t page);

    void addDefaultPage(const std::optional<PageTemplateSettings>& pageTemplate, Document* doc = nullptr);
    void duplicatePage();
    void insertNewPage(size_t position, bool automatedInsertion = false);
    void appendNewPdfPages();
    void insertPage(const PageRef& page, size_t position, bool shouldScrollToPage = true);
    void deletePage();
    void movePageTowardsBeginning();
    void movePageTowardsEnd();

    /**
     * Ask the user whether a page with the given id
     * should be added to the document.
     */
    void askInsertPdfPage(size_t pdfPage);

    /**
     * Disable / enable page action buttons
     */
    void updatePageActions();

    /**
     * Get the navigation history handler.
     */
    NavigationHistory* getNavigationHistory() const;

    // selection handling
    void clearSelection();

    void moveSelectionToLayer(size_t layerNo);

    void setCopyCutEnabled(bool enabled);

    void enableAutosave(bool enable);

    void clearSelectionEndText();

    void selectAllOnPage();

    void reorderSelection(EditSelection::OrderChange change);

    void setToolSize(ToolSize size);

    /**
     * Change the line style of the PEN if select, or of selected elements if any
     * Otherwise, select the PEN tool and set its linestyle
     */
    void setLineStyle(const std::string& style);

    /**
     * Change the eraser type. If the eraser is not selected, select it as well.
     */
    void setEraserType(EraserType type);

    void setFill(bool fill);

    TextEditor* getTextEditor();

    GladeSearchpath* getGladeSearchPath() const;

    void disableSidebarTmp(bool disabled);

    XournalScheduler* getScheduler() const;

    void block(const std::string& name);
    void unblock();

    void setLastAutosaveFile(fs::path newAutosaveFile);
    void deleteLastAutosaveFile();
    void setClipboardHandlerSelection(EditSelection* selection);

    void addChangedDocumentListener(DocumentListener* dl);
    void removeChangedDocumentListener(DocumentListener* dl);

    MetadataManager* getMetadataManager() const;
    Settings* getSettings() const;
    ToolHandler* getToolHandler() const;
    ZoomControl* getZoomControl() const;
    Document* getDocument() const;
    UndoRedoHandler* getUndoRedoHandler() const;
    MainWindow* getWindow() const;
    GtkWindow* getGtkWindow() const;
    ScrollHandler* getScrollHandler() const;
    PageRef getCurrentPage();
    size_t getCurrentPageNo() const;
    XournalppCursor* getCursor() const;
    Sidebar* getSidebar() const;
    SearchBar* getSearchBar() const;
    AudioController* getAudioController() const;
    PageTypeHandler* getPageTypes() const;
    PageBackgroundChangeController* getPageBackgroundChangeController() const;
    LayerController* getLayerController() const;
    PluginController* getPluginController() const;
    const Palette& getPalette() const;

    /**
     * Show floating toolbox at specified coordinates
     * @param x x coordinate relative to main window
     * @param y y coordinate relative to main window
     */
    void showFloatingToolbox(int x, int y);

    bool copy();
    bool cut();
    bool paste();

    void help();

    void selectAlpha(OpacityFeature feature);

    /**
     * @brief Initialize the all button tools based on the respective ButtonConfigs
     *
     */
    void initButtonTool();


public:
    // UndoRedoListener interface
    void undoRedoChanged() override;
    void undoRedoPageChanged(PageRef page) override;

public:
    // ProgressListener interface
    void setMaximumState(size_t max) override;
    void setCurrentState(size_t state) override;

public:
    // ClipboardListener interface
    void clipboardCutCopyEnabled(bool enabled) override;
    void clipboardPasteEnabled(bool enabled) override;
    void clipboardPasteText(std::string text) override;
    void clipboardPasteImage(GdkPixbuf* img) override;
    void clipboardPasteXournal(ObjectInputStream& in) override;
    void deleteSelection() override;

    void clipboardPaste(ElementPtr e);

public:
    void registerPluginToolButtons(ToolMenuHandler* toolMenuHandler);
    inline ActionDatabase* getActionDatabase() const { return actionDB.get(); }
    void loadPaletteFromSettings();

protected:
    void setRotationSnapping(bool enable);
    void setGridSnapping(bool enable);

    void showFontDialog();
    void showColorChooserDialog();

    void fileLoaded(int scrollToPage = -1);

    void eraserSizeChanged();
    void penSizeChanged();
    void highlighterSizeChanged();

    static bool checkChangedDocument(Control* control);
    static bool autosaveCallback(Control* control);

    /**
     * Load metadata later, md will be deleted
     */
    void loadMetadata(MetadataEntry md);

    static bool loadMetadataCallback(MetadataCallbackData* data);

    void saveImpl(bool saveAs, std::function<void(bool)> callback);

private:
    /**
     * @brief Creates the specified geometric tool if it's not on the current page yet. Deletes it if it already exists.
     * @return true if a geometric tool was created
     */
    template <class ToolClass, class ViewClass, class ControllerClass, class InputHandlerClass,
              GeometryToolType toolType>
    bool toggleGeometryTool();
    void resetGeometryTool();

    /**
     * @brief Creates a compass if it's not on the current page yet. Deletes it if it already exists.
     * @return true if a compass was created
     */
    bool toggleCompass();
    /**
     * @brief Creates a setsquare if it's not on the current page yet. Deletes it if it already exists.
     * @return true if a setsquare was created
     */
    bool toggleSetsquare();

    struct MissingPdfData;
    /**
     * Prompt the user that the PDF background is missing and offer solution options
     */
    void promptMissingPdf(MissingPdfData& missingPdf, const fs::path& filepath);

    /**
     * Handle the response from the missing PDF dialog
     */
    void missingPdfDialogResponseHandler(fs::path proposedPdfFilepath, int responseId);

    /**
     * "Closes" the document, preparing the editor for a new document.
     */
    void closeDocument();

    /**
     * Forcibly replaces the opened document.
     * WARNING: Be sure the active document has been saved (or discarded) before calling replaceDocument()
     */
    void replaceDocument(std::unique_ptr<Document> doc, int scrollToPage);

    /**
     * Asynchronously opens the provided path and parse it as a .xopp or .xoj file. Then forcibly replaces the currently
     * opened document with the new one. Asks the user in case of doubts (e.g. wrong file version) and aborts if the
     * document was not correctly and entirely parsed. Calls callback afterwards, with boolean parameter true on
     * success. WARNING: Be sure the active document has been saved (or discarded) before calling openXoppFile()
     */
    void openXoppFile(fs::path filepath, int scrollToPage, std::function<void(bool)> callback);

    /**
     * Opens the provided path and parse it as a PDF file. Then forcibly replaces the currently opened document with a
     * new one based on the PDF. WARNING: Be sure the active document has been saved (or discarded) before calling
     * openPdfFile()
     *
     * @return true on success
     */
    bool openPdfFile(fs::path filepath, bool attachToDocument, int scrollToPage);

    /**
     * Opens the provided path and parse it as a PNG file. Then forcibly replaces the currently opened document with a
     * new one based on the PNG. WARNING: Be sure the active document has been saved (or discarded) before calling
     * openPngFile()
     *
     * @return true on success
     */
    bool openPngFile(fs::path filepath, bool attachToDocument, int scrollToPage);

    /**
     * Opens the provided path and parse it as a .xopt template  file. Then forcibly replaces the currently opened
     * document with a new one based on the template. WARNING: Be sure the active document has been saved (or discarded)
     * before calling openXoptFile()
     *
     * @return true on success
     */
    bool openXoptFile(fs::path filepath);

    /**
     * @brief Get the pen line style to select in the toolbar
     *
     * @return style to select, empty if no style should be selected (active
     * selection with differing line styles)
     */
    auto getLineStyleToSelect() -> std::optional<std::string> const;

    UndoRedoHandler* undoRedo = nullptr;
    ZoomControl* zoom = nullptr;
    /// Ambitus: what the window does when a draft closes (see setDraftCloseHandler)
    std::function<void(std::function<void(bool)>)> draftCloseHandler;

    Settings* settings = nullptr;
    std::unique_ptr<Palette> palette;
    MainWindow* win = nullptr;

    Document* doc = nullptr;

    Sidebar* sidebar = nullptr;
    SearchBar* searchBar = nullptr;

    ToolHandler* toolHandler;

    ScrollHandler* scrollHandler;

    ToolbarDragDropHandler* dragDropHandler = nullptr;

    GApplication* gtkApp = nullptr;

    /**
     * The cursor handler
     */
    XournalppCursor* cursor;

    /**
     * Timeout id: the timeout watches the changes and actualizes the previews from time to time
     */
    guint changeTimout;

    /**
     * The pages wihch has changed since the last update (for preview update)
     */
    std::vector<PageRef> changedPages;
    bool boardGrowPending = false;
    bool boardViewPending = false;
    /// Ambitus: the grow ahead of the view, run by boardViewChanged after the main loop iteration
    void growBoardAhead();
    bool boardOpen = false;
    std::vector<ambitus::Tab> tabs;
    /// Ambitus: notebooks closed in this session, oldest first to fall off past the capacity
    ambitus::ClosedTabs closedTabs;
    /// Ambitus: answers the "Check" of Summa with the page on screen (ambitus/SummaPoll.h)
    std::unique_ptr<ambitus::SummaPoller> summaPoller;
    size_t activeTab = ambitus::NO_TAB;
    /// the tab shown beside the one in the window (read only, see ambitus/SideTab.h); NO_TAB when none
    size_t sideTab = ambitus::NO_TAB;
    /// the tab next to which the file being opened by openInTab goes
    size_t newTabAfter = ambitus::NO_TAB;
    /// Ambitus tabs: keeps the document in the window aside, in its tab
    void parkActiveTab();
    void keepAutosaveAside(ambitus::Tab& tab);
    /// autosaves the tabs aside changed since their last autosave (the autosave timer)
    void autosaveTabsAside();
    void deleteTabAutosaves();
    unsigned nextTabSerial = 1;
    /// The document in the window gets a tab, if it is a file or has changes and has none yet
    void ensureActiveTab();
    /// Puts the document in the window aside; the next one loaded gets a new tab next to it
    void startNewTab();
    /// Keeps the "reopen closed tab" action in step with the stack of closed notebooks
    void updateReopenAction();
    /// The tabs of this session as paths, pages and which one was active
    std::vector<ambitus::restoretabs::SavedTab> tabsForRestore() const;
    void tabsChanged();
    void setBoardOpen(bool board);  ///< Ambitus: growBoardToFit() already queued

    /**
     * DocumentListener instances that are to be updated by checkChangedDocument.
     */
    std::list<DocumentListener*> changedDocumentListeners;

    /**
     * Our clipboard abstraction
     */
    ClipboardHandler* clipboardHandler = nullptr;

    /**
     * The autosave handler ID
     */
    guint autosaveTimeout = 0;
    fs::path lastAutosaveFilename;

    XournalScheduler* scheduler;

    /**
     * State / Blocking attributes
     */
    GtkWidget* statusbar = nullptr;
    GtkLabel* lbState = nullptr;
    GtkProgressBar* pgState = nullptr;
    size_t maxState = 0;
    bool isBlocking;

    GladeSearchpath* gladeSearchPath;

    MetadataManager* metadata;

    PageTypeHandler* pageTypes;

    std::unique_ptr<PageBackgroundChangeController> pageBackgroundChangeController;

    LayerController* layerController;

    std::unique_ptr<GeometryTool> geometryTool;
    std::unique_ptr<GeometryToolController> geometryToolController;

    std::unique_ptr<NavigationHistory> navHistory;

    /**
     * Manage all Xournal++ plugins
     */
    PluginController* pluginController;

    std::unique_ptr<ActionDatabase> actionDB;
    template <Action a>
    friend struct ActionProperties;

    // Keep after the ActionDatabase so it is destroyed first: ~AudioController refers to the ActionDatabase
    std::unique_ptr<AudioController> audioController;
};
