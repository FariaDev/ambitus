#include "MainWindow.h"

#include <algorithm>
#include <regex>

#include <gdk-pixbuf/gdk-pixbuf.h>  // for gdk_pixbuf_new_fr...
#include <gdk/gdk.h>                // for gdk_screen_get_de...
#include <gio/gio.h>                // for g_cancellable_is_...
#include <gtk/gtkcssprovider.h>     // for gtk_css_provider_...

#include "ambitus/Drafts.h"
#include "ambitus/NewNotebook.h"
#include "ambitus/Rename.h"
#include "ambitus/SettingsRows.h"
#include "ambitus/Versions.h"
#include "control/AudioController.h"                    // for AudioController
#include "control/Control.h"                            // for Control
#include "control/DeviceListHelper.h"                   // for getSourceMapping
#include "control/RecentManager.h"                      // for moveRecentFileFilename
#include "control/ScrollHandler.h"                      // for ScrollHandler
#include "control/actions/ActionDatabase.h"             // for ActionDatabase
#include "control/jobs/XournalScheduler.h"              // for XournalScheduler
#include "control/layer/LayerController.h"              // for LayerController
#include "control/settings/MetadataManager.h"           // for MetadataManager
#include "control/settings/PageTemplateSettings.h"      // for PageTemplateSettings
#include "control/settings/Settings.h"                  // for Settings
#include "control/settings/SettingsEnums.h"             // for SCROLLBAR_HIDE_HO...
#include "control/zoom/ZoomControl.h"                   // for ZoomControl
#include "gui/AmbitusColors.h"                          // for stageColor
#include "gui/FloatingToolbox.h"                        // for FloatingToolbox
#include "gui/GladeGui.h"                               // for GladeGui
#include "gui/IconNameHelper.h"                         // for IconNameHelper
#include "gui/PdfFloatingToolbox.h"                     // for PdfFloatingToolbox
#include "gui/SearchBar.h"                              // for SearchBar
#include "gui/ToolitemDragDrop.h"                       // for ToolitemDragDrop
#include "gui/inputdevices/InputEvents.h"               // for INPUT_DEVICE_TOUC...
#include "gui/menus/menubar/Menubar.h"                  // for Menubar
#include "gui/menus/menubar/ToolbarSelectionSubmenu.h"  // for ToolbarSelectionSubmenu
#include "gui/scroll/ScrollHandling.h"                  // for ScrollHandling
#include "gui/sidebar/Sidebar.h"                        // for Sidebar
#include "gui/toolbarMenubar/AbstractToolItem.h"        // for AbstractToolItem
#include "gui/toolbarMenubar/ToolMenuHandler.h"         // for ToolMenuHandler
#include "gui/toolbarMenubar/model/ToolbarData.h"       // for ToolbarData
#include "gui/toolbarMenubar/model/ToolbarModel.h"      // for ToolbarModel
#include "gui/widgets/SpinPageAdapter.h"                // for SpinPageAdapter
#include "gui/widgets/XournalWidget.h"                  // for gtk_xournal_get_l...
#include "library/LibraryView.h"                        // for LibraryView
#include "library/SummaState.h"                         // for loadSummaState
#include "model/Document.h"                             // for Document
#include "model/XojPage.h"                              // for XojPage
#include "util/GListView.h"                             // for GListView, GListV...
#include "util/GtkUtil.h"                               // for getWidgetDPI
#include "util/PathUtil.h"                              // for getConfigFile
#include "util/StringUtils.h"                           // for char_cast
#include "util/Util.h"                                  // for execInUiThread, npos
#include "util/glib_casts.h"                            // for wrap_for_once_v
#include "util/gtk4_helper.h"                           // for gtk_widget_get_width
#include "util/i18n.h"                                  // for FS, _F
#include "util/raii/CStringWrapper.h"                   // for OwnedCString

#include "BoardNavigator.h"  // for BoardNavigator
#include "CommandPalette.h"
#include "GladeSearchpath.h"  // for GladeSearchpath
#include "KeepDraftCard.h"
#include "NewNotebookDialog.h"
#include "PageGrowHandle.h"  // for PageGrowHandle
#include "PageMenu.h"        // for PageMenu
#include "RadialMenu.h"      // for RadialMenu
#include "RenameCard.h"
#include "SelectionBar.h"  // for SelectionBar
#include "SettingsCard.h"
#include "SetupCard.h"
#include "SidePane.h"
#include "StudioPalette.h"       // for StudioPalette
#include "TitleBar.h"            // for TitleBar
#include "Toast.h"               // for Toast
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIO...
#include "ToolbarPalette.h"      // for paletteFileForToolbar
#include "VersionsPanel.h"
#include "XournalView.h"  // for XournalView
#include "config-dev.h"   // for TOOLBAR_CONFIG
#include "filesystem.h"   // for path, exists

#ifdef __APPLE__
// the following header file contains a definition of struct Point that conflicts with model/Point.h
#define Point Point_CF
#include <CoreFoundation/CoreFoundation.h>
#undef Point
#endif

using std::string;

namespace {
/// Ambitus: the width of the column of the pages beside the Studio, in px
constexpr int STUDIO_PAGES_WIDTH = 120;
}  // namespace

static void themeCallback(GObject*, GParamSpec*, gpointer data) { static_cast<MainWindow*>(data)->updateColorscheme(); }

MainWindow::MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent):
        GladeGui(gladeSearchPath, "main.glade", "mainWindow"),
        control(control),
        toolbar(std::make_unique<ToolMenuHandler>(control, this)),
        menubar(std::make_unique<Menubar>()) {
    gtk_window_set_application(GTK_WINDOW(getWindow()), parent);

    panedContainerWidget.reset(get("panelMainContents"), xoj::util::ref);
    boxContainerWidget.reset(get("mainContentContainer"), xoj::util::ref);
    mainContentWidget.reset(get("boxContents"), xoj::util::ref);
    sidebarWidget.reset(get("sidebar"), xoj::util::ref);

    loadMainCSS(gladeSearchPath, "xournalpp.css");
    loadMainCSS(gladeSearchPath, "ambitus.css");
    // the accent is Summa's (graphite in ambitus.css): violet redefines its colours above the stylesheet
    if (ambitus::library::loadSummaState().accent == "violet") {
        xoj::util::GObjectSPtr<GtkCssProvider> violet(gtk_css_provider_new(), xoj::util::adopt);
        gtk_css_provider_load_from_data(violet.get(),
                                        "@define-color amb_accent #8044ca; @define-color amb_on_accent #ffffff;"
                                        "@define-color amb_dk_accent #8957d0; @define-color amb_dk_on_accent #ffffff;",
                                        -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(violet.get()),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    }

    GtkOverlay* overlay = GTK_OVERLAY(get("mainOverlay"));

    // Ambitus: the library and the editor share the window through a stack
    {
        GtkWidget* editor = GTK_WIDGET(overlay);
        g_object_ref(editor);
        gtk_container_remove(GTK_CONTAINER(this->window), editor);
        viewStack = gtk_stack_new();
        gtk_stack_set_transition_type(GTK_STACK(viewStack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
        gtk_stack_set_transition_duration(GTK_STACK(viewStack), 120);
        gtk_stack_add_named(GTK_STACK(viewStack), editor, "editor");
        g_object_unref(editor);

        ambitus::library::LibraryView::Actions actions;
        actions.open = [control](const fs::path& p) { control->openInTab(p); };
        actions.openAt = [control](const fs::path& p, size_t page) { control->openInTab(p, static_cast<int>(page)); };
        actions.openBeside = [control](const fs::path& p) { control->openBeside(p); };
        actions.hasNotebookInWindow = [control] { return control->hasNotebookInWindow(); };
        actions.searchInk = [control] {
            auto* s = control->getSettings();
            return s->getAmbitusSummaBridge() && s->getAmbitusSearchInk();
        };
        actions.newNotebook = [this](const fs::path& folder) { showNewNotebook(folder); };
        actions.newDraft = [this] { newDraft(); };
        actions.rename = [this](const fs::path& p) { openRenameCard(p); };
        actions.newBoard = [this, control](const fs::path& folder) {
            fs::path file;
            if (!folder.empty()) {
                // boards go in their own section of the folder
                fs::path dir = folder / _("Boards");
                std::error_code ec;
                fs::create_directories(dir, ec);
                if (ec) {
                    dir = folder;
                }
                const std::string base = _("Untitled board");
                file = dir / (base + ".xopp");
                for (int i = 2; fs::exists(file); i++) {
                    file = dir / (base + " " + std::to_string(i) + ".xopp");
                }
            }
            showEditor();
            control->newBoard(file);
        };
        library = std::make_unique<ambitus::library::LibraryView>(
                Util::getConfigFile("library.ini"), Util::getCacheFile("library-index.json"), std::move(actions));
        gtk_stack_add_named(GTK_STACK(viewStack), library->getWidget(), "library");
        windowOverlay = gtk_overlay_new();
        gtk_container_add(GTK_CONTAINER(windowOverlay), viewStack);
        gtk_container_add(GTK_CONTAINER(this->window), windowOverlay);
        gtk_widget_show(viewStack);
        gtk_widget_show(windowOverlay);
        gtk_stack_set_visible_child_name(GTK_STACK(viewStack), "editor");
    }

    this->pdfFloatingToolBox = std::make_unique<PdfFloatingToolbox>(this, overlay);
    this->floatingToolbox = std::make_unique<FloatingToolbox>(this, overlay);

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbarWidgets[i].reset(get(TOOLBAR_DEFINITIONS[i].guiName), xoj::util::ref);
    }

    initXournalWidget();
    this->growHandle = std::make_unique<ambitus::PageGrowHandle>(control, xournal.get(), overlay);
    this->studioPalette = std::make_unique<ambitus::StudioPalette>(control, overlay, winXournal);
    this->radialMenu = std::make_unique<ambitus::RadialMenu>(control, xournal.get(), overlay);
    this->pageMenu = std::make_unique<ambitus::PageMenu>(control, xournal.get(), overlay);
    this->radialMenu->setPageMenu(this->pageMenu.get());
    this->selectionBar = std::make_unique<ambitus::SelectionBar>(control, xournal.get(), overlay);
    this->boardNavigator = std::make_unique<ambitus::BoardNavigator>(control, xournal.get(), overlay);
    // before the notices, so they go over the preview of a version
    this->versionsPanel = std::make_unique<ambitus::VersionsPanel>(control, overlay, winXournal, xournal->getWidget());
    gtk_box_pack_end(GTK_BOX(gtk_widget_get_parent(winXournal)), versionsPanel->getWidget(), false, false, 0);
    // Ambitus: the notebook beside the one in the window, in the other half of the split (starts hidden)
    this->sidePane = std::make_unique<ambitus::SidePane>(control, sideSplit);
    // the palette of the Studio follows the pages on the left while the divider moves and the window is resized
    g_signal_connect(sideSplit, "notify::position",
                     G_CALLBACK(+[](GtkWidget*, GParamSpec*, MainWindow* self) { self->placeStudioPalette(); }), this);
    g_signal_connect_after(sideSplit, "size-allocate",
                           G_CALLBACK(+[](GtkWidget*, GdkRectangle*, MainWindow* self) { self->placeStudioPalette(); }),
                           this);
    this->toast = std::make_unique<ambitus::Toast>(overlay, winXournal);
    ambitus::CommandPalette::Notebooks paletteBooks;
    paletteBooks.all = [this] { return library->allNotebooks(); };
    paletteBooks.recent = [this] { return library->recentNotebooks(); };
    paletteBooks.open = [control](const fs::path& p) { control->openInTab(p); };  // as the library opens one
    paletteBooks.openBeside = [control](const fs::path& p) { control->openBeside(p); };
    this->commandPalette = std::make_unique<ambitus::CommandPalette>(
            overlay, GTK_WINDOW(this->window), [this] { return getMenuModel(); }, xournal->getWidget(),
            std::move(paletteBooks));

    this->newNotebookDialog =
            std::make_unique<ambitus::NewNotebookDialog>(GTK_OVERLAY(windowOverlay), GTK_WINDOW(this->window));
    this->keepDraftCard =
            std::make_unique<ambitus::KeepDraftCard>(GTK_OVERLAY(windowOverlay), GTK_WINDOW(this->window));
    this->renameCard = std::make_unique<ambitus::RenameCard>(GTK_OVERLAY(windowOverlay), GTK_WINDOW(this->window));

    // the first time the program opens: the setup, over the whole window
    ambitus::SetupCard::openIfFirstTime(control, GTK_OVERLAY(windowOverlay), GTK_WINDOW(this->window));

    setSidebarVisible(control->getSettings()->isSidebarVisible());

    // Window handler
    g_signal_connect(this->window, "delete-event", xoj::util::wrap_for_g_callback_v<deleteEventCallback>,
                     this->control);
#if GTK_MAJOR_VERSION == 3
    g_signal_connect(this->window, "notify::is-maximized", xoj::util::wrap_for_g_callback_v<windowMaximizedCallback>,
                     this);
#else
    g_signal_connect(this->window, "notify::maximized", xoj::util::wrap_for_g_callback_v<windowMaximizedCallback>,
                     this);
#endif

    g_signal_connect(
            this->window, "configure-event", G_CALLBACK(+[](GtkWidget* widget, GdkEvent*, gpointer self) -> gboolean {
                auto win = static_cast<MainWindow*>(self);
                GdkWindow* gdkWindow = gtk_widget_get_window(widget);
                GdkDisplay* display = gdkWindow ? gdk_window_get_display(gdkWindow) : nullptr;
                GdkMonitor* monitor = display ? gdk_display_get_monitor_at_window(display, gdkWindow) : nullptr;
                if (monitor && monitor != win->lastMonitor) {
                    win->lastMonitor = monitor;
                    const char* monitorName = gdk_monitor_get_model(monitor);
                    g_debug("Window moved to monitor \"%s\"", monitorName);
                    win->setDPI();
                }
                return false;
            }),
            this);

    // "watch over" all key events
    auto keyPropagate = +[](GtkWidget* w, GdkEvent* e, gpointer) {
        return gtk_window_propagate_key_event(GTK_WINDOW(w), (GdkEventKey*)(e));
    };
    g_signal_connect(this->window, "key-press-event", G_CALLBACK(keyPropagate), nullptr);
    g_signal_connect(this->window, "key-release-event", G_CALLBACK(keyPropagate), nullptr);

    updateScrollbarSidebarPosition();

    gtk_window_set_default_size(GTK_WINDOW(this->window), control->getSettings()->getMainWndWidth(),
                                control->getSettings()->getMainWndHeight());

    if (control->getSettings()->isMainWndMaximized()) {
        gtk_window_maximize(GTK_WINDOW(this->window));
    } else {
        gtk_window_unmaximize(GTK_WINDOW(this->window));
    }

    Util::execInUiThread([=]() {
        // Execute after the window is visible, else the check won't work
        control->setShowMenubar(control->getSettings()->isMenubarVisible());
    });

    // Drag and Drop
    g_signal_connect(this->window, "drag-data-received", G_CALLBACK(dragDataRecived), this);

    gtk_drag_dest_set(this->window, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(this->window);
    gtk_drag_dest_add_image_targets(this->window);
    gtk_drag_dest_add_text_targets(this->window);

    g_signal_connect(gtk_widget_get_settings(this->window), "notify::gtk-theme-name", G_CALLBACK(themeCallback), this);
    g_signal_connect(gtk_widget_get_settings(this->window), "notify::gtk-application-prefer-dark-theme",
                     G_CALLBACK(themeCallback), this);

    updateColorscheme();

    // Ambitus: a draft closes through the window; an empty one goes quietly, the others ask what to do
    control->setDraftCloseHandler([this](std::function<void(bool)> done) { handleDraftClose(std::move(done)); });
    control->getUndoRedoHandler()->addUndoRedoListener(this);

    // Ambitus: Ctrl+Shift+N opens a quick draft, from the library and from the editor alike
    GSimpleAction* newDraftAction = g_simple_action_new("new-draft", nullptr);
    g_signal_connect(newDraftAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, MainWindow* self) { self->newDraft(); }), this);
    g_action_map_add_action(G_ACTION_MAP(this->window), G_ACTION(newDraftAction));
    g_object_unref(newDraftAction);
    const char* newDraftAccels[] = {"<Primary><Shift>N", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(parent), "win.new-draft", newDraftAccels);

#ifdef __APPLE__
    // Ambitus: Cmd+W closes the tab in the window. Only on macOS: Linux keeps Ctrl+W unbound, as it was.
    GSimpleAction* closeActiveTabAction = g_simple_action_new("close-active-tab", nullptr);
    g_signal_connect(closeActiveTabAction, "activate",
                     G_CALLBACK(+[](GSimpleAction*, GVariant*, MainWindow* self) { self->control->closeActiveTab(); }),
                     this);
    g_action_map_add_action(G_ACTION_MAP(this->window), G_ACTION(closeActiveTabAction));
    g_object_unref(closeActiveTabAction);
    const char* closeActiveTabAccels[] = {"<Primary>w", nullptr};
    gtk_application_set_accels_for_action(GTK_APPLICATION(parent), "win.close-active-tab", closeActiveTabAccels);
#endif
}

void MainWindow::populate(GladeSearchpath* gladeSearchPath) {

    toolbar->populate(gladeSearchPath);
    menubar->populate(gladeSearchPath, this);

    // need to create tool buttons registered in plugins, so they can be added to toolbars
    control->registerPluginToolButtons(this->toolbar.get());

    createToolbar();

    setToolbarVisible(control->getSettings()->isToolbarVisible());

    // Ambitus: our own title bar, with the modes and the whole menu under "⋯"
    titleBar = std::make_unique<ambitus::TitleBar>(GTK_WINDOW(this->window), menubar->getModel(),
                                                   [this](ambitus::EditorMode m) { setEditorMode(m); });
    gtk_window_set_titlebar(GTK_WINDOW(this->window), titleBar->getWidget());
    titleBar->setMode(static_cast<ambitus::EditorMode>(control->getSettings()->getEditorMode()));
    titleBar->setTabCallbacks(
            [this](size_t i) {
                if (i < control->getTabs().size()) {
                    control->activateTab(i);
                } else {
                    showEditor();
                }
            },
            [this](size_t i) { control->closeTab(i); });
    titleBar->setBesideCallback([this](size_t i) { control->openBesideTab(i); });
    titleBar->setRenameCallback([this](size_t i) {
        const auto tabs = control->getTabs();
        if (i < tabs.size()) {
            openRenameCard(tabs[i].path);
        }
    });
    tabsChanged();
    titleBar->setEditorShown(!isLibraryShown());
}

void MainWindow::placeStudioPalette() {
    if (!studioPalette) {
        return;
    }
    GtkWidget* editorOverlay = get("mainOverlay");
    int left = 0;
    int top = 0;
    if (!sidePane || !sidePane->isShown() ||
        !gtk_widget_translate_coordinates(winXournal, editorOverlay, 0, 0, &left, &top)) {
        studioPalette->setSpan(0, 0);
        return;
    }
    const int right = gtk_widget_get_allocated_width(editorOverlay) - left - gtk_widget_get_allocated_width(winXournal);
    studioPalette->setSpan(left, std::max(0, right));
}

void MainWindow::tabsChanged() {
    if (sidePane) {
        sidePane->show(control->getSideNotebook());
        placeStudioPalette();
    }
    if (!titleBar) {
        return;
    }
    std::vector<ambitus::TabItem> items;
    bool anyActive = false;
    for (auto& t: control->getTabs()) {
        ambitus::TabItem it;
        if (t.path.empty()) {
            it.title = _("Untitled");
        } else {
            const std::string stem = std::string(char_cast(t.path.stem().u8string()));
            it.title = ambitus::drafts::isDraft(t.path) ? ambitus::drafts::tabLabel(stem) : stem;
        }
        it.color = library ? library->colorOf(t.path) : "";
        it.active = t.active;
        it.beside = t.beside;
        it.modified = t.modified;
        // a draft is named when it is kept (see KeepDraftCard), an untitled notebook has no file yet
        it.renamable = !t.path.empty() && !ambitus::drafts::isDraft(t.path);
        anyActive = anyActive || t.active;
        items.push_back(std::move(it));
    }
    if (!anyActive && !isLibraryShown()) {
        // the blank document of the start has no tab until something is written on it
        ambitus::TabItem it;
        it.title = _("Untitled");
        it.active = true;
        it.closable = false;
        items.push_back(std::move(it));
    }
    titleBar->setTabs(std::move(items));
    if (versionsPanel) {
        versionsPanel->tabsChanged();
    }
    updateDraftPill();
}

void MainWindow::setEditorMode(ambitus::EditorMode mode) {
    control->getSettings()->setEditorMode(static_cast<int>(mode));
    applyEditorMode();
}

void MainWindow::selectionChanged() {
    if (selectionBar) {
        selectionBar->selectionChanged();
    }
}

void MainWindow::applyEditorMode() {
    auto* settings = control->getSettings();
    auto mode = static_cast<ambitus::EditorMode>(settings->getEditorMode());
    // a board has its own layout: the palette above, no modes (the setting is left alone)
    const bool board = control->isBoardOpen() && !isLibraryShown();
    if (board) {
        mode = ambitus::EditorMode::STUDIO;
    }
    bool classic = mode == ambitus::EditorMode::CLASSIC;
    bool focus = mode == ambitus::EditorMode::FOCUS;
    // the column of the pages is the Studio's alone: not in Focus, and not on a board
    const bool studioPages = mode == ambitus::EditorMode::STUDIO && !board && settings->getAmbitusStudioPages();
    // the bars are hidden here without touching the settings, so Classic comes back as it was;
    // the library has no menu bar: the whole menu is under "⋯" in the title bar
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(this->window),
                                            classic && !isLibraryShown() && settings->isMenubarVisible());
    updateStudioZoom();  // before the visibility of the bars, which counts the items of the bottom one
    updateStudioPageItems();
    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        if (i >= TBFloatFirst && i <= TBFloatLast) {
            continue;  // the floating toolbox keeps its own
        }
        GtkWidget* w = this->toolbarWidgets[i].get();
        bool bottom = std::string_view(TOOLBAR_DEFINITIONS[i].guiName).starts_with("tbBottom");
        bool show = (classic || (bottom && !focus && !board)) && settings->isToolbarVisible() &&
                    gtk_toolbar_get_n_items(GTK_TOOLBAR(w)) != 0;
        gtk_widget_set_visible(w, show);
    }
    setStudioPages(studioPages);  // before the sidebar: the column takes its place in the Studio
    setSidebarVisible(classic ? settings->isSidebarVisible() : studioPages);
    if (studioPalette) {
        if (focus) {
            studioPalette->setRevealOnHover();
        } else {
            studioPalette->setVisible(mode == ambitus::EditorMode::STUDIO);
        }
    }
    if (boardNavigator) {
        boardNavigator->setVisible(board);
    }
    selectionChanged();  // the bar over the selection exists in the Studio and Focus modes only
    // the Studio mode keeps a band above the pages for the palette, in the colour of the canvas
    gtk_widget_set_margin_top(winXournal, mode == ambitus::EditorMode::STUDIO ? 60 : 0);
    {
        static GtkCssProvider* band = nullptr;
        GtkWidget* contents = get("boxContents");
        if (!band) {
            band = gtk_css_provider_new();
            gtk_style_context_add_provider(gtk_widget_get_style_context(contents), GTK_STYLE_PROVIDER(band),
                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        }
        auto c = ambitus::stageColor(settings, darkMode);
        if (board) {  // the band is part of the board: the colour of its paper
            control->getDocument()->lock_shared();
            if (auto p = control->getDocument()->getPage(0)) {
                c = p->getBackgroundColor();
            }
            control->getDocument()->unlock_shared();
        }
        char css[96];
        g_snprintf(css, sizeof(css), "box { background-color: #%02x%02x%02x; }", c.red, c.green, c.blue);
        gtk_css_provider_load_from_data(band, css, -1, nullptr);
    }
    if (titleBar) {
        titleBar->setMode(mode);
        titleBar->setModesShown(!board);
    }
}

void MainWindow::boardContentChanged() {
    if (boardNavigator) {
        boardNavigator->contentChanged();
    }
}

GMenuModel* MainWindow::getMenuModel() const { return menubar->getModel(); }

MainWindow::~MainWindow() {
    cancelDraftSave();
    // Ambitus: the window is gone before the Control, and the handler must not call it again
    if (control && control->getUndoRedoHandler()) {
        control->getUndoRedoHandler()->removeUndoRedoListener(this);
    }
}

void MainWindow::showToast(const std::string& text, bool busy) {
    if (toast) {
        toast->show(text, busy);
    }
}

void MainWindow::showToastAction(const std::string& text, const std::string& actionLabel,
                                 std::function<void()> callback) {
    if (toast) {
        toast->showAction(text, actionLabel, std::move(callback));
    }
}

void MainWindow::showToastError(const std::string& text, const std::string& detail) {
    if (toast) {
        toast->showError(text, detail);
    }
}

void MainWindow::toggleVersions() {
    if (versionsPanel) {
        showEditor();
        versionsPanel->toggle();
    }
}

void MainWindow::versionsRestored() {
    if (versionsPanel) {
        versionsPanel->restored();
    }
}

void MainWindow::openCommandPalette(bool beside) {
    if (isLibraryShown()) {
        // the box lives over the page; in the library, Ctrl+K goes on putting the keyboard in its search
        library->focusSearch();
    } else if (commandPalette) {
        commandPalette->open(beside);
    }
}

void MainWindow::showNewNotebook(const fs::path& folder) {
    if (!newNotebookDialog || newNotebookDialog->isOpen()) {
        return;
    }
    fs::path near = folder;
    if (near.empty() && isLibraryShown()) {
        near = library->currentFolder();
    }
    if (near.empty()) {
        // the subject of the notebook that is open
        Document* doc = control->getDocument();
        doc->lock_shared();
        near = doc->getFilepath();
        doc->unlock_shared();
    }
    newNotebookDialog->setStartPaper(ambitus::settingsrows::paperFromId(control->getSettings()->getAmbitusNewPaper())
                                             .value_or(ambitus::newnotebook::DEFAULT_PAPER));
    newNotebookDialog->setStartSize(ambitus::settingsrows::sizeFromId(control->getSettings()->getAmbitusNewSize())
                                            .value_or(ambitus::newnotebook::DEFAULT_SIZE));
    newNotebookDialog->open(library->subjects(), near, [this](const ambitus::NewNotebookDialog::Request& r) {
        using namespace ambitus::newnotebook;
        if (!r.file.empty()) {
            std::error_code ec;
            fs::create_directories(r.file.parent_path(), ec);  // the "Boards" folder of a subject, the first time
        }
        showEditor();
        if (r.size == PageSize::INFINITE) {
            control->newBoard(r.file);  // the board that "New board" makes
        } else {
            PageTemplateSettings page = control->getSettings()->getPageTemplateSettings();
            const Dimensions d = pageDimensions(r.size);
            page.setPageWidth(d.width);
            page.setPageHeight(d.height);
            page.setBackgroundType(paperType(r.paper));
            page.setBackgroundColor(Colors::white);
            control->newFile(r.file, page);
            setEditorMode(ambitus::EditorMode::STUDIO);
        }
        if (!r.file.empty()) {
            control->save([](bool) {});  // in the folder of the subject: the first version is kept from the start
        }
    });
}

void MainWindow::newDraft() {
    if (!newNotebookDialog || newNotebookDialog->isOpen()) {
        return;
    }
    auto* settings = control->getSettings();
    newNotebookDialog->setStartDraftPaper(ambitus::settingsrows::paperFromId(settings->getAmbitusDraftPaper())
                                                  .value_or(ambitus::newnotebook::DEFAULT_PAPER));
    newNotebookDialog->setStartDraftSize(ambitus::settingsrows::draftSizeFromId(settings->getAmbitusDraftSize())
                                                 .value_or(ambitus::newnotebook::DEFAULT_SIZE));
    newNotebookDialog->openDraft([this](const ambitus::NewNotebookDialog::Request& r) {
        // the next draft opens with this choice; the notebook's choice (the settings of the notebook) is not touched
        control->getSettings()->setAmbitusDraftPaper(ambitus::settingsrows::paperId(r.paper));
        control->getSettings()->setAmbitusDraftSize(ambitus::settingsrows::draftSizeId(r.size));
        makeDraft(r.paper, r.size);
    });
}

void MainWindow::makeDraft(ambitus::newnotebook::Paper paper, ambitus::newnotebook::PageSize size) {
    const fs::path dir = ambitus::drafts::folder();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        showToastError(_("Could not create the draft."), ec.message());
        return;
    }

    GDateTime* now = g_date_time_new_now_local();
    std::tm t{};
    t.tm_year = g_date_time_get_year(now) - 1900;
    t.tm_mon = g_date_time_get_month(now) - 1;
    t.tm_mday = g_date_time_get_day_of_month(now);
    t.tm_hour = g_date_time_get_hour(now);
    t.tm_min = g_date_time_get_minute(now);
    g_date_time_unref(now);

    const std::string name = ambitus::drafts::newName(t, [&dir](const std::string& n) {
        std::error_code e;
        return fs::exists(dir / (n + ".xopp"), e);
    });
    const fs::path file = dir / (name + ".xopp");

    showEditor();
    if (size == ambitus::newnotebook::PageSize::INFINITE) {
        control->newBoard(file);  // the board that the Infinite choice of the new notebook makes
    } else {
        control->newFile(file,
                         ambitus::drafts::pageTemplate(control->getSettings()->getPageTemplateSettings(), paper, size));
        setEditorMode(ambitus::EditorMode::STUDIO);
    }
    draftDirty = false;
    draftPendingPath.clear();
    updateDraftStatus();
    control->save([this, file](bool ok) {
        if (!ok) {
            draftDirty = true;
            draftPendingPath = file;
        }
        updateDraftStatus();
        if (ok && library) {
            library->refresh();
        }
    });
    tabsChanged();
}

void MainWindow::appendPageAtEnd() {
    Document* doc = control->getDocument();
    doc->lock_shared();
    const size_t count = doc->getPageCount();
    PageRef last = count > 0 ? doc->getPage(count - 1) : PageRef{};
    doc->unlock_shared();
    if (!last) {
        return;
    }

    // the paper of the last page, as copyBackgroundFromOtherPage copies it (the page type chosen for the new pages is
    // not used here); insertPage keeps the undo and scrolls to the page
    control->clearSelectionEndText();
    auto page = std::make_shared<XojPage>(last->getWidth(), last->getHeight());
    const PageType bg = last->getBackgroundType();
    page->setBackgroundType(bg);
    if (bg.isPdfPage()) {
        page->setBackgroundPdfPageNr(last->getPdfPageNr());
    } else if (bg.isImagePage()) {
        page->setBackgroundImage(last->getBackgroundImage());
    } else {
        page->setBackgroundColor(last->getBackgroundColor());
    }
    control->insertPage(page, count);
}

void MainWindow::initDraftPill() {
    draftPillRevealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(draftPillRevealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);

    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(frame, 32);
    gtk_widget_set_margin_end(frame, 32);
    gtk_widget_set_margin_top(frame, 32);
    gtk_widget_set_margin_bottom(frame, 32);
    gtk_container_add(GTK_CONTAINER(draftPillRevealer), frame);

    draftPill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    // the surface is the Studio palette's; the pill adds the room of 4 px and its own pieces
    gtk_style_context_add_class(gtk_widget_get_style_context(draftPill), "amb-palette");
    gtk_style_context_add_class(gtk_widget_get_style_context(draftPill), "amb-draft-pill");
    gtk_widget_set_valign(draftPill, GTK_ALIGN_CENTER);

    // the label: the icon, "Draft" and the dot of the save state; it tells nothing more than that
    draftInfo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(draftInfo), "amb-draft-info");
    GtkWidget* infoIcon = gtk_image_new_from_icon_name("xopp-file-plus", GTK_ICON_SIZE_MENU);
    gtk_image_set_pixel_size(GTK_IMAGE(infoIcon), 15);
    gtk_box_pack_start(GTK_BOX(draftInfo), infoIcon, false, false, 0);
    gtk_box_pack_start(GTK_BOX(draftInfo), gtk_label_new(_("Draft")), false, false, 0);
    draftDot = gtk_drawing_area_new();
    gtk_widget_set_size_request(draftDot, 6, 6);
    gtk_widget_set_valign(draftDot, GTK_ALIGN_CENTER);
    g_signal_connect(draftDot, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
                         GdkRGBA c;
                         gtk_style_context_get_color(gtk_widget_get_style_context(w), gtk_widget_get_state_flags(w),
                                                     &c);
                         cairo_set_source_rgba(cr, c.red, c.green, c.blue, c.alpha);
                         cairo_arc(cr, 3, 3, 3, 0, 2 * G_PI);
                         cairo_fill(cr);
                         return FALSE;
                     }),
                     nullptr);
    g_signal_connect_swapped(draftDot, "style-updated", G_CALLBACK(gtk_widget_queue_draw), draftDot);
    gtk_box_pack_start(GTK_BOX(draftInfo), draftDot, false, false, 0);
    gtk_box_pack_start(GTK_BOX(draftPill), draftInfo, false, false, 0);

    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(sep), "amb-draft-sep");
    gtk_widget_set_size_request(sep, 1, 20);
    gtk_widget_set_valign(sep, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(draftPill), sep, false, false, 0);

    GtkWidget* discard = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(discard), "amb-draft-ghost");
    GtkWidget* discardRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* trash = gtk_image_new_from_icon_name("xopp-trash", GTK_ICON_SIZE_MENU);
    gtk_image_set_pixel_size(GTK_IMAGE(trash), 15);
    gtk_box_pack_start(GTK_BOX(discardRow), trash, false, false, 0);
    gtk_box_pack_start(GTK_BOX(discardRow), gtk_label_new(_("Discard")), false, false, 0);
    gtk_container_add(GTK_CONTAINER(discard), discardRow);
    g_signal_connect(discard, "clicked", G_CALLBACK(+[](GtkButton*, MainWindow* self) { self->discardActiveDraft(); }),
                     this);
    gtk_box_pack_start(GTK_BOX(draftPill), discard, false, false, 0);

    GtkWidget* keep = gtk_button_new_with_label(_("Keep in a subject…"));
    gtk_style_context_add_class(gtk_widget_get_style_context(keep), "amb-draft-primary");
    g_signal_connect(keep, "clicked", G_CALLBACK(+[](GtkButton*, MainWindow* self) { self->openKeepDraftCard(); }),
                     this);
    gtk_box_pack_start(GTK_BOX(draftPill), keep, false, false, 0);
    gtk_container_add(GTK_CONTAINER(frame), draftPill);
    updateDraftStatus();

    g_signal_connect_swapped(draftPillRevealer, "notify::child-revealed", G_CALLBACK(+[](MainWindow* self) {
                                 auto* r = GTK_REVEALER(self->draftPillRevealer);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     gtk_widget_hide(self->draftPillRevealer);
                                 }
                             }),
                             this);

    gtk_widget_show_all(draftPillRevealer);
    gtk_widget_set_no_show_all(draftPillRevealer, true);
    gtk_widget_hide(draftPillRevealer);
    GtkOverlay* target = GTK_OVERLAY(get("mainOverlay"));
    gtk_overlay_add_overlay(target, draftPillRevealer);
    draftPillPositionId = g_signal_connect(target, "get-child-position", G_CALLBACK(draftPillPosition), this);
}

gboolean MainWindow::draftPillPosition(GtkOverlay*, GtkWidget* w, GdkRectangle* alloc, MainWindow* self) {
    if (w != self->draftPillRevealer || !self->winXournal) {
        return false;
    }
    GtkWidget* host = gtk_widget_get_parent(GTK_WIDGET(w));
    if (!host || !GTK_IS_OVERLAY(host)) {
        return false;
    }
    int x = 0, y = 0;
    const int areaW = gtk_widget_get_allocated_width(self->winXournal);
    const int areaH = gtk_widget_get_allocated_height(self->winXournal);
    if (areaW <= 0 || !gtk_widget_translate_coordinates(self->winXournal, host, 0, 0, &x, &y)) {
        return false;
    }
    GtkRequisition natural;
    gtk_widget_get_preferred_size(w, nullptr, &natural);
    alloc->width = std::min(natural.width, areaW);
    alloc->height = natural.height;
    alloc->x = x + (areaW - alloc->width) / 2;
    alloc->y = y + areaH - alloc->height;
    return true;
}

void MainWindow::updateDraftStatus() {
    if (!draftInfo || !draftDot) {
        return;
    }
    // the dot belongs to the draft on screen: another draft may have a save on the way and still show as saved
    const bool pending =
            draftDirty && !draftPendingPath.empty() && control->isDraftOpen() && activeFilePath() == draftPendingPath;
    GtkStyleContext* ctx = gtk_widget_get_style_context(draftDot);
    if (pending) {
        gtk_style_context_remove_class(ctx, "amb-draft-dot-saved");
        gtk_style_context_add_class(ctx, "amb-draft-dot-pending");
    } else {
        gtk_style_context_remove_class(ctx, "amb-draft-dot-pending");
        gtk_style_context_add_class(ctx, "amb-draft-dot-saved");
    }
    gtk_widget_set_tooltip_text(draftInfo, pending ? _("Saving…") : _("Saved on its own"));
    gtk_widget_queue_draw(draftDot);
}

void MainWindow::updateDraftPill() {
    if (!draftPillRevealer) {
        return;
    }
    updateDraftStatus();
    const bool show = control->isDraftOpen() && !isLibraryShown();
    if (show == draftPillShown) {
        return;
    }
    draftPillShown = show;
    gboolean animations = true;
    g_object_get(gtk_widget_get_settings(draftPillRevealer), "gtk-enable-animations", &animations, nullptr);
    gtk_revealer_set_transition_duration(
            GTK_REVEALER(draftPillRevealer),
            animations ? (show ? ambitus::toast::FADE_IN_MS : ambitus::toast::FADE_OUT_MS) : 0);
    if (show) {
        gtk_widget_show(draftPillRevealer);
    }
    gtk_revealer_set_reveal_child(GTK_REVEALER(draftPillRevealer), show);
    if (!show && !animations) {
        gtk_widget_hide(draftPillRevealer);
    }
}

void MainWindow::saveDraft() {
    draftSaveTimer = 0;
    if (control->isDraftOpen()) {
        const fs::path saving = activeFilePath();
        control->save([this, saving](bool ok) {
            if (ok && saving == draftPendingPath) {
                draftDirty = false;
                draftPendingPath.clear();
            }
            updateDraftStatus();
        });
    }
}

void MainWindow::cancelDraftSave() {
    if (draftSaveTimer) {
        g_source_remove(draftSaveTimer);
        draftSaveTimer = 0;
    }
}

void MainWindow::scheduleDraftSave() {
    if (!control->isDraftOpen()) {
        cancelDraftSave();
        return;
    }
    cancelDraftSave();
    draftSaveTimer = g_timeout_add(
            2 * 1000,
            +[](gpointer data) -> gboolean {
                static_cast<MainWindow*>(data)->saveDraft();
                return G_SOURCE_REMOVE;
            },
            this);
    draftDirty = true;
    draftPendingPath = activeFilePath();
    updateDraftStatus();
}

void MainWindow::undoRedoChanged() { scheduleDraftSave(); }

void MainWindow::undoRedoPageChanged(PageRef page) { (void)page; }

fs::path MainWindow::activeFilePath() const {
    Document* doc = control->getDocument();
    doc->lock_shared();
    fs::path path = doc->getFilepath();
    doc->unlock_shared();
    return path;
}

bool MainWindow::draftHasContent() const {
    Document* doc = control->getDocument();
    doc->lock_shared();
    bool content = false;
    for (size_t i = 0; i < doc->getPageCount() && !content; i++) {
        PageRef page = doc->getPage(i);
        content = page && page->isAnnotated();
    }
    doc->unlock_shared();
    return content;
}

size_t MainWindow::activeTabIndex() const {
    const auto tabs = control->getTabs();
    for (size_t i = 0; i < tabs.size(); i++) {
        if (tabs[i].active) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

void MainWindow::discardActiveDraft() {
    const size_t index = activeTabIndex();
    if (index == static_cast<size_t>(-1)) {
        return;
    }
    draftDiscardOnClose = true;
    control->closeTab(index);
}

void MainWindow::handleDraftClose(std::function<void(bool)> done) {
    cancelDraftSave();
    const fs::path draft = activeFilePath();
    if (!ambitus::drafts::isDraft(draft)) {
        done(false);
        return;
    }
    const bool discardNow = std::exchange(draftDiscardOnClose, false);
    if (discardNow || !draftHasContent()) {
        ambitus::drafts::discard(draft);
        library->refresh();
        done(true);
        return;
    }

    if (!keepDraftCard) {
        done(false);
        return;
    }
    keepDraftCard->openClose(
            [this, draft, done](ambitus::KeepDraftCard::CloseChoice choice) {
                using Choice = ambitus::KeepDraftCard::CloseChoice;
                if (choice == Choice::DISCARD) {
                    ambitus::drafts::discard(draft);
                    library->refresh();
                    if (done) {
                        done(true);
                    }
                } else if (choice == Choice::KEEP_IN_DRAFTS) {
                    control->save([this, done](bool saved) {
                        if (saved && library) {
                            library->refresh();
                        }
                        if (done) {
                            done(saved);
                        }
                    });
                } else {
                    openKeepDraftCard(done);
                }
            },
            [done] {
                if (done) {
                    done(false);
                }
            });
}

void MainWindow::openKeepDraftCard(std::function<void(bool)> after) {
    if (!keepDraftCard || keepDraftCard->isOpen() || !control->isDraftOpen()) {
        if (after) {
            after(false);
        }
        return;
    }
    const fs::path draft = activeFilePath();
    auto cancel = after;
    keepDraftCard->open(
            library->subjects(),
            [this, draft, after = std::move(after)](const ambitus::KeepDraftCard::Request& r) {
                // a board goes in the "Boards" folder of the subject, as a new board does
                const fs::path folder =
                        control->isBoardOpen() ? r.file.parent_path() / _("Boards") : r.file.parent_path();
                keepDraft(draft, folder, r.name, std::move(after));
            },
            [cancel = std::move(cancel)] {
                if (cancel) {
                    cancel(false);
                }
            });
}

void MainWindow::keepDraft(const fs::path& draft, const fs::path& folder, const std::string& name,
                           std::function<void(bool)> after) {
    control->save([this, draft, folder, name, after = std::move(after)](bool saved) mutable {
        if (!saved) {
            if (after) {
                after(false);
            }
            return;
        }
        auto target = ambitus::drafts::keep(draft, folder, name);
        if (!target) {
            showToastError(_("Could not keep the draft."), std::string());
            if (after) {
                after(false);
            }
            return;
        }
        ambitus::versions::followMove(ambitus::versions::defaultRoot(), draft, *target);
        Document* doc = control->getDocument();
        doc->lock();
        doc->setFilepath(*target);
        doc->unlock();
        if (library) {
            library->refresh();
        }
        control->updateWindowTitle();
        draftDirty = false;
        draftPendingPath.clear();
        updateDraftPill();
        if (after) {
            after(true);
        }
    });
}

void MainWindow::openRenameCard(const fs::path& file) {
    if (!renameCard || renameCard->isOpen()) {
        return;
    }
    const bool board = library && library->isBoard(file);
    renameCard->open(file, board, [this, file](const std::string& name) { renameNotebook(file, name); });
}

void MainWindow::renameNotebook(const fs::path& file, const std::string& name) {
    const bool board = library && library->isBoard(file);
    saveOpenNotebook(file, [this, file, name, board](bool saved) {
        std::optional<fs::path> to;
        if (saved) {
            to = ambitus::rename::moveNotebook(file, name, ambitus::versions::defaultRoot());
        }
        if (!to) {
            showToastError(board ? _("Could not rename the board.") : _("Could not rename the notebook."),
                           std::string());
            return;
        }
        // the recent entry goes with the file; renamed, it is the newest one, as a notebook that was just worked on
        if (RecentManager::moveRecentFileFilename(file, *to)) {
            RecentManager::addRecentFileFilename(*to);
        }
        if (library) {
            library->notebookMoved(file, *to);
        }
        control->followRenamedFile(file, *to);
    });
}

void MainWindow::saveOpenNotebook(const fs::path& file, std::function<void(bool)> done) {
    const auto tabs = control->getTabs();
    for (size_t i = 0; i < tabs.size(); i++) {
        if (tabs[i].path != file) {
            continue;
        }
        if (!tabs[i].modified) {
            break;
        }
        // a tab put aside is saved where it is shown: it comes to the front first
        control->activateTab(i);
        control->save(std::move(done));
        return;
    }
    done(true);
}

void MainWindow::showSettingsCard() {
    ambitus::SettingsCard::open(control, GTK_OVERLAY(windowOverlay), GTK_WINDOW(window));
}

void MainWindow::openPalettePanel() {
    bool moved = false;
    if (isLibraryShown()) {
        showEditor();
        moved = true;
    }
    if (static_cast<ambitus::EditorMode>(control->getSettings()->getEditorMode()) != ambitus::EditorMode::STUDIO &&
        !control->isBoardOpen()) {
        setEditorMode(ambitus::EditorMode::STUDIO);  // the palette is only there in the Studio mode
        moved = true;
    }
    if (!studioPalette) {
        return;
    }
    if (!moved) {
        studioPalette->openPanel();
        return;
    }
    // the panel is a popover of the palette: it needs the palette on screen, one turn of the main loop later
    g_idle_add(
            +[](gpointer d) -> gboolean {
                static_cast<MainWindow*>(d)->studioPalette->openPanel();
                return G_SOURCE_REMOVE;
            },
            this);
}

bool MainWindow::setLibraryFolder(const fs::path& folder, const fs::path& replacing) {
    return library->setFolder(folder, replacing);
}

void MainWindow::showLibrary() {
    // the page the editor is on stays in memory until the notebook closes; the "Continue" card reads it from disk
    control->getMetadataManager()->documentChanged();
    library->refresh();
    gtk_stack_set_visible_child_name(GTK_STACK(viewStack), "library");
    library->focusSearch();
    tabsChanged();
    if (titleBar) {
        titleBar->setEditorShown(false);
        applyEditorMode();
    }
}

void MainWindow::showEditor() {
    if (isLibraryShown()) {
        gtk_stack_set_visible_child_name(GTK_STACK(viewStack), "editor");
    }
    tabsChanged();
    if (titleBar) {
        titleBar->setEditorShown(true);
        applyEditorMode();
    }
}

auto MainWindow::isLibraryShown() const -> bool {
    return viewStack && library && gtk_stack_get_visible_child(GTK_STACK(viewStack)) == library->getWidget();
}

struct ThemeProperties {
    bool dark;
    bool darkSuffix;
    std::string rootname;  ///< Name without any putative -dark suffix
};
static ThemeProperties getThemeProperties(GtkWidget* w) {
    xoj::util::OwnedCString name;
    [[maybe_unused]] bool useEnv = false;
    // Gtk prioritizes GTK_THEME over GtkSettings content
    // cf https://gitlab.gnome.org/GNOME/gtk/blob/90d84a2af8b367bd5a5312b3fa3b67563462c0ef/gtk/gtksettings.c#L1567-L1622
    if (auto* p = g_getenv("GTK_THEME")) {
        *(name.contentReplacer()) = g_strdup(p);
        useEnv = true;
    } else {
        g_object_get(gtk_widget_get_settings(w), "gtk-theme-name", name.contentReplacer(), nullptr);
    }

    // Try to figure out if the theme is dark or light
    // Some themes handle their dark variant via "gtk-application-prefer-dark-theme" while other just append "-dark"
    const std::regex nameparser("([a-zA-Z0-9_\\.-]+?)([:-][dD]ark)?");
    std::cmatch sm;
    std::regex_match(name.get(), sm, nameparser);

    ThemeProperties props;
    if (sm.size() < 3) {
        g_warning("Fails to extract theme root name from: \"%s\"", name.get());
        props.rootname = name.get();
        props.darkSuffix = false;
    } else {
        props.rootname = sm[1];
        props.darkSuffix = sm[2].length() > 0;
    }
    gboolean dark = false;

#ifdef __APPLE__
    if (!useEnv) {
        CFStringRef interfaceStyle =
                (CFStringRef)CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"), kCFPreferencesCurrentApplication);
        if (interfaceStyle) {
            char buffer[128];
            if (CFStringGetCString(interfaceStyle, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                std::string style = buffer;
                if (auto pos = style.find("Dark"); pos != std::string::npos) {
                    dark = true;
                }
            }
        }
    }
#else
    g_object_get(gtk_widget_get_settings(w), "gtk-application-prefer-dark-theme", &dark, nullptr);
#endif

    g_debug("Extracted theme info: Name = %s, rootname = %s, dark = %s", name.get(), props.rootname.c_str(),
            dark ? "true" : "false");

    props.dark = props.darkSuffix || dark;  // Some themes handle their dark variant via this setting

    return props;
}

void MainWindow::updateColorscheme() {
    g_signal_handlers_block_by_func(gtk_widget_get_settings(this->window),
                                    reinterpret_cast<gpointer>(G_CALLBACK(themeCallback)), this);
    auto variant = control->getSettings()->getThemeVariant();
    if (variant == THEME_VARIANT_USE_SYSTEM) {
        gtk_settings_reset_property(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme");
        if (modifiedGtkSettingsTheme) {
            // Some bug in Gtk makes an infinite loop despite us blocking the signals
            gtk_settings_reset_property(gtk_widget_get_settings(this->window), "gtk-theme-name");
            modifiedGtkSettingsTheme = false;
        }
    }
    auto props = getThemeProperties(this->window);

    this->darkMode = (props.dark && variant != THEME_VARIANT_FORCE_LIGHT) || variant == THEME_VARIANT_FORCE_DARK;

    // Ambitus: the outline of the colour swatches is the border of the theme, which the theme change replaces
    this->toolbar->updateColorToolItemOutlines(ambitus::borderColor(this->darkMode));

    // Set up icons
    {
        const auto uiPath = this->getGladeSearchPath()->getFirstSearchPath();
        const auto lightColorIcons = (uiPath / "iconsColor-light");
        const auto darkColorIcons = (uiPath / "iconsColor-dark");
        const auto lightLucideIcons = (uiPath / "iconsLucide-light");
        const auto darkLucideIcons = (uiPath / "iconsLucide-dark");

        // icon load order from lowest priority to highest priority
        std::vector<fs::path> iconLoadOrder = {};
        const auto chosenTheme = control->getSettings()->getIconTheme();
        switch (chosenTheme) {
            case ICON_THEME_COLOR:
                iconLoadOrder = {darkLucideIcons, lightLucideIcons, darkColorIcons, lightColorIcons};
                break;
            case ICON_THEME_LUCIDE:
                iconLoadOrder = {darkColorIcons, lightColorIcons, darkLucideIcons, lightLucideIcons};
                break;
            default:
                g_message("Unknown icon theme!");
        }

        if (this->darkMode) {
            for (size_t i = 0; 2 * i + 1 < iconLoadOrder.size(); ++i) {
                std::swap(iconLoadOrder[2 * i], iconLoadOrder[2 * i + 1]);
            }
        }

        for (auto& p: iconLoadOrder) {
            gtk_icon_theme_prepend_search_path(gtk_icon_theme_get_default(), char_cast(p.u8string().c_str()));
        }
    }

    GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(this->window));

    if (this->darkMode) {
        gtk_style_context_add_class(context, "darkMode");
        g_object_set(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", true, nullptr);
    } else {
        gtk_style_context_remove_class(context, "darkMode");
        g_object_set(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", false, nullptr);
        if (props.darkSuffix) {  // The active theme is all dark. Remove the trailing "-dark"
            g_object_set(gtk_widget_get_settings(this->window), "gtk-theme-name", props.rootname.c_str(), nullptr);
            modifiedGtkSettingsTheme = true;
        }
    }

    {
        gchar* name = nullptr;
        g_object_get(gtk_widget_get_settings(this->window), "gtk-theme-name", &name, nullptr);
        g_debug("Theme name: %s", name);
        g_debug("Modified in GtkSettings: %s", modifiedGtkSettingsTheme ? "true" : "false");
        g_free(name);
        gboolean gtkdark = true;
        g_object_get(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", &gtkdark, nullptr);
        g_debug("Theme variant: %s", gtkdark ? "dark" : "light");
        g_debug("Icon theme: %s", iconThemeToString(control->getSettings()->getIconTheme()));
    }
    g_signal_handlers_unblock_by_func(gtk_widget_get_settings(this->window), reinterpret_cast<gpointer>(themeCallback),
                                      this);
}

void MainWindow::initXournalWidget() {
    winXournal = gtk_scrolled_window_new();

    setGtkTouchscreenScrollingForDeviceMapping();

    GtkWidget* content = get("boxContents");
    initDraftPill();

    // Ambitus: the pages, and at their right the panel of versions (added once the window is built)
    GtkWidget* pageRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(winXournal, true);
    gtk_widget_set_vexpand(winXournal, true);
    gtk_box_pack_start(GTK_BOX(pageRow), winXournal, true, true, 0);
    gtk_widget_show(pageRow);
    // Ambitus: the pages on the left, and on the right the notebook beside them, read only (SidePane)
    sideSplit = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(sideSplit), "amb-split");
    gtk_paned_pack1(GTK_PANED(sideSplit), pageRow, true, false);
    gtk_box_pack_start(GTK_BOX(content), sideSplit, true, true, 0);
    gtk_widget_show(sideSplit);

    scrollHandling = std::make_unique<ScrollHandling>(GTK_SCROLLED_WINDOW(winXournal));

    this->xournal = std::make_unique<XournalView>(winXournal, control, scrollHandling.get());

    control->getZoomControl()->initZoomHandler(this->window, winXournal, xournal.get(), control);
    gtk_widget_show_all(winXournal);
}

void MainWindow::setGtkTouchscreenScrollingForDeviceMapping() {
    InputDeviceClass touchscreenClass =
            DeviceListHelper::getSourceMapping(GDK_SOURCE_TOUCHSCREEN, this->getControl()->getSettings());

    setGtkTouchscreenScrollingEnabled(touchscreenClass == INPUT_DEVICE_TOUCHSCREEN &&
                                      !control->getSettings()->getTouchDrawingEnabled());
}

void MainWindow::setGtkTouchscreenScrollingEnabled(bool enabled) {
    if (!control->getSettings()->getGtkTouchInertialScrollingEnabled()) {
        enabled = false;
    }
    gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(winXournal), enabled);
}

auto MainWindow::getLayout() const -> Layout* { return this->xournal->getLayout(); }

auto cancellable_cancel(GCancellable* cancel) -> bool {
    g_cancellable_cancel(cancel);

    g_warning("Timeout... Cancel loading URL");

    return false;
}

void MainWindow::dragDataRecived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y, GtkSelectionData* data,
                                 guint info, guint time, MainWindow* win) {
    GtkWidget* source = gtk_drag_get_source_widget(dragContext);
    if (source && widget == gtk_widget_get_toplevel(source)) {
        gtk_drag_finish(dragContext, false, false, time);
        return;
    }

    guchar* text = gtk_selection_data_get_text(data);
    if (text) {
        win->control->clipboardPasteText(reinterpret_cast<const char*>(text));

        g_free(text);
        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    xoj::util::GObjectSPtr<GdkPixbuf> image(gtk_selection_data_get_pixbuf(data), xoj::util::adopt);
    if (image) {
        win->control->clipboardPasteImage(image.get());

        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    gchar** uris = gtk_selection_data_get_uris(data);
    if (uris) {
        for (int i = 0; uris[i] != nullptr && i < 3; i++) {
            const char* uri = uris[i];

            GCancellable* cancel = g_cancellable_new();
            auto cancelTimeout = g_timeout_add(3000, xoj::util::wrap_for_once_v<cancellable_cancel>, cancel);

            xoj::util::GObjectSPtr<GFile> file(g_file_new_for_uri(uri), xoj::util::adopt);
            GError* err = nullptr;
            GFileInputStream* in = g_file_read(file.get(), cancel, &err);
            if (g_cancellable_is_cancelled(cancel)) {
                continue;
            }

            if (err == nullptr) {
                xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(
                        gdk_pixbuf_new_from_stream(G_INPUT_STREAM(in), cancel, nullptr), xoj::util::adopt);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }
                g_input_stream_close(G_INPUT_STREAM(in), cancel, nullptr);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }

                if (pixbuf) {
                    win->control->clipboardPasteImage(pixbuf.get());
                }
            } else {
                g_error_free(err);
            }

            if (!g_cancellable_is_cancelled(cancel)) {
                g_source_remove(cancelTimeout);
            }
            g_object_unref(cancel);
        }

        gtk_drag_finish(dragContext, true, false, time);

        g_strfreev(uris);
    }

    gtk_drag_finish(dragContext, false, false, time);
}

auto MainWindow::getControl() const -> Control* { return control; }

void MainWindow::updateScrollbarSidebarPosition() {
    // Part 1: update scrollbar position
    if (winXournal != nullptr) {
        GtkScrolledWindow* scrolledWindow = GTK_SCROLLED_WINDOW(winXournal);

        ScrollbarHideType type = this->getControl()->getSettings()->getScrollbarHideType();

        bool scrollbarOnLeft = control->getSettings()->isScrollbarOnLeft();
        if (scrollbarOnLeft) {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_RIGHT);
        } else {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_LEFT);
        }

        gtk_widget_set_visible(gtk_scrolled_window_get_hscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_HORIZONTAL));
        gtk_widget_set_visible(gtk_scrolled_window_get_vscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_VERTICAL));

        gtk_scrolled_window_set_overlay_scrolling(scrolledWindow,
                                                  !control->getSettings()->isScrollbarFadeoutDisabled());
    }

    // Part 2: update sidebar position
    GtkPaned* paned = GTK_PANED(this->panedContainerWidget.get());

    // Allocation is reset when we switch up the contained elements. Fetch the
    // width here in case we need it afterwards.
    int contentWidth = gtk_widget_get_width(this->boxContainerWidget.get());

    bool sidebarRight = sidebarOnRight();
    if (sidebarRight != (gtk_paned_get_child2(paned) == this->sidebarWidget.get())) {
        // switch sidebar and main content
        GtkWidget* sidebar = this->sidebarWidget.get();
        GtkWidget* mainContent = this->sidebarVisible ? this->mainContentWidget.get() : nullptr;
#if GTK_MAJOR_VERSION == 3
        if (this->sidebarVisible) {
            gtk_container_remove(GTK_CONTAINER(paned), sidebar);
            gtk_container_remove(GTK_CONTAINER(paned), mainContent);

            if (sidebarRight) {
                gtk_paned_pack1(paned, mainContent, true, false);
                gtk_paned_pack2(paned, sidebar, false, false);
            } else {
                gtk_paned_pack1(paned, sidebar, false, false);
                gtk_paned_pack2(paned, mainContent, true, false);
            }
        } else {
            // The sidebar is hidden. That means the paned widget only contains the
            // sidebar while the main contents are shown alone in the box container.
            gtk_container_remove(GTK_CONTAINER(paned), sidebar);

            if (sidebarRight) {
                gtk_paned_pack2(paned, sidebar, false, false);
            } else {
                gtk_paned_pack1(paned, sidebar, false, false);
            }
        }
#else
        gtk_paned_set_start_child(paned, nullptr);
        gtk_paned_set_end_child(paned, nullptr);
        if (sidebarRight) {
            gtk_paned_set_start_child(paned, mainContent);
            gtk_paned_set_resize_start_child(paned, true);
            gtk_paned_set_shrink_start_child(paned, false);
            gtk_paned_set_end_child(paned, sidebar);
            gtk_paned_set_resize_end_child(paned, false);
            gtk_paned_set_shrink_end_child(paned, false);
        } else {
            gtk_paned_set_end_child(paned, mainContent);
            gtk_paned_set_resize_end_child(paned, true);
            gtk_paned_set_shrink_end_child(paned, false);
            gtk_paned_set_start_child(paned, sidebar);
            gtk_paned_set_resize_start_child(paned, false);
            gtk_paned_set_shrink_start_child(paned, false);
        }
#endif
    }

    if (this->sidebarVisible) {
        updatePanedPosition(contentWidth);
    }
}

auto MainWindow::deleteEventCallback(GtkWidget* widget, GdkEvent* event, Control* control) -> bool {
    control->quit();

    return true;
}

void MainWindow::setSidebarVisible(bool visible) {
    if (!visible && (this->control->getSidebar() != nullptr)) {
        this->control->getSidebar()->saveSize();
    }

    if (visible != this->sidebarVisible) {
        // Due to a GTK bug, we can't just hide the sidebar widget in the GtkPaned.
        // If we do this, we create a dead region where the pane separator was previously.
        // In this region, we can't use the touchscreen to start horizontal strokes.
        // As such:
        if (!visible) {
            // hide sidebar
#if GTK_MAJOR_VERSION == 3
            gtk_container_remove(GTK_CONTAINER(panedContainerWidget.get()), mainContentWidget.get());
#else
            if (sidebarOnRight()) {
                gtk_paned_set_start_child(GTK_PANED(panedContainerWidget.get()), nullptr);
            } else {
                gtk_paned_set_end_child(GTK_PANED(panedContainerWidget.get()), nullptr);
            }
#endif
            gtk_box_remove(GTK_BOX(boxContainerWidget.get()), panedContainerWidget.get());
            gtk_box_append(GTK_BOX(boxContainerWidget.get()), mainContentWidget.get());
            this->sidebarVisible = false;
        } else {
            // show sidebar

            // Allocation is reset when we switch up the contained elements. Fetch the
            // width here in case we need it afterwards.
            int contentWidth = gtk_widget_get_width(boxContainerWidget.get());

            gtk_box_remove(GTK_BOX(boxContainerWidget.get()), mainContentWidget.get());

#if GTK_MAJOR_VERSION == 3
            if (sidebarOnRight()) {
                gtk_paned_pack1(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get(), true, false);
            } else {
                gtk_paned_pack2(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get(), true, false);
            }
#else
            if (sidebarOnRight()) {
                gtk_paned_set_start_child(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get());
            } else {
                gtk_paned_set_end_child(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get());
            }
#endif

            gtk_box_append(GTK_BOX(boxContainerWidget.get()), panedContainerWidget.get());
            this->sidebarVisible = true;

            updatePanedPosition(contentWidth);
        }
    }

    gtk_widget_set_visible(sidebarWidget.get(), visible);
}

/**
 * Invert the position of the paned widget and disconnect from the signal.
 * @param handlerId should be the ID of the signal handler that should be disconnected.
 */
static void invertPanedPosition(GtkWidget* widget, GtkAllocation* allocation, gulong* handlerId) {
    int newDividerPos = allocation->width - gtk_paned_get_position(GTK_PANED(widget));
    gtk_paned_set_position(GTK_PANED(widget), newDividerPos);

    // We only need to switch the position once, so disconnect the signal right away.
    g_signal_handler_disconnect(widget, *handlerId);
}

void MainWindow::updatePanedPosition(int contentWidth) {
    const int width = sidebarWidthNow();
    if (!sidebarOnRight()) {
        // Sidebar is on the left side.
        gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()), width);
    } else {
        // Sidebar is on the right side.
        if (contentWidth > 0) {
            int dividerPos = contentWidth - width;
            gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()), dividerPos);
        } else {
            // Allocation is unkown (window hasn't been shown yet). We have to wait for the signal.
            // Set position as if the sidebar was on the left side, and let the signal handler
            // simply invert the position when the allocation is known.
            gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()), width);
            gulong* signal_id = new gulong{};
            *signal_id = g_signal_connect_data(
                    this->panedContainerWidget.get(), "size-allocate",
                    xoj::util::wrap_for_g_callback_v<invertPanedPosition>, signal_id,
                    [](gpointer d, GClosure*) { delete reinterpret_cast<gulong*>(d); }, GConnectFlags(0));
        }
    }
}

bool MainWindow::sidebarOnRight() const { return control->getSettings()->isSidebarOnRight() && !studioPagesShown; }

int MainWindow::sidebarWidthNow() const {
    return studioPagesShown ? STUDIO_PAGES_WIDTH : control->getSettings()->getSidebarWidth();
}

void MainWindow::setStudioPages(bool shown) {
    if (shown == studioPagesShown) {
        return;
    }
    Sidebar* sidebar = control->getSidebar();
    if (sidebar && sidebarVisible) {
        // the Classic width is kept as it was: saved before the column takes the place of the sidebar (in the column
        // the sidebar does not save)
        sidebar->saveSize();
    }
    studioPagesShown = shown;
    if (sidebar) {
        sidebar->setStudioColumn(shown);
    }
    if (studioPagesButton) {
        // GTK emits "clicked" when the state is set here: the handler of the button must not take it as a click
        studioPagesSyncing = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(studioPagesButton), shown);
        studioPagesSyncing = false;
    }
    updateScrollbarSidebarPosition();  // the side and the width follow the column
}

void MainWindow::setToolbarVisible(bool visible) {
    Settings* settings = control->getSettings();

    settings->setToolbarVisible(visible);
    for (auto& w: this->toolbarWidgets) {
        if (!visible || (gtk_toolbar_get_n_items(GTK_TOOLBAR(w.get())) != 0)) {
            gtk_widget_set_visible(w.get(), visible);
        }
    }
}

void MainWindow::setMenubarVisible(bool visible) {
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(this->getWindow()), visible);
}

void MainWindow::setMaximized(bool maximized) { this->maximized = maximized; }

auto MainWindow::isMaximized() const -> bool { return this->maximized; }

auto MainWindow::setFullscreen(bool enabled) const -> void {
    if (enabled) {
        gtk_window_fullscreen(GTK_WINDOW(this->getWindow()));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(this->getWindow()));
    }
}

auto MainWindow::isDarkTheme() const -> bool { return this->darkMode; }

auto MainWindow::getXournal() const -> XournalView* { return xournal.get(); }

auto MainWindow::windowMaximizedCallback(GObject* window, GParamSpec*, MainWindow* win) -> void {
    win->setMaximized(gtk_window_is_maximized(GTK_WINDOW(window)));
}

void MainWindow::toolbarSelected(const std::string& id) {
    const auto& toolbars = toolbar->getModel()->getToolbars();
    auto it = std::find_if(toolbars.begin(), toolbars.end(), [&](const auto& d) { return d->getId() == id; });
    ToolbarData* d = it == toolbars.end() ? nullptr : it->get();

    // Ambitus: the presets of the house bring the palette they were made for; a palette chosen by the person stays
    Settings* settings = control->getSettings();
    const std::string palette =
            d ? ambitus::paletteFileForToolbar(d->getId(), settings->getColorPaletteSetting().filename().string()) : "";
    const bool same = d && this->selectedToolbar == d;
    if (!palette.empty()) {
        settings->setColorPaletteSetting(Util::getBuiltInPaletteDirectoryPath() / palette);
        control->loadPaletteFromSettings();
    }

    toolbarSelected(d);

    if (!palette.empty()) {
        if (same) {  // the colour buttons follow the palette even when the bar itself stays
            this->reloadToolbars();
        }
        settings->save();
    }
}

void MainWindow::toolbarSelected(ToolbarData* d) {
    if (!d || this->selectedToolbar == d) {
        return;
    }

    Settings* settings = control->getSettings();
    settings->setSelectedToolbar(d->getId());

    this->clearToolbar();
    this->loadToolbar(d);
}

auto MainWindow::clearToolbar() -> const ToolbarData* {
    // the Studio zoom buttons go with the bars they were put in
    this->studioZoomItems.clear();
    this->studioPageItems.clear();
    this->studioPagesButton = nullptr;

    if (this->selectedToolbar != nullptr) {
        for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
            ToolMenuHandler::unloadToolbar(this->toolbarWidgets[i].get());
        }

        this->toolbar->freeDynamicToolbarItems();
    }
    return std::exchange(this->selectedToolbar, nullptr);
}

void MainWindow::loadToolbar(ToolbarData* d) {
    this->selectedToolbar = d;

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbar->load(d, this->toolbarWidgets[i].get(), TOOLBAR_DEFINITIONS[i].propName,
                            TOOLBAR_DEFINITIONS[i].horizontal);
    }

    this->updateStudioZoom();
    this->updateStudioPageItems();
    this->floatingToolbox->flagRecalculateSizeRequired();
}

void MainWindow::updateStudioZoom() {
    GtkWidget* bottom = nullptr;
    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        if (std::string_view(TOOLBAR_DEFINITIONS[i].guiName) == "tbBottom1") {
            bottom = this->toolbarWidgets[i].get();
            break;
        }
    }
    if (!bottom) {
        return;
    }

    auto mode = static_cast<ambitus::EditorMode>(control->getSettings()->getEditorMode());
    const bool board = control->isBoardOpen() && !isLibraryShown();
    const bool studio = (mode == ambitus::EditorMode::STUDIO || board) && !isLibraryShown();

    if (!studio) {
        for (GtkToolItem* it: studioZoomItems) {
            if (gtk_widget_get_parent(GTK_WIDGET(it)) == bottom) {
                gtk_container_remove(GTK_CONTAINER(bottom), GTK_WIDGET(it));
            }
        }
        studioZoomItems.clear();
        return;
    }
    if (!studioZoomItems.empty() || this->toolbar->getToolItems().empty()) {
        return;  // already there, or the tool items are not built yet
    }

    // the bottom bar alone has no zoom: the buttons of the preset are in the top bar, which the Studio hides
    std::vector<std::string> present;
    int position = -1;
    const int count = gtk_toolbar_get_n_items(GTK_TOOLBAR(bottom));
    for (int i = 0; i < count; i++) {
        GtkWidget* widget = GTK_WIDGET(gtk_toolbar_get_nth_item(GTK_TOOLBAR(bottom), i));
        if (auto* data = ToolitemDragDrop::metadataGetMetadata(widget); data && data->item) {
            present.push_back(data->item->getId());
            if (data->item->getId() == "ZOOM_SLIDER") {
                position = i;  // the zoom buttons go before the slider, as in the other bars
            }
        }
    }

    for (const char* id: {"ZOOM_OUT", "ZOOM_IN", "ZOOM_FIT"}) {
        if (std::find(present.begin(), present.end(), id) != present.end()) {
            continue;  // the preset already brings this one in its bottom bar
        }
        for (const auto& item: this->toolbar->getToolItems()) {
            if (item->getId() != id) {
                continue;
            }
            auto toolItem = item->createToolItem(true);
            gtk_toolbar_insert(GTK_TOOLBAR(bottom), GTK_TOOL_ITEM(toolItem.get()), position);
            ToolitemDragDrop::attachMetadata(toolItem.get(), -1, item.get());
            this->studioZoomItems.push_back(GTK_TOOL_ITEM(toolItem.get()));
            if (position >= 0) {
                position++;
            }
            break;
        }
    }
}

GtkWidget* MainWindow::studioBottomBar() const {
    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        if (std::string_view(TOOLBAR_DEFINITIONS[i].guiName) == "tbBottom1") {
            return this->toolbarWidgets[i].get();
        }
    }
    return nullptr;
}

/// Ambitus: whether a widget is or holds a spin button (the page spinner of the bar holds one)
static bool holdsSpinButton(GtkWidget* w) {
    if (GTK_IS_SPIN_BUTTON(w)) {
        return true;
    }
    if (!GTK_IS_CONTAINER(w)) {
        return false;
    }
    bool found = false;
    GList* children = gtk_container_get_children(GTK_CONTAINER(w));
    for (GList* l = children; l != nullptr && !found; l = l->next) {
        found = holdsSpinButton(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    return found;
}

void MainWindow::updateStudioPageItems() {
    GtkWidget* bottom = studioBottomBar();
    if (!bottom) {
        return;
    }

    auto mode = static_cast<ambitus::EditorMode>(control->getSettings()->getEditorMode());
    const bool board = control->isBoardOpen() && !isLibraryShown();
    const bool studio = mode == ambitus::EditorMode::STUDIO && !board && !isLibraryShown();
    if (!studio) {
        for (GtkToolItem* it: studioPageItems) {
            if (gtk_widget_get_parent(GTK_WIDGET(it)) == bottom) {
                gtk_container_remove(GTK_CONTAINER(bottom), GTK_WIDGET(it));
            }
        }
        studioPageItems.clear();
        studioPagesButton = nullptr;
        return;
    }
    if (!studioPageItems.empty()) {
        return;
    }

    // "Pages" at the left of the bar: it opens and closes the column of the page thumbnails
    IconNameHelper icons(control->getSettings());
    GtkWidget* button = gtk_toggle_button_new();
    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* icon =
            gtk_image_new_from_icon_name(icons.iconName("sidebar-page-preview").c_str(), GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_box_pack_start(GTK_BOX(content), icon, false, false, 0);
    gtk_box_pack_start(GTK_BOX(content), gtk_label_new(_("Pages")), false, false, 0);
    gtk_container_add(GTK_CONTAINER(button), content);
    gtk_widget_set_tooltip_text(button, _("Show or hide the pages beside the page"));
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(+[](MainWindow* self) {
                                 if (!self->studioPagesSyncing) {  // a click of the user, not the state of the setting
                                     self->toggleStudioPages();
                                 }
                             }),
                             this);
    gtk_widget_show_all(button);
    studioPagesSyncing = true;  // the handler above ignores this "clicked" (GTK sends it when the state is set)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), studioPagesShown);
    studioPagesSyncing = false;
    studioPagesButton = button;

    GtkToolItem* toggle = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(toggle), button);
    gtk_widget_show_all(GTK_WIDGET(toggle));
    GtkToolItem* separator = gtk_separator_tool_item_new();
    gtk_widget_show(GTK_WIDGET(separator));
    gtk_toolbar_insert(GTK_TOOLBAR(bottom), toggle, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(bottom), separator, 1);

    // "+ Page" right after "Page n of m": the page after the current one, as the New page after action makes it
    GtkWidget* addButton = gtk_button_new_with_label(_("+ Page"));
    gtk_widget_set_tooltip_text(addButton, _("New page after"));
    g_signal_connect_swapped(addButton, "clicked", G_CALLBACK(+[](MainWindow* self) {
                                 self->control->insertNewPage(self->control->getCurrentPageNo() + 1);
                             }),
                             this);
    GtkToolItem* add = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(add), addButton);
    gtk_widget_show_all(GTK_WIDGET(add));
    int spinnerIndex = -1;
    for (int i = 0; i < gtk_toolbar_get_n_items(GTK_TOOLBAR(bottom)); i++) {
        if (holdsSpinButton(GTK_WIDGET(gtk_toolbar_get_nth_item(GTK_TOOLBAR(bottom), i)))) {
            spinnerIndex = i;
            break;
        }
    }
    gtk_toolbar_insert(GTK_TOOLBAR(bottom), add, spinnerIndex < 0 ? 2 : spinnerIndex + 1);
    studioPageItems = {toggle, separator, add};
}

void MainWindow::toggleStudioPages() {
    auto* settings = control->getSettings();
    settings->setAmbitusStudioPages(!settings->getAmbitusStudioPages());
    applyEditorMode();
}

void MainWindow::reloadToolbars() {
    ToolbarData* d = getSelectedToolbar();
    this->clearToolbar();
    this->toolbarSelected(d);
}

auto MainWindow::getSelectedToolbar() const -> ToolbarData* { return this->selectedToolbar; }

auto MainWindow::getToolbarWidgets() const -> const ToolbarWidgetArray& { return toolbarWidgets; }

auto MainWindow::getToolbarName(GtkToolbar* toolbar) const -> const char* {
    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        if (static_cast<void*>(this->toolbarWidgets[i].get()) == static_cast<void*>(toolbar)) {
            return TOOLBAR_DEFINITIONS[i].propName;
        }
    }

    return "";
}

void MainWindow::setDynamicallyGeneratedSubmenuDisabled(bool disabled) { menubar->setDisabled(disabled); }

void MainWindow::updateToolbarMenu() {
    menubar->getToolbarSelectionSubmenu().update(toolbar.get(), this->selectedToolbar);
}

void MainWindow::createToolbar() {
    toolbarSelected(control->getSettings()->getSelectedToolbar());

    this->control->getScheduler()->unblockRerenderZoom();
}

void MainWindow::updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage) {
    toolbar->setPageInfo(page, pagecount, pdfpage);
}

auto MainWindow::getMenubar() const -> Menubar* { return menubar.get(); }

void MainWindow::show(GtkWindow* parent) {
    gtk_widget_show(this->window);
    // after the menu bar is restored (queued in the constructor)
    Util::execInUiThread([this]() { applyEditorMode(); });
}

void MainWindow::setUndoDescription(const string& description) { menubar->setUndoDescription(description); }

void MainWindow::setRedoDescription(const string& description) { menubar->setRedoDescription(description); }

auto MainWindow::getToolbarModel() const -> ToolbarModel* { return this->toolbar->getModel(); }

auto MainWindow::getToolMenuHandler() const -> ToolMenuHandler* { return this->toolbar.get(); }

void MainWindow::loadMainCSS(GladeSearchpath* gladeSearchPath, const gchar* cssFilename) {
    auto filepath = gladeSearchPath->findFile("", cssFilename);
    xoj::util::GObjectSPtr<GtkCssProvider> provider(gtk_css_provider_new(), xoj::util::adopt);
    gtk_css_provider_load_from_path(provider.get(), char_cast(filepath.u8string().c_str()), nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider.get()),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

PdfFloatingToolbox* MainWindow::getPdfToolbox() const { return this->pdfFloatingToolBox.get(); }

FloatingToolbox* MainWindow::getFloatingToolbox() const { return this->floatingToolbox.get(); }

void MainWindow::setDPI() const {
    if (auto dpi = this->getControl()->getSettings()->getDisplayDpi(); dpi == -1) {
        auto res = xoj::util::gtk::getWidgetDPI(this->window);
        this->getControl()->getZoomControl()->setZoom100Value(res.value_or(Util::DPI_NORMALIZATION_FACTOR) /
                                                              Util::DPI_NORMALIZATION_FACTOR);
    } else {
        this->getControl()->getZoomControl()->setZoom100Value(dpi / Util::DPI_NORMALIZATION_FACTOR);
    }
}
