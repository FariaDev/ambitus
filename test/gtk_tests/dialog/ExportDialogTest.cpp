#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "gui/GladeSearchpath.h"
#include "gui/dialog/ExportDialog.h"

#include "GtkTest.h"
#include "config-test.h"

static GtkWidget* findWidget(GtkWidget* widget, const char* id) {
    if (g_strcmp0(gtk_buildable_get_name(GTK_BUILDABLE(widget)), id) == 0) {
        return widget;
    }
    if (GTK_IS_CONTAINER(widget)) {
        g_autoptr(GList) children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (auto* child = children; child; child = child->next) {
            if (auto* match = findWidget(GTK_WIDGET(child->data), id)) {
                return match;
            }
        }
    }
    return nullptr;
}

class ExportDialogTest: public GtkTest {
    void runTest(GtkApplication*) override {
        GladeSearchpath paths;
        paths.addSearchDirectory(GET_UI_FOLDER);
        xoj::popup::ExportDialog dialog(&paths, 1, 1, false, {});
        auto* window = GTK_WIDGET(dialog.getWindow());
        auto* all = findWidget(window, "rdRangeAll");
        auto* pages = findWidget(window, "rdRangePages");
        auto* entry = findWidget(window, "txtPages");
        auto* ok = findWidget(window, "btOk");
        auto* hint = findWidget(window, "lbPageRangeHint");
        auto* example = findWidget(window, "lbPageRangeExample");
        ASSERT_TRUE(GTK_IS_TOGGLE_BUTTON(all));
        ASSERT_TRUE(GTK_IS_TOGGLE_BUTTON(pages));
        ASSERT_TRUE(GTK_IS_ENTRY(entry));
        ASSERT_TRUE(GTK_IS_BUTTON(ok));
        ASSERT_TRUE(GTK_IS_LABEL(hint));
        ASSERT_TRUE(GTK_IS_LABEL(example));

        auto checkRangeError = [&](bool invalid) {
            EXPECT_EQ(bool(gtk_widget_get_visible(hint)), invalid);
            EXPECT_EQ(bool(gtk_widget_get_child_visible(example)), !invalid);
            EXPECT_EQ(bool(gtk_widget_get_sensitive(ok)), !invalid);
            EXPECT_EQ(bool(gtk_style_context_has_class(gtk_widget_get_style_context(entry), "error")), invalid);
        };

        checkRangeError(false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pages), true);
        gtk_entry_set_text(GTK_ENTRY(entry), "3");
        checkRangeError(true);
        EXPECT_STREQ(gtk_label_get_text(GTK_LABEL(hint)), "This document has one page. Enter 1.");

        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(all), true);
        checkRangeError(false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pages), true);
        checkRangeError(true);
        gtk_entry_set_text(GTK_ENTRY(entry), "1");
        checkRangeError(false);
    }
};

TEST_F(ExportDialogTest, ExplainsInvalidRangeInSinglePageDocument) {}
