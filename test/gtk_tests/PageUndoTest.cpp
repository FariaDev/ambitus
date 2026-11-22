#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "control/Control.h"
#include "control/actions/ActionDatabase.h"
#include "gui/GladeSearchpath.h"
#include "gui/MainWindow.h"
#include "model/Document.h"

#include "config-test.h"
#include "dialog/GtkTest.h"

class PageUndoTest: public GtkTest {
    void runTest(GtkApplication* app) override;
};

void PageUndoTest::runTest(GtkApplication* app) {
    GladeSearchpath paths;
    paths.addSearchDirectory(GET_UI_FOLDER);
    paths.addSearchDirectory(XOJ_TEST_RESOURCE_DIR);
    Control control(G_APPLICATION(app), &paths);
    MainWindow window(&paths, &control, app);
    control.initWindow(&window, true);
    window.populate(&paths);
    control.newFile();
    bool ready = false;
    g_idle_add(
            +[](gpointer data) -> gboolean {
                *static_cast<bool*>(data) = true;
                return G_SOURCE_REMOVE;
            },
            &ready);
    while (!ready) {
        g_main_context_iteration(nullptr, true);
    }

    // In the default Portrait toolbar, the footer begins with PAGE_SPIN.
    auto* item = gtk_toolbar_get_nth_item(GTK_TOOLBAR(window.get("tbBottom1")), 0);
    ASSERT_NE(item, nullptr);
    auto* children = gtk_container_get_children(GTK_CONTAINER(gtk_bin_get_child(GTK_BIN(item))));
    auto* spinner = g_list_nth_data(children, 1);
    g_list_free(children);
    ASSERT_TRUE(GTK_IS_SPIN_BUTTON(spinner));
    auto* adjustment = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spinner));

    auto* actions = control.getActionDatabase();
    ASSERT_EQ(control.getDocument()->getPageCount(), 1);
    ASSERT_EQ(gtk_adjustment_get_upper(adjustment), 1);
    actions->fireActivateAction(Action::NEW_PAGE_AFTER);
    actions->fireActivateAction(Action::GOTO_FIRST);
    ASSERT_EQ(control.getDocument()->getPageCount(), 2);
    ASSERT_EQ(control.getCurrentPageNo(), 0);
    ASSERT_EQ(gtk_adjustment_get_upper(adjustment), 2);
    ASSERT_TRUE(actions->isActionEnabled(Action::GOTO_NEXT));
    ASSERT_TRUE(actions->isActionEnabled(Action::GOTO_LAST));

    actions->fireActivateAction(Action::UNDO);
    EXPECT_EQ(control.getDocument()->getPageCount(), 1);
    EXPECT_EQ(control.getCurrentPageNo(), 0);
    EXPECT_EQ(gtk_adjustment_get_upper(adjustment), 1);
    EXPECT_FALSE(actions->isActionEnabled(Action::GOTO_NEXT));
    EXPECT_FALSE(actions->isActionEnabled(Action::GOTO_LAST));
    EXPECT_FALSE(actions->isActionEnabled(Action::DELETE_PAGE));
}

TEST_F(PageUndoTest, UndoingPageInsertionRefreshesFooterAndActions) {}
