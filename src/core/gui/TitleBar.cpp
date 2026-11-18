#include "TitleBar.h"

#include "util/i18n.h"
#ifdef __APPLE__
#include "ambitus/MacWindowButtons.h"
#endif

namespace ambitus {

namespace {
void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

#ifdef __APPLE__
void useSystemButtonsOf(GtkWidget* window, gpointer) { macwindow::useSystemWindowButtons(GTK_WINDOW(window)); }
#endif

GtkWidget* iconButton(const char* icon, const char* action, const char* tooltip) {
    GtkWidget* b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(b), action);
    gtk_widget_set_tooltip_text(b, tooltip);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    addClass(b, "amb-tb-icon");
    return b;
}
}  // namespace

TitleBar::TitleBar(GtkWindow* window, GMenuModel* menu, std::function<void(EditorMode)> onMode):
        window(window), moreMenu([window] { return window; }), onMode(std::move(onMode)) {
    moreMenu.setFallbackAccels(menu);
    // the tab menu (right button on a tab) closes through this action, with the index of the tab
    if (G_IS_ACTION_MAP(window)) {
        GSimpleAction* closeTab = g_simple_action_new("close-tab", G_VARIANT_TYPE_UINT64);
        g_signal_connect(closeTab, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant* p, TitleBar* self) {
                             self->closeLater(static_cast<size_t>(g_variant_get_uint64(p)));
                         }),
                         this);
        g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(closeTab));
        g_object_unref(closeTab);
    }
    bar = gtk_header_bar_new();
    addClass(bar, "amb-titlebar");
#ifdef __APPLE__
    // macOS: the bar draws no window buttons. The system's are on the window, over the left of the bar, and the tabs
    // start after them (ambitus.css). The style is set once the window exists, and again on each map (GTK may set it)
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(bar), false);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(bar), ":");
    addClass(bar, "amb-mac");
    g_signal_connect_after(window, "realize", G_CALLBACK(useSystemButtonsOf), nullptr);
    g_signal_connect_after(window, "map", G_CALLBACK(useSystemButtonsOf), nullptr);
#else
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(bar), true);
#endif

    // left: the tabs (no mark: the Library tab comes first, as in Summa)
    strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(strip, "amb-tabs");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(bar), strip);

    // no title in the middle: the active tab says it
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(bar), gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));

    // right (packed from the right edge): menu, undo, modes
    more = gtk_button_new_from_icon_name("view-more-horizontal-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect_swapped(more, "clicked", G_CALLBACK(+[](TitleBar* self) { self->openMenu(); }), this);
    gtk_widget_set_tooltip_text(more, _("Menu"));
    gtk_widget_set_valign(more, GTK_ALIGN_CENTER);
    addClass(more, "amb-tb-icon");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), more);

    undo = iconButton("edit-undo-symbolic", "win.undo", _("Undo"));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), undo);

    modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(modes, "amb-modes");
    gtk_widget_set_valign(modes, GTK_ALIGN_CENTER);
    const char* names[3] = {_("Classic"), _("Studio"), _("Focus")};
    const char* tips[3] = {_("All the toolbars"), _("A floating palette above the page"), _("Only the page")};
    GSList* group = nullptr;
    for (int i = 0; i < 3; i++) {
        GtkWidget* b = gtk_radio_button_new_with_label(group, names[i]);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(b));
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), false);  // a button, not a radio dot
        gtk_widget_set_tooltip_text(b, tips[i]);
        g_object_set_data(G_OBJECT(b), "amb-mode", GINT_TO_POINTER(i));
        g_signal_connect(b, "toggled", G_CALLBACK(+[](GtkToggleButton* t, TitleBar* self) {
                             if (self->updating || !gtk_toggle_button_get_active(t)) {
                                 return;
                             }
                             int m = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(t), "amb-mode"));
                             self->onMode(static_cast<EditorMode>(m));
                         }),
                         this);
        gtk_box_pack_start(GTK_BOX(modes), b, false, false, 0);
        modeButtons[i] = b;
    }
    gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), modes);

    gtk_widget_show_all(bar);
    rebuildTabs();
}

namespace {
/// Whether the point (x, y) of `from` falls on `target`, a widget inside it
bool overWidget(GtkWidget* from, GtkWidget* target, double x, double y) {
    int tx = 0, ty = 0;
    if (!gtk_widget_translate_coordinates(from, target, static_cast<int>(x), static_cast<int>(y), &tx, &ty)) {
        return false;
    }
    GtkAllocation a;
    gtk_widget_get_allocation(target, &a);
    return tx >= 0 && ty >= 0 && tx < a.width && ty < a.height;
}
}  // namespace

void TitleBar::openTabMenu(GtkWidget* tab, size_t index) {
    GtkWidget* box = moreMenu.begin(tab, GTK_POS_BOTTOM, MENU_WIDTH);
    moreMenu.addItem(box, "xopp-x", _("Close"), "close-tab", g_variant_new_uint64(index));
    moreMenu.popup();
}

void TitleBar::openMenu() {
    // what the notebook gives: there is nothing to save, export or look back on in the library
    auto onlyInEditor = [this](GtkWidget* item) {
        if (!editorShown) {
            gtk_widget_set_sensitive(item, false);
        }
    };

    GtkWidget* box = moreMenu.begin(more, GTK_POS_BOTTOM, MENU_WIDTH);
    moreMenu.addItem(box, "xopp-file-plus", C_("Title bar menu", "New notebook"), "new-file", nullptr);
    moreMenu.addItem(box, "xopp-folder", C_("Title bar menu", "Open…"), "open", nullptr);
    moreMenu.addItem(box, "xopp-edit-undo", C_("Title bar menu", "Reopen closed tab"), "reopen-closed-tab", nullptr);
    onlyInEditor(moreMenu.addItem(box, "xopp-edit-copy", C_("Title bar menu", "Save a copy as…"), "save-as", nullptr));
    moreMenu.addSeparator(box);
    onlyInEditor(
            moreMenu.addItem(box, "xopp-file-down", C_("Title bar menu", "Export as PDF…"), "export-as-pdf", nullptr));
    moreMenu.addItem(box, "xopp-tool-image", C_("Title bar menu", "Annotate a PDF…"), "annotate-pdf", nullptr);
    // the notebook of the window: only in the editor, and only when it has a name to change
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].active && items[i].renamable && editorShown && onRename) {
            moreMenu.addCallItem(box, "xopp-tool-pencil", C_("Title bar menu", "Rename…"), [this, i] { onRename(i); });
        }
    }
    onlyInEditor(moreMenu.addItem(box, "xopp-history", C_("Title bar menu", "Versions of this notebook"),
                                  "earlier-versions", nullptr));
    moreMenu.addSeparator(box);
    moreMenu.addItem(box, "xopp-search", C_("Title bar menu", "Commands and notebooks"), "command-palette", nullptr);
    moreMenu.addItem(box, "xopp-settings", C_("Title bar menu", "Settings"), "app.preferences", nullptr);
    moreMenu.addSeparator(box);
    moreMenu.addItem(box, "xopp-book", C_("Title bar menu", "About Ambitus"), "app.about", nullptr);
    moreMenu.popup();
}

void TitleBar::setEditorShown(bool editor) {
    const bool changed = editor != editorShown;
    editorShown = editor;
    gtk_widget_set_visible(modes, editor && modesShown);
    gtk_widget_set_visible(undo, editor);
    // a click on the active tab comes here too: rebuilding then would take the tab away before its double click
    if (changed) {
        rebuildTabs();
    }
}

void TitleBar::setModesShown(bool shown) {
    modesShown = shown;
    gtk_widget_set_visible(modes, editorShown && modesShown);
}

void TitleBar::setMode(EditorMode mode) {
    updating = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(modeButtons[static_cast<int>(mode)]), true);
    updating = false;
}

void TitleBar::setTabCallbacks(std::function<void(size_t)> activate, std::function<void(size_t)> close) {
    onActivate = std::move(activate);
    onClose = std::move(close);
}

void TitleBar::setRenameCallback(std::function<void(size_t)> rename) { onRename = std::move(rename); }

void TitleBar::closeLater(size_t index) {
    if (!onClose) {
        return;
    }
    // after the click is done with the button: closing rebuilds the strip
    using Call = std::pair<std::function<void(size_t)>, size_t>;
    g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            +[](gpointer d) -> gboolean {
                auto* c = static_cast<Call*>(d);
                c->first(c->second);
                return G_SOURCE_REMOVE;
            },
            new Call(onClose, index), [](gpointer d) { delete static_cast<Call*>(d); });
}

void TitleBar::setTabs(std::vector<TabItem> newItems) {
    if (newItems == items) {
        return;  // called after every stroke (with the window title)
    }
    items = std::move(newItems);
    rebuildTabs();
}

void TitleBar::rebuildTabs() {
    gtk_container_foreach(GTK_CONTAINER(strip), [](GtkWidget* w, gpointer) { gtk_widget_destroy(w); }, nullptr);

    libraryTab = gtk_button_new();
    GtkWidget* lbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_add(GTK_CONTAINER(lbox), gtk_image_new_from_icon_name("view-grid-symbolic", GTK_ICON_SIZE_MENU));
    gtk_container_add(GTK_CONTAINER(lbox), gtk_label_new(_("Library")));
    gtk_container_add(GTK_CONTAINER(libraryTab), lbox);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(libraryTab), "win.library");
    addClass(libraryTab, "amb-tab");
    // the right button of the header bar opens the window menu, whose "Close" closes the whole window
    g_signal_connect(libraryTab, "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton* e, gpointer) -> gboolean {
                         return e->button == GDK_BUTTON_SECONDARY;
                     }),
                     nullptr);
    if (!editorShown) {
        addClass(libraryTab, "active");
    }
    gtk_box_pack_start(GTK_BOX(strip), libraryTab, false, false, 0);

    for (size_t i = 0; i < items.size(); i++) {
        const auto& it = items[i];
        GtkWidget* tab = gtk_button_new();
        addClass(tab, "amb-tab");
        if (it.active && editorShown) {
            addClass(tab, "active");
        }
        gtk_widget_set_tooltip_text(tab, it.title.c_str());
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);

        GtkWidget* dot = gtk_drawing_area_new();
        gtk_widget_set_size_request(dot, 7, 7);
        gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
        g_object_set_data_full(G_OBJECT(dot), "amb-color", g_strdup(it.color.c_str()), g_free);
        g_signal_connect(dot, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
                             const char* c = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "amb-color"));
                             GdkRGBA rgba;
                             if (!c || !*c || !gdk_rgba_parse(&rgba, c)) {
                                 // no course: the colour of the text, faint
                                 gtk_style_context_get_color(gtk_widget_get_style_context(w),
                                                             gtk_widget_get_state_flags(w), &rgba);
                                 rgba.alpha *= .35;
                             }
                             gdk_cairo_set_source_rgba(cr, &rgba);
                             cairo_arc(cr, 3.5, 3.5, 3.5, 0, 2 * G_PI);
                             cairo_fill(cr);
                             return false;
                         }),
                         nullptr);
        gtk_container_add(GTK_CONTAINER(box), dot);

        GtkWidget* label = gtk_label_new((it.modified ? it.title + " •" : it.title).c_str());
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 24);
        gtk_container_add(GTK_CONTAINER(box), label);
        g_object_set_data(G_OBJECT(tab), "amb-name", label);

        if (it.closable) {
            GtkWidget* x = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
            addClass(x, "amb-tab-close");
            gtk_widget_set_tooltip_text(x, _("Close"));
            gtk_widget_set_valign(x, GTK_ALIGN_CENTER);
            gtk_container_add(GTK_CONTAINER(box), x);
            // GtkButton puts its input window over its children: the x never gets the click, the tab does
            g_object_set_data(G_OBJECT(tab), "amb-close", x);
        }
        gtk_container_add(GTK_CONTAINER(tab), box);

        g_object_set_data(G_OBJECT(tab), "amb-index", GSIZE_TO_POINTER(i));
        g_signal_connect(tab, "clicked", G_CALLBACK(+[](GtkWidget* w, TitleBar* self) {
                             size_t i = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(w), "amb-index"));
                             if (self->onActivate) {
                                 self->onActivate(i);
                             }
                         }),
                         this);
        // the middle button closes, as in a browser, and so does the left one on the x
        g_signal_connect(tab, "button-release-event",
                         G_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, TitleBar* self) -> gboolean {
                             size_t i = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(w), "amb-index"));
                             if (!self->onClose || i >= self->items.size() || !self->items[i].closable) {
                                 return false;
                             }
                             if (e->button == GDK_BUTTON_MIDDLE) {
                                 self->closeLater(i);
                                 return true;
                             }
                             auto* x = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(w), "amb-close"));
                             if (e->button == GDK_BUTTON_PRIMARY && x && overWidget(w, x, e->x, e->y)) {
                                 self->closeLater(i);  // the click still activates the tab, as closing does first
                             }
                             return false;
                         }),
                         this);
        // the right button: the menu of the tab, not the one of the window
        g_signal_connect(tab, "button-press-event",
                         G_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, TitleBar* self) -> gboolean {
                             if (e->button != GDK_BUTTON_SECONDARY) {
                                 return false;
                             }
                             size_t i = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(w), "amb-index"));
                             if (i < self->items.size() && self->items[i].closable) {
                                 self->openTabMenu(w, i);
                             }
                             return true;
                         }),
                         this);
        // a double click on the name of a notebook's tab renames it (the first click has made the tab the active one)
        g_signal_connect(tab, "button-press-event",
                         G_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, TitleBar* self) -> gboolean {
                             if (e->type != GDK_2BUTTON_PRESS || e->button != GDK_BUTTON_PRIMARY) {
                                 return false;
                             }
                             size_t i = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(w), "amb-index"));
                             auto* name = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(w), "amb-name"));
                             if (!self->onRename || i >= self->items.size() || !self->items[i].renamable || !name ||
                                 !overWidget(w, name, e->x, e->y)) {
                                 return false;
                             }
                             self->onRename(i);
                             return true;
                         }),
                         this);
        gtk_box_pack_start(GTK_BOX(strip), tab, false, false, 0);
    }

    GtkWidget* plus = gtk_button_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_MENU);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(plus), "win.library");
    gtk_widget_set_tooltip_text(plus, _("Open another notebook"));
    gtk_widget_set_valign(plus, GTK_ALIGN_CENTER);
    addClass(plus, "amb-tb-icon");
    gtk_box_pack_start(GTK_BOX(strip), plus, false, false, 0);

    gtk_widget_show_all(strip);
}

}  // namespace ambitus
