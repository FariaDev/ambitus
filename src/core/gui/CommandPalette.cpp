/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "CommandPalette.h"

#include <algorithm>
#include <map>
#include <string>

#include "control/ToolEnums.h"
#include "util/i18n.h"

namespace ambitus {

namespace {
constexpr int ICON_PX = 16;
constexpr int MAX_LIST_HEIGHT = 300;
constexpr int TOP_MARGIN = 64;
constexpr int BOX_WIDTH = 520;

void addClass(GtkWidget* w, const char* cls) { gtk_style_context_add_class(gtk_widget_get_style_context(w), cls); }

bool animationsOn(GtkWidget* w) {
    gboolean on = true;
    g_object_get(gtk_widget_get_settings(w), "gtk-enable-animations", &on, nullptr);
    return on;
}

/// The icon, or an empty space of its size when there is none (the names line up)
GtkWidget* icon(const char* name, int px = ICON_PX) {
    if (!name) {
        GtkWidget* gap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_size_request(gap, px, px);
        return gap;
    }
    GtkWidget* img = gtk_image_new_from_icon_name(name, GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(img), px);
    return img;
}

/// The icon of a command: the tool it picks, or what it does; null when it has none
const char* iconFor(const Command& c) {
    if (c.action == "win.select-tool") {
        if (!c.target || !g_variant_is_of_type(c.target.get(), G_VARIANT_TYPE_UINT64)) {
            return nullptr;
        }
        switch (g_variant_get_uint64(c.target.get())) {
            case TOOL_PEN:
                return "xopp-tool-pencil";
            case TOOL_ERASER:
                return "xopp-tool-eraser";
            case TOOL_HIGHLIGHTER:
                return "xopp-tool-highlighter";
            case TOOL_TEXT:
                return "xopp-tool-text";
            case TOOL_IMAGE:
                return "xopp-tool-image";
            case TOOL_SELECT_RECT:
                return "xopp-select-rect";
            case TOOL_SELECT_REGION:
                return "xopp-select-lasso";
            case TOOL_HAND:
                return "xopp-hand";
            case TOOL_VERTICAL_SPACE:
                return "xopp-vertical-space";
            case TOOL_LASER_POINTER_PEN:
                return "xopp-laser-pointer";
            default:
                return nullptr;
        }
    }
    static const std::map<std::string, const char*> byAction = {
            {"win.new-file", "xopp-document-new"},
            {"win.open", "xopp-document-open"},
            {"win.save", "xopp-document-save"},
            {"win.export-as-pdf", "xopp-document-export-pdf"},
            {"win.print", "xopp-document-print"},
            {"win.library", "xopp-book"},
            {"win.search-notebooks", "xopp-search"},
            {"win.search", "xopp-search"},
            {"win.undo", "xopp-edit-undo"},
            {"win.redo", "xopp-edit-redo"},
            {"win.cut", "xopp-edit-cut"},
            {"win.copy", "xopp-edit-copy"},
            {"win.copy-page-as-image", "xopp-edit-copy"},
            {"win.paste", "xopp-edit-paste"},
            {"win.delete", "xopp-trash"},
            {"win.delete-page", "xopp-trash"},
            {"win.new-page-before", "xopp-file-plus"},
            {"win.new-page-after", "xopp-file-plus"},
            {"win.new-page-at-end", "xopp-file-plus"},
            {"win.send-page-to-summa", "xopp-send"},
            {"win.send-other-part-to-summa", "xopp-send"},
            {"win.convert-to-text", "xopp-convert-text"},
            {"win.earlier-versions", "xopp-history"},
            {"win.preferences", "xopp-settings"},
            {"win.grid-snapping", "xopp-snapping-grid"},
            {"win.rotation-snapping", "xopp-snapping-rotation"},
            {"win.fullscreen", "xopp-fullscreen"},
            {"win.presentation-mode", "xopp-presentation-mode"},
            {"win.show-sidebar", "xopp-sidebar-show"},
    };
    auto it = byAction.find(c.action);
    return it == byAction.end() ? nullptr : it->second;
}

/// The text as Pango markup, with what the query finds marked as the prototype does: the colour of the text at 16%
std::string markupOf(const std::string& text, const std::string& query, const std::string& markColor) {
    auto escaped = [](const std::string& s) {
        gchar* e = g_markup_escape_text(s.c_str(), static_cast<gssize>(s.size()));
        std::string out = e;
        g_free(e);
        return out;
    };
    std::string out;
    size_t at = 0;
    for (auto const& [from, to]: highlightRanges(text, query)) {
        out += escaped(text.substr(at, from - at));
        out += "<span bgcolor=\"" + markColor + "\" bgalpha=\"16%\">" + escaped(text.substr(from, to - from)) +
               "</span>";
        at = to;
    }
    return out + escaped(text.substr(at));
}

GtkWidget* sectionTitle(PaletteSection section) {
    const char* name = section == PaletteSection::Recent   ? _("Recent") :
                       section == PaletteSection::Commands ? _("Commands") :
                                                             _("Notebooks");
    gchar* upper = g_utf8_strup(name, -1);
    gchar* escaped = g_markup_escape_text(upper, -1);
    gchar* markup = g_strdup_printf("<span weight=\"600\" letter_spacing=\"650\">%s</span>", escaped);
    GtkWidget* l = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(l), markup);
    g_free(markup);
    g_free(escaped);
    g_free(upper);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    addClass(l, "amb-cmd-sec");
    gtk_widget_show(l);
    return l;
}
}  // namespace

CommandPalette::CommandPalette(GtkOverlay* overlay, GtkWindow* window, std::function<GMenuModel*()> menu,
                               GtkWidget* focusBack, Notebooks notebooks):
        window(window), menu(std::move(menu)), focusBack(focusBack), notebooks(std::move(notebooks)) {
    // the veil covers the window; the box is at the top of it. Clicking on the veil (and not on the box) closes
    revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    GtkWidget* clicks = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(clicks), false);
    GtkWidget* veil = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(veil, "amb-cmd-veil");
    gtk_container_add(GTK_CONTAINER(clicks), veil);
    gtk_container_add(GTK_CONTAINER(revealer), clicks);

    panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(panel, "amb-cmd");
    gtk_widget_set_halign(panel, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(panel, GTK_ALIGN_START);
    gtk_widget_set_margin_top(panel, TOP_MARGIN);
    gtk_widget_set_size_request(panel, BOX_WIDTH, -1);
    gtk_container_add(GTK_CONTAINER(veil), panel);

    // a plain entry: a GtkSearchEntry would have its Return taken by the SearchBar of Xournal++ and its text only
    // reaches us ~150 ms after the last key
    entry = gtk_entry_new();
    addClass(entry, "amb-cmd-entry");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry), GTK_ENTRY_ICON_PRIMARY, "xopp-search");
    gtk_entry_set_icon_activatable(GTK_ENTRY(entry), GTK_ENTRY_ICON_PRIMARY, false);
    // GTK hides the placeholder of a focused entry, and the box always has the focus: the hint is a label over it
    GtkWidget* entryArea = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(entryArea), entry);
    hint = gtk_label_new(_("Command or notebook…"));
    addClass(hint, "amb-cmd-hint");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_widget_set_valign(hint, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(entryArea), hint);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(entryArea), hint, true);
    gtk_container_add(GTK_CONTAINER(panel), entryArea);

    scroller = gtk_scrolled_window_new(nullptr, nullptr);
    addClass(scroller, "amb-cmd-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), true);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), MAX_LIST_HEIGHT);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_BROWSE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), true);
    gtk_widget_set_can_focus(list, false);
    // the title of each section goes above its first row
    gtk_list_box_set_header_func(
            GTK_LIST_BOX(list),
            +[](GtkListBoxRow* row, GtkListBoxRow* before, gpointer) {
                auto section = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "amb-section"));
                if (!before || GPOINTER_TO_INT(g_object_get_data(G_OBJECT(before), "amb-section")) != section) {
                    gtk_list_box_row_set_header(row, sectionTitle(static_cast<PaletteSection>(section - 1)));
                } else {
                    gtk_list_box_row_set_header(row, nullptr);
                }
            },
            nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroller), list);
    gtk_container_add(GTK_CONTAINER(panel), scroller);

    empty = gtk_label_new("");
    addClass(empty, "amb-cmd-empty");
    gtk_label_set_xalign(GTK_LABEL(empty), 0);
    gtk_label_set_ellipsize(GTK_LABEL(empty), PANGO_ELLIPSIZE_END);
    gtk_widget_set_no_show_all(empty, true);
    gtk_container_add(GTK_CONTAINER(panel), empty);

    g_signal_connect_swapped(entry, "changed", G_CALLBACK(+[](CommandPalette* self) {
                                 if (!self->opening) {
                                     self->refresh();
                                 }
                             }),
                             this);
    g_signal_connect(entry, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* e, CommandPalette* self) -> gboolean {
                         switch (e->keyval) {
                             case GDK_KEY_Return:
                             case GDK_KEY_KP_Enter:
                                 self->runSelected((e->state & GDK_MOD1_MASK) != 0);  // Alt+Enter: beside
                                 return TRUE;
                             case GDK_KEY_Escape:
                                 self->close();
                                 return TRUE;
                             case GDK_KEY_Down:
                             case GDK_KEY_KP_Down:
                                 self->move(1);
                                 return TRUE;
                             case GDK_KEY_Up:
                             case GDK_KEY_KP_Up:
                                 self->move(-1);
                                 return TRUE;
                             default:
                                 return FALSE;
                         }
                     }),
                     this);
    // a click on the veil closes it; one on the box only gets here from a child that left it unhandled, and is ignored
    g_signal_connect(clicks, "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton* e, CommandPalette* self) -> gboolean {
                         GtkAllocation a;
                         gtk_widget_get_allocation(self->panel, &a);
                         gint ox = 0;
                         gint oy = 0;
                         gdk_window_get_origin(gtk_widget_get_window(self->panel), &ox, &oy);
                         const bool inside = e->x_root >= ox + a.x && e->x_root < ox + a.x + a.width &&
                                             e->y_root >= oy + a.y && e->y_root < oy + a.y + a.height;
                         if (!inside) {
                             self->close();
                         }
                         return TRUE;
                     }),
                     this);
    // a click on the title bar or elsewhere outside the overlay closes it too, after a click on a row has run
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
                         self->runSelected(false);
                     }),
                     this);
    // once faded out, the revealer goes away, so it takes no clicks from the page
    g_signal_connect_swapped(revealer, "notify::child-revealed", G_CALLBACK(+[](CommandPalette* self) {
                                 auto* r = GTK_REVEALER(self->revealer);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     gtk_widget_hide(self->revealer);
                                 }
                             }),
                             this);

    gtk_widget_show_all(revealer);
    gtk_widget_hide(empty);
    gtk_widget_set_no_show_all(revealer, true);
    gtk_widget_hide(revealer);
    gtk_overlay_add_overlay(overlay, revealer);
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

std::string CommandPalette::shortcutOf(const Command& c) const {
    std::string accel = c.accel;  // the menu file, when the application has none for it
    if (GtkApplication* app = gtk_window_get_application(window)) {
        gchar* detailed = g_action_print_detailed_name(c.action.c_str(), c.target.get());
        gchar** accels = gtk_application_get_accels_for_action(app, detailed);
        if (accels && accels[0]) {
            accel = accels[0];
        }
        g_strfreev(accels);
        g_free(detailed);
    }
    guint key = 0;
    GdkModifierType mods{};
    if (accel.empty() || (gtk_accelerator_parse(accel.c_str(), &key, &mods), key == 0)) {
        return "";
    }
    gchar* label = gtk_accelerator_get_label(key, mods);
    std::string out = label;
    g_free(label);
    return out;
}

void CommandPalette::open(bool beside) {
    besideMode = beside;
    gtk_label_set_text(GTK_LABEL(hint), beside ? _("Notebook to open beside…") : _("Command or notebook…"));
    if (isOpen()) {
        refresh();
        gtk_widget_grab_focus(entry);
        gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
        return;
    }
    commands.clear();
    for (auto& c: collectCommands(menu())) {
        std::string name;
        GActionGroup* group = groupOf(c.action, name);
        // the files of "Recent documents" are there to be opened by number: the notebooks of the box do that by name
        const bool recentFile =
                c.action.rfind("win.open-file-at", 0) == 0 || c.action.rfind("win.remove-file-at", 0) == 0;
        if (group && g_action_group_has_action(group, name.c_str()) &&
            g_action_group_get_action_enabled(group, name.c_str()) && c.action != "win.command-palette" &&
            !recentFile) {
            commands.push_back(std::move(c));
        }
    }
    books = notebooks.all ? notebooks.all() : std::vector<library::NotebookRef>{};
    recentBooks.clear();
    if (notebooks.recent) {
        for (auto const& r: notebooks.recent()) {
            auto it = std::find_if(books.begin(), books.end(), [&](auto const& b) { return b.path == r.path; });
            if (it != books.end()) {
                recentBooks.push_back(static_cast<size_t>(it - books.begin()));
            }
        }
    }
    opening = true;
    gtk_entry_set_text(GTK_ENTRY(entry), "");
    opening = false;
    refresh();
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? palette::FADE_IN_MS : 0);
    gtk_widget_show(revealer);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
    gtk_widget_realize(entry);  // the revealer realizes its child late, and the focus change needs it realized
    gtk_widget_grab_focus(entry);
}

void CommandPalette::close() {
    if (!isOpen()) {
        return;
    }
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? palette::FADE_OUT_MS : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), false);
    // without a transition, child-revealed does not change after the revealer is off: hide it here
    if (!animationsOn(revealer)) {
        gtk_widget_hide(revealer);
    }
    if (focusBack) {
        gtk_widget_queue_draw(focusBack);  // the page does not repaint by itself under an overlay
        gtk_widget_grab_focus(focusBack);
    }
}

bool CommandPalette::isOpen() const { return gtk_revealer_get_reveal_child(GTK_REVEALER(revealer)); }

GtkWidget* CommandPalette::makeRow(const PaletteRow& r, const std::string& query) {
    GtkStyleContext* ctx = gtk_widget_get_style_context(panel);
    GdkRGBA color{};
    gtk_style_context_get_color(ctx, GTK_STATE_FLAG_NORMAL, &color);
    gchar* hex = g_strdup_printf("#%02x%02x%02x", static_cast<int>(color.red * 255 + 0.5),
                                 static_cast<int>(color.green * 255 + 0.5), static_cast<int>(color.blue * 255 + 0.5));
    const std::string markColor = hex;
    g_free(hex);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    std::string name, small, shortcut;
    bool checked = false;
    if (r.section == PaletteSection::Commands) {
        auto const& c = commands[r.index];
        name = c.label;
        small = query.empty() ? "" : c.path;  // the way in the menu, to tell two of a name apart
        shortcut = shortcutOf(c);
        std::string action;
        if (GActionGroup* group = groupOf(c.action, action); group && !c.target) {
            GVariant* state = g_action_group_get_action_state(group, action.c_str());
            if (state) {
                checked = g_variant_is_of_type(state, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(state);
                g_variant_unref(state);
            }
        }
        gtk_box_pack_start(GTK_BOX(box), icon(iconFor(c)), false, false, 0);
    } else {
        auto const& b = books[r.index];
        name = b.name;
        small = b.place;
        gtk_box_pack_start(GTK_BOX(box), icon("xopp-book"), false, false, 0);
    }
    // the name, and after it, fainter and smaller, where it is. One label ellipsizes both, and asks for one character
    // only, so that a long name does not widen the box
    std::string text = markupOf(name, query, markColor);
    if (!small.empty()) {
        text += "  <span alpha=\"60%\" size=\"smaller\">· " + markupOf(small, query, markColor) + "</span>";
    }
    GtkWidget* l = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(l), text.c_str());
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
    gtk_box_pack_start(GTK_BOX(box), l, true, true, 0);
    if (!shortcut.empty()) {
        GtkWidget* k = gtk_label_new(shortcut.c_str());
        addClass(k, "amb-cmd-key");
        gtk_box_pack_end(GTK_BOX(box), k, false, false, 0);
    }
    if (checked) {
        gtk_box_pack_end(GTK_BOX(box), icon("xopp-check"), false, false, 0);
    }

    GtkWidget* row = gtk_list_box_row_new();
    addClass(row, "amb-cmd-item");
    gtk_widget_set_can_focus(row, false);
    gtk_container_add(GTK_CONTAINER(row), box);
    g_object_set_data(G_OBJECT(row), "amb-section", GINT_TO_POINTER(static_cast<int>(r.section) + 1));
    // the mouse moves the choice, as in the prototype; a row that comes under a still mouse (the list scrolled by the
    // keys) does not
    gtk_widget_add_events(row, GDK_POINTER_MOTION_MASK);
    g_signal_connect(row, "motion-notify-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventMotion*, CommandPalette* self) -> gboolean {
                         if (gtk_list_box_get_selected_row(GTK_LIST_BOX(self->list)) != GTK_LIST_BOX_ROW(w)) {
                             gtk_list_box_select_row(GTK_LIST_BOX(self->list), GTK_LIST_BOX_ROW(w));
                         }
                         return FALSE;
                     }),
                     this);
    return row;
}

void CommandPalette::refresh() {
    GList* rows = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList* r = rows; r; r = r->next) {
        gtk_widget_destroy(GTK_WIDGET(r->data));
    }
    g_list_free(rows);

    shownQuery = gtk_entry_get_text(GTK_ENTRY(entry));
    gtk_widget_set_visible(hint, shownQuery.empty());
    std::vector<Candidate> cmdCandidates, bookCandidates;
    for (auto const& c: commands) {
        if (!besideMode) {  // the box that opens beside shows the notebooks only
            cmdCandidates.push_back({c.label, c.path});
        }
    }
    for (auto const& b: books) {
        bookCandidates.push_back({b.name, b.place});
    }
    shown = arrangePalette(cmdCandidates, bookCandidates, recentBooks, shownQuery);
    for (auto const& r: shown) {
        gtk_container_add(GTK_CONTAINER(list), makeRow(r, shownQuery));
    }
    gtk_widget_show_all(list);
    gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller)), 0);

    gtk_widget_set_visible(scroller, !shown.empty());
    if (shown.empty()) {
        gchar* text = g_strdup_printf(_("Nothing with “%s”."), shownQuery.c_str());
        gtk_label_set_text(GTK_LABEL(empty), text);
        g_free(text);
    }
    gtk_widget_set_visible(empty, shown.empty());
    if (focusBack) {
        gtk_widget_queue_draw(focusBack);  // the box may have shrunk
    }
    select(0);
}

void CommandPalette::select(int index) {
    if (GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), index)) {
        gtk_list_box_select_row(GTK_LIST_BOX(list), row);
    }
}

void CommandPalette::move(int delta) {
    const int n = static_cast<int>(shown.size());
    if (n == 0) {
        return;
    }
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
    const int at = std::clamp((row ? gtk_list_box_row_get_index(row) : 0) + delta, 0, n - 1);
    select(at);
    scrollTo(gtk_list_box_get_selected_row(GTK_LIST_BOX(list)));
}

/// The chosen row stays in sight, with the title of its section when it is the first of it
void CommandPalette::scrollTo(GtkListBoxRow* row) {
    if (!row) {
        return;
    }
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller));
    int x = 0, top = 0;
    if (!gtk_widget_translate_coordinates(GTK_WIDGET(row), list, 0, 0, &x, &top)) {
        return;
    }
    int bottom = top + gtk_widget_get_allocated_height(GTK_WIDGET(row));
    if (GtkWidget* header = gtk_list_box_row_get_header(row)) {
        top -= gtk_widget_get_allocated_height(header);
    }
    if (top < gtk_adjustment_get_value(adj)) {
        gtk_adjustment_set_value(adj, top);
    } else if (bottom > gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj)) {
        gtk_adjustment_set_value(adj, bottom - gtk_adjustment_get_page_size(adj));
    }
}

void CommandPalette::runSelected(bool beside) {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
    if (!row) {
        return;
    }
    const auto at = static_cast<size_t>(gtk_list_box_row_get_index(row));
    if (at >= shown.size()) {
        return;
    }
    const PaletteRow chosen = shown[at];
    // closed first: what it runs may open a dialog or another tab
    if (chosen.section == PaletteSection::Commands) {
        const Command c = commands[chosen.index];
        close();
        std::string name;
        if (GActionGroup* group = groupOf(c.action, name)) {
            g_action_group_activate_action(group, name.c_str(), c.target.get());
        }
    } else {
        const fs::path path = books[chosen.index].path;
        close();
        if ((beside || besideMode) && notebooks.openBeside) {
            notebooks.openBeside(path);
        } else if (notebooks.open) {
            notebooks.open(path);
        }
    }
}

}  // namespace ambitus
