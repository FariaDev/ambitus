#include "PageMenu.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "control/Control.h"
#include "control/ToolEnums.h"
#include "control/pagetype/PageTypeHandler.h"
#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "gui/Layout.h"
#include "gui/PageView.h"
#include "gui/XournalView.h"
#include "model/Element.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "util/GVariantTemplate.h"
#include "util/i18n.h"

#include "TitleBar.h"

namespace ambitus {

namespace {
constexpr int ICON_PX = 16;
constexpr int CHEVRON_PX = 14;

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

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
}  // namespace

/// What an item does when it is clicked: the action of the window and its target, which the item owns
struct PageMenu::Item {
    PageMenu* self;
    std::string action;
    GVariant* target;
};

PageMenu::PageMenu(Control* control, XournalView* xournal, GtkOverlay* overlay):
        control(control), xournal(xournal), overlay(overlay) {}

PageMenu::~PageMenu() {
    if (popover) {
        gtk_widget_destroy(popover);
    }
}

GtkWindow* PageMenu::window() const { return GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(overlay))); }

bool PageMenu::enabled() const {
    // a board is always in the Studio layout, whatever the setting says
    if (control->isBoardOpen()) {
        return true;
    }
    return control->getSettings()->getEditorMode() != static_cast<int>(EditorMode::CLASSIC);
}

bool PageMenu::hasInk() const {
    EditSelection* s = xournal->getSelection();
    if (!s) {
        return false;
    }
    for (const Element* e: s->getElementsView()) {
        if (e->getType() == ELEMENT_STROKE && static_cast<const Stroke*>(e)->getToolType() == StrokeTool::PEN) {
            return true;
        }
    }
    return false;
}

std::string PageMenu::shortcutOf(const char* action, GVariant* target) const {
    GtkWindow* win = window();
    GtkApplication* app = GTK_IS_WINDOW(win) ? gtk_window_get_application(win) : nullptr;
    if (!app) {
        return "";
    }
    std::string name = std::string("win.") + action;
    gchar* detailed = g_action_print_detailed_name(name.c_str(), target);
    gchar** accels = gtk_application_get_accels_for_action(app, detailed);
    std::string out;
    if (accels && accels[0]) {
        guint key = 0;
        GdkModifierType mods = GdkModifierType(0);
        gtk_accelerator_parse(accels[0], &key, &mods);
        if (key != 0) {
            gchar* label = gtk_accelerator_get_label(key, mods);
            out = label;
            g_free(label);
        }
    }
    g_strfreev(accels);
    g_free(detailed);
    return out;
}

GtkWidget* PageMenu::newPopover(GtkWidget* relativeTo, GtkPositionType position) {
    GtkWidget* pop = gtk_popover_new(relativeTo);
    gtk_popover_set_position(GTK_POPOVER(pop), position);
    addClass(pop, "amb-selpop");
    addClass(pop, "amb-ctxpop");
    return pop;
}

void PageMenu::addSeparator(GtkWidget* box) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    addClass(sep, "amb-selpop-sep");
    gtk_box_pack_start(GTK_BOX(box), sep, false, false, 0);
}

GtkWidget* PageMenu::addItem(GtkWidget* box, const char* iconName, const char* text, const char* action,
                             GVariant* target, bool danger) {
    GtkWidget* b = gtk_button_new();
    gtk_widget_set_can_focus(b, false);
    addClass(b, "amb-selpop-item");
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

    // an action that the window has disabled (or does not have) gives a faded item that does nothing
    auto* win = G_ACTION_GROUP(window());
    gtk_widget_set_sensitive(b,
                             g_action_group_has_action(win, action) && g_action_group_get_action_enabled(win, action));

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
                         g_action_group_activate_action(G_ACTION_GROUP(it->self->window()), it->action.c_str(),
                                                        it->target);
                     }),
                     item);
    gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    return b;
}

void PageMenu::addSubmenuItem(GtkWidget* box, const char* iconName, const char* text,
                              const std::function<void(GtkWidget*)>& fill) {
    GtkWidget* b = gtk_button_new();
    gtk_widget_set_can_focus(b, false);
    addClass(b, "amb-selpop-item");
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(row), icon(iconName), false, false, 0);
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
    gtk_box_pack_end(GTK_BOX(row), icon("xopp-chevron-right", CHEVRON_PX), false, false, 0);
    gtk_container_add(GTK_CONTAINER(b), row);
    gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);

    submenu = newPopover(b, GTK_POS_RIGHT);
    GtkWidget* sub = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(submenu), sub);
    fill(sub);
    gtk_widget_show_all(sub);
    g_signal_connect_swapped(b, "clicked", G_CALLBACK(gtk_popover_popup), submenu);
}

void PageMenu::open(const InputEvent& e) {
    close();
    if (popover) {
        gtk_widget_destroy(popover);  // also takes the submenu, which hangs from one of its items
        popover = nullptr;
        submenu = nullptr;
    }

    // overlay coordinates of the event (InputEvent::absolute is in the coordinates of the pages widget)
    int ox = 0, oy = 0;
    gtk_widget_translate_coordinates(xournal->getWidget(), GTK_WIDGET(overlay), 0, 0, &ox, &oy);
    GdkRectangle at = {static_cast<int>(std::lround(e.absolute.x)) + ox,
                       static_cast<int>(std::lround(e.absolute.y)) + oy, 1, 1};

    bool selection = xournal->getSelection() != nullptr;
    if (!selection) {
        // "this page" is the one under the pointer; the click did not reach the page, which would have selected it
        auto* view = xournal->getLayout()->getPageViewAt(static_cast<int>(std::lround(e.relative.x)),
                                                         static_cast<int>(std::lround(e.relative.y)));
        if (view && !view->isSelected()) {
            control->firePageSelected(view->getPage());
        }
    }

    popover = newPopover(GTK_WIDGET(overlay), GTK_POS_BOTTOM);
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &at);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(box, WIDTH, -1);
    gtk_container_add(GTK_CONTAINER(popover), box);

    // the arrange-selection-order action takes the EditSelection::OrderChange as a uint64: 0 is to the front, 3 is to
    // the back (the same as the selection bar)
    if (selection) {
        GtkWidget* convert = addItem(box, "xopp-convert-text", _("Convert to text"), "convert-to-text", nullptr);
        if (!hasInk()) {
            gtk_widget_set_sensitive(convert, false);  // only handwriting can be converted
        }
        addItem(box, "xopp-send", _("Send to Summa"), "send-page-to-summa", nullptr);
        addSeparator(box);
        addItem(box, "xopp-edit-cut", C_("Page menu", "Cut"), "cut", nullptr);
        addItem(box, "xopp-edit-copy", C_("Page menu", "Copy"), "copy", nullptr);
        addSubmenuItem(box, "xopp-layers", C_("Page menu", "Arrange"), [this](GtkWidget* sub) {
            addItem(sub, "xopp-bring-to-front", C_("Selection bar", "Bring to front"), "arrange-selection-order",
                    g_variant_new_uint64(0));
            addItem(sub, "xopp-send-to-back", C_("Selection bar", "Send to back"), "arrange-selection-order",
                    g_variant_new_uint64(3));
        });
        addSeparator(box);
        addItem(box, "xopp-trash-danger", C_("Selection bar", "Delete"), "delete", nullptr, true);
    } else {
        addItem(box, "xopp-edit-paste", C_("Page menu", "Paste"), "paste", nullptr);
        // Xournal++ inserts an image with its tool: it arms it, and a rectangle dragged on the page picks the file
        addItem(box, "xopp-tool-image", C_("Page menu", "Insert image…"), "select-tool", makeGVariant(TOOL_IMAGE));
        addSeparator(box);
        addItem(box, "xopp-file-plus", C_("Page menu", "New page below"), "new-page-after", nullptr);
        addSubmenuItem(box, "xopp-square", C_("Page menu", "Paper of this page"), [this](GtkWidget* sub) {
            // the types of paper the Journal menu has (plain, ruled, graph, dotted...), the current one with a check
            auto current = control->getCurrentPage()->getBackgroundType();
            guint64 index = 0;
            for (auto const& info: control->getPageTypes()->getPageTypes()) {
                addItem(sub, info->page == current ? "xopp-check" : nullptr, info->name.c_str(), "menu.pick-page-type",
                        g_variant_new_uint64(index++));
            }
        });
        addItem(box, "xopp-vertical-space", C_("Page menu", "Vertical space"), "select-tool",
                makeGVariant(TOOL_VERTICAL_SPACE));
        addSeparator(box);
        addItem(box, "xopp-send", C_("Page menu", "Send page to Summa"), "send-page-to-summa", nullptr);
        addItem(box, "xopp-trash-danger", C_("Page menu", "Delete page"), "delete-page", nullptr, true);
    }
    gtk_widget_show_all(box);

    // when it goes away, the keyboard goes back to the page
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(+[](PageMenu* self) {
                                 if (self->submenu) {
                                     gtk_popover_popdown(GTK_POPOVER(self->submenu));
                                 }
                                 gtk_widget_grab_focus(self->xournal->getWidget());
                             }),
                             this);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void PageMenu::close() {
    if (submenu) {
        gtk_popover_popdown(GTK_POPOVER(submenu));
    }
    if (popover) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
}

}  // namespace ambitus
