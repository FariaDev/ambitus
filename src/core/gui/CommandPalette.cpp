/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "CommandPalette.h"

#include <string>

#include "util/i18n.h"

namespace ambitus {

namespace {
constexpr size_t MAX_ROWS = 9;

void addClass(GtkWidget* w, const char* cls) { gtk_style_context_add_class(gtk_widget_get_style_context(w), cls); }

GtkWidget* dimLabel(const std::string& text, const char* cls) {
    GtkWidget* l = gtk_label_new(text.c_str());
    addClass(l, cls);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_START);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 26);
    return l;
}
}  // namespace

CommandPalette::CommandPalette(GtkOverlay* overlay, GtkWindow* window, std::function<GMenuModel*()> menu,
                               GtkWidget* focusBack):
        window(window), menu(std::move(menu)), focusBack(focusBack) {
    frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    addClass(frame, "amb-cmd");
    gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(frame, GTK_ALIGN_START);
    gtk_widget_set_margin_top(frame, 56);
    gtk_widget_set_size_request(frame, 520, -1);

    entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Type a command…"));
    gtk_container_add(GTK_CONTAINER(frame), entry);

    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_BROWSE);
    gtk_widget_set_can_focus(list, false);
    gtk_container_add(GTK_CONTAINER(frame), list);

    empty = gtk_label_new(_("No command with that name."));
    addClass(empty, "amb-cmd-empty");
    gtk_widget_set_no_show_all(empty, true);
    gtk_container_add(GTK_CONTAINER(frame), empty);

    g_signal_connect_swapped(entry, "search-changed", G_CALLBACK(+[](CommandPalette* self) { self->refresh(); }), this);
    g_signal_connect_swapped(entry, "stop-search", G_CALLBACK(+[](CommandPalette* self) { self->close(); }), this);
    g_signal_connect(entry, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* e, CommandPalette* self) -> gboolean {
                         // Xournal++'s SearchBar binds Return to "next-match" for every GtkSearchEntry: no "activate"
                         // here
                         if (e->keyval == GDK_KEY_Return || e->keyval == GDK_KEY_KP_Enter) {
                             // "search-changed" comes ~150 ms after the last key: the list may be of an older text
                             if (self->shownQuery != gtk_entry_get_text(GTK_ENTRY(self->entry))) {
                                 self->refresh();
                             }
                             self->runSelected();
                             return TRUE;
                         }
                         if (e->keyval == GDK_KEY_Down || e->keyval == GDK_KEY_KP_Down) {
                             self->move(1);
                             return TRUE;
                         }
                         if (e->keyval == GDK_KEY_Up || e->keyval == GDK_KEY_KP_Up) {
                             self->move(-1);
                             return TRUE;
                         }
                         return FALSE;
                     }),
                     this);
    // a click elsewhere closes it, after a click on a row has run
    g_signal_connect(entry, "focus-out-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, CommandPalette* self) -> gboolean {
                         if (self->closeIdle) {
                             return FALSE;
                         }
                         self->closeIdle = g_idle_add(
                                 +[](gpointer d) -> gboolean {
                                     auto* self = static_cast<CommandPalette*>(d);
                                     self->closeIdle = 0;
                                     // Alt+Tab to another window is not a click elsewhere: it stays open
                                     if (self->isOpen() && !gtk_widget_has_focus(self->entry) &&
                                         gtk_window_is_active(self->window)) {
                                         self->close();
                                     }
                                     return G_SOURCE_REMOVE;
                                 },
                                 self);
                         return FALSE;
                     }),
                     this);
    g_signal_connect(list, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, CommandPalette* self) {
                         gtk_list_box_select_row(GTK_LIST_BOX(self->list), row);
                         self->runSelected();
                     }),
                     this);

    gtk_widget_show_all(frame);
    gtk_widget_hide(frame);
    gtk_widget_set_no_show_all(frame, true);
    gtk_overlay_add_overlay(overlay, frame);
}

CommandPalette::~CommandPalette() {
    if (closeIdle) {
        g_source_remove(closeIdle);
    }
}

GActionGroup* CommandPalette::groupOf(const std::string& action, std::string& name) const {
    if (action.rfind("win.", 0) == 0) {
        name = action.substr(4);
        return G_ACTION_GROUP(window);
    }
    if (action.rfind("app.", 0) == 0) {
        name = action.substr(4);
        GtkApplication* app = gtk_window_get_application(window);
        return app ? G_ACTION_GROUP(app) : nullptr;
    }
    return nullptr;
}

void CommandPalette::open() {
    commands.clear();
    for (auto& c: collectCommands(menu())) {
        std::string name;
        GActionGroup* group = groupOf(c.action, name);
        if (group && g_action_group_has_action(group, name.c_str()) &&
            g_action_group_get_action_enabled(group, name.c_str()) && c.action != "win.command-palette") {
            commands.push_back(std::move(c));
        }
    }
    gtk_entry_set_text(GTK_ENTRY(entry), "");
    refresh();
    gtk_widget_show(frame);
    gtk_widget_grab_focus(entry);
}

void CommandPalette::close() {
    if (!isOpen()) {
        return;
    }
    gtk_widget_hide(frame);
    if (focusBack) {
        gtk_widget_queue_draw(focusBack);  // the page does not repaint by itself under an overlay
        gtk_widget_grab_focus(focusBack);
    }
}

bool CommandPalette::isOpen() const { return gtk_widget_get_visible(frame); }

void CommandPalette::refresh() {
    GList* rows = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList* r = rows; r; r = r->next) {
        gtk_widget_destroy(GTK_WIDGET(r->data));
    }
    g_list_free(rows);

    shownQuery = gtk_entry_get_text(GTK_ENTRY(entry));
    shown = matchCommands(commands, shownQuery, MAX_ROWS);
    for (size_t i: shown) {
        auto const& c = commands[i];
        std::string label = c.label;
        std::string name;
        if (GActionGroup* group = groupOf(c.action, name); group && !c.target) {
            GVariant* state = g_action_group_get_action_state(group, name.c_str());
            if (state) {
                if (g_variant_is_of_type(state, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(state)) {
                    label += "  ✓";
                }
                g_variant_unref(state);
            }
        }
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget* l = gtk_label_new(label.c_str());
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_MIDDLE);  // the recent files are long paths
        gtk_label_set_max_width_chars(GTK_LABEL(l), 40);
        gtk_box_pack_start(GTK_BOX(box), l, true, true, 0);
        if (!c.path.empty()) {
            gtk_box_pack_start(GTK_BOX(box), dimLabel(c.path, "amb-cmd-path"), false, false, 0);
        }
        guint key = 0;
        GdkModifierType mods{};
        if (!c.accel.empty() && (gtk_accelerator_parse(c.accel.c_str(), &key, &mods), key != 0)) {
            gchar* shortcut = gtk_accelerator_get_label(key, mods);
            gtk_box_pack_end(GTK_BOX(box), dimLabel(shortcut, "amb-cmd-accel"), false, false, 0);
            g_free(shortcut);
        }
        GtkWidget* row = gtk_list_box_row_new();
        gtk_widget_set_can_focus(row, false);
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_container_add(GTK_CONTAINER(list), row);
    }
    gtk_widget_show_all(list);
    gtk_widget_set_visible(empty, shown.empty());
    if (focusBack) {
        gtk_widget_queue_draw(focusBack);  // the list may have shrunk
    }
    if (GtkListBoxRow* first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), 0)) {
        gtk_list_box_select_row(GTK_LIST_BOX(list), first);
    }
}

void CommandPalette::move(int delta) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
    const int n = static_cast<int>(shown.size());
    if (n == 0) {
        return;
    }
    int at = row ? gtk_list_box_row_get_index(row) : 0;
    at = ((at + delta) % n + n) % n;
    gtk_list_box_select_row(GTK_LIST_BOX(list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), at));
}

void CommandPalette::runSelected() {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
    if (!row) {
        return;
    }
    const auto at = static_cast<size_t>(gtk_list_box_row_get_index(row));
    if (at >= shown.size()) {
        return;
    }
    const Command c = commands[shown[at]];
    close();
    std::string name;
    if (GActionGroup* group = groupOf(c.action, name)) {
        g_action_group_activate_action(group, name.c_str(), c.target.get());
    }
}

}  // namespace ambitus
