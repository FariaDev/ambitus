#include "PageMenu.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "ambitus/SendPage.h"
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

PageMenu::PageMenu(Control* control, XournalView* xournal, GtkOverlay* overlay):
        control(control),
        xournal(xournal),
        overlay(overlay),
        // when it goes away, the keyboard goes back to the page
        menu([overlay] { return GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(overlay))); },
             [xournal] { gtk_widget_grab_focus(xournal->getWidget()); }) {}

PageMenu::~PageMenu() = default;

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

void PageMenu::open(const InputEvent& e) {
    close();

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

    GtkWidget* box = menu.begin(GTK_WIDGET(overlay), GTK_POS_BOTTOM, WIDTH);
    gtk_popover_set_pointing_to(menu.popover(), &at);

    // the arrange-selection-order action takes the EditSelection::OrderChange as a uint64: 0 is to the front, 3 is to
    // the back (the same as the selection bar)
    if (selection) {
        GtkWidget* convert = menu.addItem(box, "xopp-convert-text", _("Convert to text"), "convert-to-text", nullptr);
        if (!hasInk()) {
            gtk_widget_set_sensitive(convert, false);  // only handwriting can be converted
        }
        menu.addItem(box, "xopp-send", _("Send to Summa"), "send-page-to-summa", nullptr);
        menu.addItem(box, nullptr, sendOtherPartLabel(control).c_str(), "send-other-part-to-summa", nullptr);
        menu.addSeparator(box);
        menu.addItem(box, "xopp-edit-cut", C_("Page menu", "Cut"), "cut", nullptr);
        menu.addItem(box, "xopp-edit-copy", C_("Page menu", "Copy"), "copy", nullptr);
        menu.addSubmenuItem(box, "xopp-layers", C_("Page menu", "Arrange"), [this](GtkWidget* sub) {
            menu.addItem(sub, "xopp-bring-to-front", C_("Selection bar", "Bring to front"), "arrange-selection-order",
                         g_variant_new_uint64(0));
            menu.addItem(sub, "xopp-send-to-back", C_("Selection bar", "Send to back"), "arrange-selection-order",
                         g_variant_new_uint64(3));
        });
        menu.addSeparator(box);
        menu.addItem(box, "xopp-trash-danger", C_("Selection bar", "Delete"), "delete", nullptr, true);
    } else {
        menu.addItem(box, "xopp-edit-paste", C_("Page menu", "Paste"), "paste", nullptr);
        // Xournal++ inserts an image with its tool: it arms it, and a rectangle dragged on the page picks the file
        menu.addItem(box, "xopp-tool-image", C_("Page menu", "Insert image…"), "select-tool", makeGVariant(TOOL_IMAGE));
        menu.addSeparator(box);
        menu.addItem(box, "xopp-file-plus", C_("Page menu", "New page below"), "new-page-after", nullptr);
        menu.addSubmenuItem(box, "xopp-square", C_("Page menu", "Paper of this page"), [this](GtkWidget* sub) {
            // the types of paper the Journal menu has (plain, ruled, graph, dotted...), the current one with a check
            auto current = control->getCurrentPage()->getBackgroundType();
            guint64 index = 0;
            for (auto const& info: control->getPageTypes()->getPageTypes()) {
                menu.addItem(sub, info->page == current ? "xopp-check" : nullptr, info->name.c_str(),
                             "menu.pick-page-type", g_variant_new_uint64(index++));
            }
        });
        menu.addItem(box, "xopp-vertical-space", C_("Page menu", "Vertical space"), "select-tool",
                     makeGVariant(TOOL_VERTICAL_SPACE));
        menu.addSeparator(box);
        menu.addItem(box, "xopp-send", C_("Page menu", "Send page to Summa"), "send-page-to-summa", nullptr);
        menu.addItem(box, nullptr, sendOtherPartLabel(control).c_str(), "send-other-part-to-summa", nullptr);
        menu.addItem(box, "xopp-trash-danger", C_("Page menu", "Delete page"), "delete-page", nullptr, true);
    }
    menu.popup();
}

void PageMenu::close() { menu.close(); }

}  // namespace ambitus
