#include "PopupMenu.h"

#include <climits>
#include <utility>
#include <vector>

#include "ambitus/Commands.h"

namespace ambitus {

namespace {
constexpr int ICON_PX = 16;
constexpr int CHEVRON_PX = 14;
constexpr const char* ITEM_CLASS = "amb-selpop-item";
constexpr const char* ACTIVE_CLASS = "amb-selpop-active";  ///< the item the keyboard (or the pointer) is on

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }
bool hasClass(GtkWidget* w, const char* c) { return gtk_style_context_has_class(gtk_widget_get_style_context(w), c); }

/// The icon, or an empty space of its size when there is none (the rows of a submenu line up with the checked one)
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

/// The items of the column that can be chosen, in order
std::vector<GtkWidget*> choosable(GtkWidget* column) {
    std::vector<GtkWidget*> out;
    GList* children = gtk_container_get_children(GTK_CONTAINER(column));
    for (GList* l = children; l; l = l->next) {
        auto* w = GTK_WIDGET(l->data);
        if (hasClass(w, ITEM_CLASS) && gtk_widget_get_sensitive(w)) {
            out.push_back(w);
        }
    }
    g_list_free(children);
    return out;
}

GtkWidget* columnOf(GtkWidget* popover) { return gtk_bin_get_child(GTK_BIN(popover)); }
}  // namespace

/// What an item does when it is clicked: the action and its target, which the item owns
struct PopupMenu::Item {
    PopupMenu* self;
    std::string action;
    GVariant* target;
};

PopupMenu::PopupMenu(std::function<GtkWindow*()> window, std::function<void()> onClosed):
        getWindow(std::move(window)), onClosed(std::move(onClosed)) {}

PopupMenu::~PopupMenu() {
    if (pop) {
        gtk_widget_destroy(pop);
    }
}

GtkWindow* PopupMenu::window() const { return getWindow(); }

void PopupMenu::setFallbackAccels(GMenuModel* menu) {
    fallbackAccels.clear();
    for (auto const& c: collectCommands(menu)) {
        if (c.accel.empty()) {
            continue;
        }
        gchar* detailed = g_action_print_detailed_name(c.action.c_str(), c.target.get());
        fallbackAccels.emplace(detailed, c.accel);
        g_free(detailed);
    }
}

GActionGroup* PopupMenu::groupOf(const std::string& action, std::string& name) const {
    GtkWindow* win = window();
    if (action.rfind("app.", 0) == 0) {
        name = action.substr(4);
        GtkApplication* app = GTK_IS_WINDOW(win) ? gtk_window_get_application(win) : nullptr;
        return app ? G_ACTION_GROUP(app) : nullptr;
    }
    name = action;
    return GTK_IS_WINDOW(win) ? G_ACTION_GROUP(win) : nullptr;
}

std::string PopupMenu::shortcutOf(const char* action, GVariant* target) const {
    GtkWindow* win = window();
    GtkApplication* app = GTK_IS_WINDOW(win) ? gtk_window_get_application(win) : nullptr;
    if (!app) {
        return "";
    }
    std::string name = std::string(g_str_has_prefix(action, "app.") ? "" : "win.") + action;
    gchar* detailed = g_action_print_detailed_name(name.c_str(), target);
    gchar** accels = gtk_application_get_accels_for_action(app, detailed);
    std::string accel;
    if (accels && accels[0]) {
        accel = accels[0];
    } else if (auto it = fallbackAccels.find(detailed); it != fallbackAccels.end()) {
        accel = it->second;  // the menu file, when the application has none for it
    }
    g_strfreev(accels);
    g_free(detailed);

    guint key = 0;
    GdkModifierType mods = GdkModifierType(0);
    if (accel.empty()) {
        return "";
    }
    gtk_accelerator_parse(accel.c_str(), &key, &mods);
    if (key == 0) {
        return "";
    }
    gchar* label = gtk_accelerator_get_label(key, mods);
    std::string out = label;
    g_free(label);
    return out;
}

GtkWidget* PopupMenu::newPopover(GtkWidget* relativeTo, GtkPositionType position) {
    GtkWidget* p = gtk_popover_new(relativeTo);
    gtk_popover_set_position(GTK_POPOVER(p), position);
    addClass(p, "amb-selpop");
    addClass(p, "amb-ctxpop");
    g_signal_connect(p, "key-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventKey* e, PopupMenu* self) -> gboolean {
                         return self->onKey(w, e);
                     }),
                     this);
    return p;
}

GtkWidget* PopupMenu::begin(GtkWidget* relativeTo, GtkPositionType position, int width) {
    if (pop) {
        gtk_widget_destroy(pop);  // also takes the submenu, which hangs from one of its items
        pop = nullptr;
        submenu = nullptr;
    }
    pop = newPopover(relativeTo, position);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(box, width, -1);
    gtk_container_add(GTK_CONTAINER(pop), box);

    // when it goes away, the submenu goes first and the owner gets to put the keyboard back where it was
    g_signal_connect_swapped(pop, "closed", G_CALLBACK(+[](PopupMenu* self) {
                                 if (self->submenu) {
                                     gtk_popover_popdown(GTK_POPOVER(self->submenu));
                                 }
                                 self->setActive(columnOf(self->pop), nullptr);
                                 if (self->onClosed) {
                                     self->onClosed();
                                 }
                             }),
                             this);
    return box;
}

void PopupMenu::addSeparator(GtkWidget* box) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    addClass(sep, "amb-selpop-sep");
    gtk_box_pack_start(GTK_BOX(box), sep, false, false, 0);
}

GtkWidget* PopupMenu::addItem(GtkWidget* box, const char* iconName, const char* text, const char* action,
                              GVariant* target, bool danger) {
    GtkWidget* b = gtk_button_new();
    gtk_widget_set_can_focus(b, false);
    addClass(b, ITEM_CLASS);
    if (danger) {
        addClass(b, "amb-selpop-danger");
    }
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(row), icon(iconName), false, false, 0);
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
    if (std::string key = shortcutOf(action, target); !key.empty()) {
        GtkWidget* k = gtk_label_new(key.c_str());
        addClass(k, "amb-selpop-key");
        gtk_box_pack_end(GTK_BOX(row), k, false, false, 0);
    }
    gtk_container_add(GTK_CONTAINER(b), row);

    // an action that is disabled (or that does not exist) gives a faded item that does nothing
    std::string name;
    GActionGroup* group = groupOf(action, name);
    gtk_widget_set_sensitive(b, group && g_action_group_has_action(group, name.c_str()) &&
                                        g_action_group_get_action_enabled(group, name.c_str()));

    if (target) {
        g_variant_ref_sink(target);
    }
    auto* item = new Item{this, action, target};
    g_object_set_data_full(
            G_OBJECT(b), "amb-item", item, +[](gpointer p) {
                auto* it = static_cast<Item*>(p);
                if (it->target) {
                    g_variant_unref(it->target);
                }
                delete it;
            });
    g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton*, Item* it) {
                         // the menu goes first, so that an action that opens a dialog does not find it still up
                         it->self->close();
                         std::string name;
                         if (GActionGroup* group = it->self->groupOf(it->action, name)) {
                             g_action_group_activate_action(group, name.c_str(), it->target);
                         }
                     }),
                     item);
    g_signal_connect(b, "enter-notify-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventCrossing*, PopupMenu* self) -> gboolean {
                         self->setActive(gtk_widget_get_parent(w), w);  // one highlight: the pointer takes it
                         return false;
                     }),
                     this);
    gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    return b;
}

GtkWidget* PopupMenu::addCallItem(GtkWidget* box, const char* iconName, const char* text, std::function<void()> run,
                                  const char* key) {
    GtkWidget* b = gtk_button_new();
    gtk_widget_set_can_focus(b, false);
    addClass(b, ITEM_CLASS);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(row), icon(iconName), false, false, 0);
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
    if (key && *key) {
        GtkWidget* k = gtk_label_new(key);
        addClass(k, "amb-selpop-key");
        gtk_box_pack_end(GTK_BOX(row), k, false, false, 0);
    }
    gtk_container_add(GTK_CONTAINER(b), row);

    auto* action = new std::function<void()>(std::move(run));
    g_object_set_data_full(
            G_OBJECT(b), "amb-run", action, +[](gpointer p) { delete static_cast<std::function<void()>*>(p); });
    g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton* button, PopupMenu* self) {
                         // the menu goes first; the function is copied, as running it may rebuild the menu
                         self->close();
                         auto* fn = static_cast<std::function<void()>*>(g_object_get_data(G_OBJECT(button), "amb-run"));
                         if (fn && *fn) {
                             auto run = *fn;
                             run();
                         }
                     }),
                     this);
    g_signal_connect(b, "enter-notify-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventCrossing*, PopupMenu* self) -> gboolean {
                         self->setActive(gtk_widget_get_parent(w), w);
                         return false;
                     }),
                     this);
    gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    return b;
}

void PopupMenu::addSubmenuItem(GtkWidget* box, const char* iconName, const char* text,
                               const std::function<void(GtkWidget*)>& fill) {
    GtkWidget* b = gtk_button_new();
    gtk_widget_set_can_focus(b, false);
    addClass(b, ITEM_CLASS);
    addClass(b, "amb-selpop-sub");
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(row), icon(iconName), false, false, 0);
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
    gtk_box_pack_end(GTK_BOX(row), icon("xopp-chevron-right", CHEVRON_PX), false, false, 0);
    gtk_container_add(GTK_CONTAINER(b), row);
    gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    g_signal_connect(b, "enter-notify-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventCrossing*, PopupMenu* self) -> gboolean {
                         self->setActive(gtk_widget_get_parent(w), w);
                         return false;
                     }),
                     this);

    submenu = newPopover(b, GTK_POS_RIGHT);
    GtkWidget* sub = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(submenu), sub);
    fill(sub);
    gtk_widget_show_all(sub);
    // opened with the keyboard it starts on its first item
    g_signal_connect(submenu, "map", G_CALLBACK(+[](GtkWidget* w, PopupMenu* self) {
                         auto items = choosable(columnOf(w));
                         self->setActive(columnOf(w), items.empty() ? nullptr : items.front());
                     }),
                     this);
    g_signal_connect_swapped(b, "clicked", G_CALLBACK(gtk_popover_popup), submenu);
}

void PopupMenu::popup() {
    if (pop) {
        gtk_widget_show_all(columnOf(pop));
        gtk_popover_popup(GTK_POPOVER(pop));
    }
}

void PopupMenu::close() {
    if (submenu) {
        gtk_popover_popdown(GTK_POPOVER(submenu));
    }
    if (pop) {
        gtk_popover_popdown(GTK_POPOVER(pop));
    }
}

void PopupMenu::setActive(GtkWidget* column, GtkWidget* item) {
    if (!column) {
        return;
    }
    GList* children = gtk_container_get_children(GTK_CONTAINER(column));
    for (GList* l = children; l; l = l->next) {
        auto* w = GTK_WIDGET(l->data);
        if (w != item) {
            gtk_style_context_remove_class(gtk_widget_get_style_context(w), ACTIVE_CLASS);
        }
    }
    g_list_free(children);
    if (item) {
        addClass(item, ACTIVE_CLASS);
    }
}

void PopupMenu::moveActive(GtkWidget* popover, int step) {
    GtkWidget* column = columnOf(popover);
    auto items = choosable(column);
    if (items.empty()) {
        return;
    }
    int current = -1;
    for (size_t i = 0; i < items.size(); i++) {
        if (hasClass(items[i], ACTIVE_CLASS)) {
            current = static_cast<int>(i);
        }
    }
    const int n = static_cast<int>(items.size());
    int next;
    if (step == INT_MIN) {
        next = 0;  // Home
    } else if (step == INT_MAX) {
        next = n - 1;  // End
    } else if (current < 0) {
        next = step > 0 ? 0 : n - 1;
    } else {
        next = (current + step + n) % n;  // goes round
    }
    setActive(column, items[static_cast<size_t>(next)]);
}

bool PopupMenu::onKey(GtkWidget* popover, GdkEventKey* e) {
    GtkWidget* column = columnOf(popover);
    GtkWidget* active = nullptr;
    for (GtkWidget* w: choosable(column)) {
        if (hasClass(w, ACTIVE_CLASS)) {
            active = w;
        }
    }
    switch (e->keyval) {
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            moveActive(popover, 1);
            return true;
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            moveActive(popover, -1);
            return true;
        case GDK_KEY_Home:
            moveActive(popover, INT_MIN);
            return true;
        case GDK_KEY_End:
            moveActive(popover, INT_MAX);
            return true;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_ISO_Enter:
        case GDK_KEY_space:
            if (active) {
                gtk_button_clicked(GTK_BUTTON(active));
            }
            return true;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            if (active && hasClass(active, "amb-selpop-sub")) {
                gtk_button_clicked(GTK_BUTTON(active));
            }
            return true;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            if (popover == submenu) {
                gtk_popover_popdown(GTK_POPOVER(submenu));
            }
            return true;
        default:
            return false;  // Esc is the popover's own
    }
}

}  // namespace ambitus
