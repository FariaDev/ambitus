#include "SidebarPreviewPages.h"

#include <algorithm>  // for max
#include <map>        // for map
#include <memory>     // for uniqu...
#include <utility>    // for pair

#include <glib-object.h>  // for g_obj...

#include "control/Control.h"                                    // for Control
#include "control/settings/Settings.h"                          // for Settings
#include "gui/MainWindow.h"                                     // for MainWindow
#include "gui/sidebar/previews/base/SidebarPreviewBaseEntry.h"  // for Sideb...
#include "model/Document.h"                                     // for Document
#include "model/PageRef.h"                                      // for PageRef
#include "util/Assert.h"                                        // for xoj_assert
#include "util/Util.h"                                          // for npos
#include "util/gtk4_helper.h"
#include "util/i18n.h"        // for _
#include "util/safe_casts.h"  // for as_signed

#include "SidebarPreviewPageEntry.h"  // for Sideb...

constexpr auto MENU_ID = "PreviewPagesContextMenu";
constexpr auto STUDIO_MENU_ID = "PreviewPagesStudioContextMenu";
constexpr auto TOOLBAR_ID = "PreviewPagesToolbar";

SidebarPreviewPages::SidebarPreviewPages(Control* control):
        SidebarPreviewBase(control, MENU_ID, TOOLBAR_ID), iconNameHelper(control->getSettings()) {
    // Ambitus: the tile of the Studio column after the last miniature; it appends a page like the "+ Page" of the bar
    GtkWidget* tile = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(tile), "amb-add-page");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* plus = gtk_label_new("+");
    gtk_style_context_add_class(gtk_widget_get_style_context(plus), "amb-add-page-plus");
    GtkWidget* text = gtk_label_new(_("Page"));
    gtk_style_context_add_class(gtk_widget_get_style_context(text), "amb-add-page-text");
    gtk_box_pack_start(GTK_BOX(box), plus, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), text, false, false, 0);
    gtk_container_add(GTK_CONTAINER(tile), box);
    gtk_widget_show_all(box);
    gtk_widget_set_tooltip_text(tile, _("Add a page after the last one"));
    gtk_widget_set_can_focus(tile, false);
    g_signal_connect(tile, "clicked", G_CALLBACK(+[](GtkButton*, gpointer self) {
                         static_cast<SidebarPreviewPages*>(self)->control->getWindow()->appendPageAtEnd();
                     }),
                     this);
    gtk_widget_set_no_show_all(tile, true);
    gtk_widget_set_visible(tile, false);
    gtk_fixed_put(this->miniaturesContainer.get(), tile, 0, 0);
    this->endWidget = tile;  // the miniatures container owns it from now on
}

SidebarPreviewPages::~SidebarPreviewPages() = default;

void SidebarPreviewPages::enableSidebar() {
    SidebarPreviewBase::enableSidebar();

    pageSelected(this->selectedEntry);
}

void SidebarPreviewPages::setStudioColumn(bool studio) {
    if (studio == studioColumn) {
        return;
    }
    studioColumn = studio;

    if (toolbarWidget) {
        gtk_widget_set_visible(toolbarWidget, !studio);
    }
    setContextMenu(studio ? STUDIO_MENU_ID : MENU_ID, studio ? "win.delete-page" : nullptr);
    gtk_widget_set_visible(endWidget, studio);

    // the size of the miniatures depends on the numbers below them: they are made again
    selectedEntry = npos;
    updatePreviews();
    pageSelected(getControl()->getCurrentPageNo());
}

bool SidebarPreviewPages::isStudioColumn() const { return studioColumn; }

bool SidebarPreviewPages::numbersBelow() {
    return studioColumn ||
           getControl()->getSettings()->getSidebarNumberingStyle() == SidebarNumberingStyle::NUMBER_BELOW_PREVIEW;
}

auto SidebarPreviewPages::getName() -> std::string { return _("Page Preview"); }

auto SidebarPreviewPages::getIconName() -> std::string { return this->iconNameHelper.iconName("sidebar-page-preview"); }

void SidebarPreviewPages::updatePreviews() {
    this->previews.clear();

    Document* doc = this->getControl()->getDocument();
    doc->lock_shared();
    size_t len = doc->getPageCount();
    for (size_t i = 0; i < len; i++) {
        auto p = std::make_unique<SidebarPreviewPageEntry>(this, doc->getPage(i), i);
        gtk_fixed_put(this->miniaturesContainer.get(), p->getWidget(), 0, 0);
        this->previews.emplace_back(std::move(p));
    }
    doc->unlock_shared();

    layout();
}

void SidebarPreviewPages::pageSizeChanged(size_t page) {
    if (page == npos || page >= this->previews.size()) {
        return;
    }
    auto& p = this->previews[page];
    p->updateSize();
    p->repaint();

    layout();
}

void SidebarPreviewPages::pageChanged(size_t page) {
    if (page == npos || page >= this->previews.size()) {
        return;
    }

    auto& p = this->previews[page];
    p->repaint();
}

void SidebarPreviewPages::pageDeleted(size_t page) {
    if (page >= previews.size()) {
        return;
    }

    previews.erase(previews.begin() + as_signed(page));

    // Unselect page, to prevent double selection displaying
    unselectPage();

    updateIndices();

    layout();
}

void SidebarPreviewPages::pageInserted(size_t page) {
    Document* doc = control->getDocument();
    doc->lock_shared();
    auto p = std::make_unique<SidebarPreviewPageEntry>(this, doc->getPage(page), page);
    doc->unlock_shared();

    gtk_fixed_put(this->miniaturesContainer.get(), p->getWidget(), 0, 0);
    this->previews.insert(this->previews.begin() + as_signed(page), std::move(p));

    // Unselect page, to prevent double selection displaying
    unselectPage();

    updateIndices();

    layout();
}

/**
 * Unselect the last selected page, if any
 */
void SidebarPreviewPages::unselectPage() {
    for (auto& p: this->previews) {
        p->setSelected(false);
    }
}

void SidebarPreviewPages::pageSelected(size_t page) {
    if (this->selectedEntry != npos && this->selectedEntry < this->previews.size()) {
        this->previews[this->selectedEntry]->setSelected(false);
    }
    this->selectedEntry = page;

    if (!this->enabled) {
        return;
    }

    if (this->selectedEntry != npos && this->selectedEntry < this->previews.size()) {
        auto& p = this->previews[this->selectedEntry];
        p->setSelected(true);
        scrollToPreview(this);
    }
}

void SidebarPreviewPages::updateIndices() {
    size_t index = 0;
    for (auto& preview: this->previews) {
        dynamic_cast<SidebarPreviewPageEntry*>(preview.get())->setIndex(index++);
    }
}
