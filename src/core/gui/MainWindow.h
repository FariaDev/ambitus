/*
 * Xournal++
 *
 * The Main window
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>    // for array
#include <atomic>   // for atomic_bool
#include <cstddef>  // for size_t
#include <functional>
#include <memory>   // for unique_ptr
#include <string>   // for string
#include <vector>   // for vector

#include <gdk/gdk.h>      // for GdkDragContext, GdkEvent
#include <glib-object.h>  // for GClosure
#include <glib.h>         // for gpointer, gboolean, gint
#include <gtk/gtk.h>      // for GtkWidget, GtkCheckMenu...

#include "util/Point.h"
#include "util/raii/GObjectSPtr.h"

#include "GladeGui.h"            // for GladeGui
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIONS_LEN
#include "filesystem.h"          // for path

class Control;
class Layout;
class SpinPageAdapter;
class ScrollHandling;
class ToolMenuHandler;
class ToolbarData;
class ToolbarModel;
class XournalView;
class PdfFloatingToolbox;
class FloatingToolbox;
class GladeSearchpath;

class Menubar;
namespace ambitus {
class PageGrowHandle;
class TitleBar;
class StudioPalette;
class PageMenu;
class RadialMenu;
class SelectionBar;
class BoardNavigator;
class Toast;
class CommandPalette;
class NewNotebookDialog;
class VersionsPanel;
enum class EditorMode;
}
namespace ambitus::library {
class LibraryView;
}

typedef std::array<xoj::util::WidgetSPtr, TOOLBAR_DEFINITIONS_LEN> ToolbarWidgetArray;

class MainWindow: public GladeGui {
public:
    MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent);
    ~MainWindow() override;

    void populate(GladeSearchpath* gladeSearchPath);

public:
    GMenuModel* getMenuModel() const;

    void show(GtkWindow* parent) override;

    void toolbarSelected(const std::string& id);
    void toolbarSelected(ToolbarData* d);
    ToolbarData* getSelectedToolbar() const;

private:
    const ToolbarData* clearToolbar();
    void loadToolbar(ToolbarData* d);
    /// Ambitus: the Studio shows the bottom bar alone, so the zoom buttons of the preset (in the top bar) go there
    void updateStudioZoom();

public:
    /**
     * reloadToolbars reloads the currently selected toolbar
     *
     * This is especially useful when a change in the SettingsDialog should be reflected right away
     */
    void reloadToolbars();

    void updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage);

    void setMaximized(bool maximized);
    bool isMaximized() const;

    void setFullscreen(bool enabled) const;

    bool isDarkTheme() const;

    XournalView* getXournal() const;

    void setMenubarVisible(bool visible);
    void setSidebarVisible(bool visible);
    void setToolbarVisible(bool visible);

    Control* getControl() const;

    PdfFloatingToolbox* getPdfToolbox() const;
    FloatingToolbox* getFloatingToolbox() const;

    void updateScrollbarSidebarPosition();

    void setUndoDescription(const std::string& description);
    void setRedoDescription(const std::string& description);

    ToolbarModel* getToolbarModel() const;
    ToolMenuHandler* getToolMenuHandler() const;

    void setDynamicallyGeneratedSubmenuDisabled(bool disabled);

    void updateToolbarMenu();
    void updateColorscheme();

    /// Ambitus: the notebook library and the editor share the window
    void showLibrary();
    /// Ambitus: a short notice over the page that goes away by itself (see ambitus::Toast)
    void showToast(const std::string& text, bool busy = false);
    /// Ambitus: a card with an action ("Undo") and the seconds left to use it
    void showToastAction(const std::string& text, const std::string& actionLabel, std::function<void()> callback);
    /// Ambitus: an error card that stays until closed; the technical detail goes to its "Copy the detail" button
    void showToastError(const std::string& text, const std::string& detail);
    /// Ambitus: the command palette (see ambitus::CommandPalette)
    void openCommandPalette();
    /// Ambitus: the "New notebook" card (see ambitus::NewNotebookDialog). `folder` is where it starts, when the caller
    /// knows (the folder the library shows); else the folder the library shows, else the one of the open notebook.
    void showNewNotebook(const fs::path& folder = {});
    /// Ambitus: the settings card (see ambitus::SettingsCard), in place of the Preferences of Xournal++
    void showSettingsCard();
    /// Ambitus: opens the panel "All tools" of the Studio palette (the "Customize…" of the settings card); the Studio
    /// mode comes on if it was not, and the library goes away if it was shown
    void openPalettePanel();
    /// Ambitus: puts a folder in the library, as "Add folder" does; `replacing` (a folder added before) goes out
    bool setLibraryFolder(const fs::path& folder, const fs::path& replacing = {});
    /// Ambitus: the overlay over the whole window, for the cards that go over the library and the page alike
    GtkWidget* getWindowOverlay() const { return windowOverlay; }
    /// Ambitus: File → Earlier versions opens the panel of versions at the right of the page, or closes it
    void toggleVersions();
    /// Ambitus: the notebook was put back to a version (out of the preview, the list follows)
    void versionsRestored();
    void showEditor();
    bool isLibraryShown() const;
    /// Ambitus: Classic (all the bars), Studio (floating palette) or Focus (only the page)
    void setEditorMode(ambitus::EditorMode mode);
    void applyEditorMode();
    /// Ambitus: the selection appeared, changed or went away (the bar over it follows)
    void selectionChanged();
    /// Ambitus: the board was edited (the minimap follows)
    void boardContentChanged();
    /// Ambitus: the open documents changed (opened, closed, saved, edited)
    void tabsChanged();

    const ToolbarWidgetArray& getToolbarWidgets() const;
    const char* getToolbarName(GtkToolbar* toolbar) const;

    Layout* getLayout() const;

    [[maybe_unused]] Menubar* getMenubar() const;

    /**
     * Disable kinetic scrolling if there is a touchscreen device that was manually mapped to another enabled input
     * device class. This is required so the GtkScrolledWindow does not swallow all the events.
     */
    void setGtkTouchscreenScrollingForDeviceMapping();
    void setGtkTouchscreenScrollingEnabled(bool enabled);

    /// Infer the window's DPI from available monitor info and use it to set the default zoom value.
    void setDPI() const;

private:
    void initXournalWidget();

    void createToolbar();

    /**
     * Update the position of the separator in the paned container, adjusting it to the saved sidebar width.
     * @param contentWidth should be the width of the paned container. The caller should retrieve the width
     * of the container before any modifications to it, as that will reset its allocation.
     */
    void updatePanedPosition(int contentWidth);

    /**
     * Window close Button is pressed
     */
    static bool deleteEventCallback(GtkWidget* widget, GdkEvent* event, Control* control);

    /**
     * Window is maximized/minimized
     */
    static void windowMaximizedCallback(GObject* window, GParamSpec*, MainWindow* win);

    /**
     * Callback for drag & drop files
     */
    static void dragDataRecived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y, GtkSelectionData* data,
                                guint info, guint time, MainWindow* win);

    /**
     * Load Overall CSS file with custom icons, other styling and potentially, user changes
     */
    static void loadMainCSS(GladeSearchpath* gladeSearchPath, const gchar* cssFilename);

private:
    Control* control;

    std::unique_ptr<XournalView> xournal;
    GtkWidget* winXournal = nullptr;
    std::unique_ptr<ScrollHandling> scrollHandling;

    std::atomic_bool gtkTouchscreenScrollingEnabled{true};

    std::unique_ptr<PdfFloatingToolbox> pdfFloatingToolBox;
    std::unique_ptr<FloatingToolbox> floatingToolbox;

    // Toolbars
    std::unique_ptr<ToolMenuHandler> toolbar;
    ToolbarData* selectedToolbar = nullptr;
    /// Ambitus: the zoom buttons put in the bottom bar for the Studio (empty in the other modes)
    std::vector<GtkToolItem*> studioZoomItems;

    std::unique_ptr<Menubar> menubar;

    bool maximized = false;
    bool darkMode = false;
    bool modifiedGtkSettingsTheme = false;

    ToolbarWidgetArray toolbarWidgets;

    bool sidebarVisible = true;

    /// The last monitor the window has been moved to -- used for setting dpi
    GdkMonitor* lastMonitor = nullptr;

    xoj::util::WidgetSPtr boxContainerWidget;
    xoj::util::WidgetSPtr panedContainerWidget;
    xoj::util::WidgetSPtr mainContentWidget;
    xoj::util::WidgetSPtr sidebarWidget;

    GtkWidget* viewStack = nullptr;
    /// Ambitus: over the library and the editor alike, for what is shown over the whole window
    GtkWidget* windowOverlay = nullptr;
    std::unique_ptr<ambitus::library::LibraryView> library;
    /// Ambitus: "+2 cm" on the page edges
    std::unique_ptr<ambitus::PageGrowHandle> growHandle;
    /// Ambitus: palette of the Studio mode
    std::unique_ptr<ambitus::StudioPalette> studioPalette;
    /// Ambitus: the menu of the right mouse button (declared before the ring, which points at it)
    std::unique_ptr<ambitus::PageMenu> pageMenu;
    /// Ambitus: ring of tools on the stylus button
    std::unique_ptr<ambitus::RadialMenu> radialMenu;
    /// Ambitus: the bar over the selection
    std::unique_ptr<ambitus::SelectionBar> selectionBar;
    std::unique_ptr<ambitus::Toast> toast;
    std::unique_ptr<ambitus::CommandPalette> commandPalette;
    std::unique_ptr<ambitus::NewNotebookDialog> newNotebookDialog;
    /// Ambitus: the versions of the notebook, at the right of the page, and the preview of one over it
    std::unique_ptr<ambitus::VersionsPanel> versionsPanel;
    /// Ambitus: minimap and zoom of a board
    std::unique_ptr<ambitus::BoardNavigator> boardNavigator;
    /// Ambitus: client side title bar
    std::unique_ptr<ambitus::TitleBar> titleBar;
};
