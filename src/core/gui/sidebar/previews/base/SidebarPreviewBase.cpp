#include "SidebarPreviewBase.h"

#include <cstdlib>  // for abs, size_t
#include <string>
#include <vector>

#include <glib-object.h>  // for g_object_ref, G_CALLBACK, g_sig...
#include <glib.h>         // for g_idle_add

#include "control/Control.h"   // for Control
#include "control/PdfCache.h"  // for PdfCache
#include "gui/Builder.h"       // for Builder
#include "gui/MainWindow.h"    // for MainWindow
#include "model/Document.h"    // for Document
#include "util/Util.h"         // for npos
#include "util/glib_casts.h"   // for wrap_for_once_v
#include "util/gtk4_helper.h"
#include "util/raii/GObjectSPtr.h"   // for GObjectSPtr
#include "util/raii/GVariantSPtr.h"  // for GVariantSPtr

#include "SidebarLayout.h"            // for SidebarLayout
#include "SidebarPreviewBaseEntry.h"  // for SidebarPreviewBaseEntry

constexpr auto XML_FILE = "sidebar.ui";

SidebarPreviewBase::SidebarPreviewBase(Control* control, const char* menuId, const char* toolbarId):
        AbstractSidebarPage(control),
        scrollableBox(gtk_scrolled_window_new(), xoj::util::adopt),
        mainBox(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), xoj::util::adopt),
        miniaturesContainer(GTK_FIXED(gtk_fixed_new()), xoj::util::adopt) {
    gtk_box_append(GTK_BOX(mainBox.get()), scrollableBox.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollableBox.get()), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrollableBox.get()), GTK_WIDGET(miniaturesContainer.get()));
    gtk_widget_set_vexpand(scrollableBox.get(), true);

    Document* doc = this->control->getDocument();
    doc->lock_shared();
    if (doc->getPdfPageCount() != 0) {
        this->cache = std::make_unique<PdfCache>(doc->getPdfDocument(), control->getSettings());
    }
    doc->unlock_shared();


    registerListener(this->control);
    this->control->addChangedDocumentListener(this);

    auto* adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrollableBox.get()));
    g_signal_connect(
            adj, "notify::page-size", G_CALLBACK(+[](GObject* adj, GParamSpec*, gpointer d) {
                static_cast<SidebarPreviewBase*>(d)->newWidth(gtk_adjustment_get_page_size(GTK_ADJUSTMENT(adj)));
            }),
            this);

    setContextMenu(menuId);

    Builder builder(control->getGladeSearchPath(), XML_FILE);
    toolbarWidget = builder.get(toolbarId);
    gtk_box_append(GTK_BOX(mainBox.get()), toolbarWidget);

    gtk_widget_show_all(mainBox.get());
}

/// Ambitus: the action of each item of a menu model, in the order GTK makes its menu items. GTK 3 keeps no action name
/// on those items, so they are matched by their place: the sections of the model give the separators (not items).
static void collectItemActions(GMenuModel* model, std::vector<std::string>& actions) {
    const int n = g_menu_model_get_n_items(model);
    for (int i = 0; i < n; ++i) {
        xoj::util::GObjectSPtr<GMenuLinkIter> links(g_menu_model_iterate_item_links(model, i), xoj::util::adopt);
        bool hasLinks = false;
        while (g_menu_link_iter_next(links.get())) {
            xoj::util::GObjectSPtr<GMenuModel> sub(g_menu_link_iter_get_value(links.get()), xoj::util::adopt);
            if (sub) {
                collectItemActions(sub.get(), actions);
                hasLinks = true;
            }
        }
        if (!hasLinks) {
            xoj::util::GVariantSPtr action(
                    g_menu_model_get_item_attribute_value(model, i, "action", G_VARIANT_TYPE_STRING), xoj::util::adopt);
            actions.emplace_back(action ? g_variant_get_string(action.get(), nullptr) : "");
        }
    }
}

void SidebarPreviewBase::setContextMenu(const char* menuId, const char* dangerAction) {
    Builder builder(control->getGladeSearchPath(), XML_FILE);
    GMenuModel* menu = G_MENU_MODEL(builder.getObject(menuId));
    contextMenu.reset(GTK_MENU(gtk_menu_new_from_model(menu)), xoj::util::adopt);
    gtk_menu_attach_to_widget(contextMenu.get(), mainBox.get(), nullptr);

    if (dangerAction) {
        // Ambitus: the item of the action is drawn in the danger colour (see the menu items of ambitus.css)
        std::vector<std::string> actions;
        collectItemActions(menu, actions);
        GList* items = gtk_container_get_children(GTK_CONTAINER(contextMenu.get()));
        size_t item = 0;
        for (GList* l = items; l != nullptr; l = l->next) {
            if (GTK_IS_SEPARATOR_MENU_ITEM(l->data)) {
                continue;
            }
            if (item < actions.size() && actions[item] == dangerAction) {
                gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(l->data)), "amb-danger");
            }
            item++;
        }
        g_list_free(items);
    }
}

SidebarPreviewBase::~SidebarPreviewBase() { this->control->removeChangedDocumentListener(this); }

void SidebarPreviewBase::enableSidebar() {
    if (!this->enabled) {
        enabled = true;
        layout();
    }
}

void SidebarPreviewBase::disableSidebar() { enabled = false; }

void SidebarPreviewBase::newWidth(double width) {
    static constexpr double TRIGGER = 20.;

    if (std::abs(lastWidth - width) > TRIGGER) {
        this->layout();
        lastWidth = width;
    }
}

auto SidebarPreviewBase::getZoom() const -> double { return this->zoom; }

auto SidebarPreviewBase::getCache() -> PdfCache* { return this->cache.get(); }

void SidebarPreviewBase::layout() {
    if (enabled) {
        SidebarLayout::layout(this);
    }
}

auto SidebarPreviewBase::hasData() -> bool { return true; }

auto SidebarPreviewBase::getWidget() -> GtkWidget* { return this->mainBox.get(); }

void SidebarPreviewBase::documentChanged(DocumentChangeType type) {
    if (type == DOCUMENT_CHANGE_COMPLETE || type == DOCUMENT_CHANGE_CLEARED) {
        this->cache.reset();

        Document* doc = control->getDocument();
        doc->lock_shared();
        if (doc->getPdfPageCount() != 0) {
            this->cache = std::make_unique<PdfCache>(doc->getPdfDocument(), control->getSettings());
        }
        doc->unlock_shared();
        updatePreviews();
    }
}

auto SidebarPreviewBase::scrollToPreview(SidebarPreviewBase* sidebar) -> bool {
    if (!sidebar->enabled) {
        return false;
    }

    MainWindow* win = sidebar->control->getWindow();
    if (win == nullptr) {
        return false;
    }

    GtkWidget* w = win->get("sidebar");
    if (!gtk_widget_get_visible(w)) {
        return false;
    }

    if (sidebar->selectedEntry != npos && sidebar->selectedEntry < sidebar->previews.size()) {
        auto& p = sidebar->previews[sidebar->selectedEntry];

        // scroll to preview
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sidebar->scrollableBox.get()));
        GtkWidget* widget = p->getWidget();

        GtkAllocation allocation;
        gtk_widget_get_allocation(widget, &allocation);
        int x = allocation.x;
        int y = allocation.y;

        if (x == -1) {
            g_idle_add(xoj::util::wrap_for_once_v<scrollToPreview>, sidebar);
            return false;
        }

        gtk_adjustment_clamp_page(vadj, y, y + allocation.height);
    }
    return false;
}

void SidebarPreviewBase::pageDeleted(size_t page) {}

void SidebarPreviewBase::pageInserted(size_t page) {}

void SidebarPreviewBase::openPreviewContextMenu(GdkEvent* currentEvent) {
    gtk_menu_popup_at_pointer(contextMenu.get(), currentEvent);
}
