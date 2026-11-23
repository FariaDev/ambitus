#include "config-features.h"

#ifdef ENABLE_PLUGINS

#include <fstream>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "control/Control.h"
#include "control/actions/ActionDatabase.h"
#include "control/settings/Settings.h"
#include "dialog/GtkTest.h"
#include "gui/GladeSearchpath.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "plugin/Plugin.h"
#include "util/PathUtil.h"

#include "config-test.h"

class LuaSetCurrentPageTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GladeSearchpath paths;
        paths.addSearchDirectory(GET_UI_FOLDER);
        paths.addSearchDirectory(XOJ_TEST_RESOURCE_DIR);
        Control control(G_APPLICATION(app), &paths);
        // An unmapped sidebar would keep retrying scrolls while waiting for its allocation.
        control.getSettings()->setSidebarVisible(false);
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
        // Let startup's asynchronous clipboard query finish before tearing down Control.
        auto* clipboard = gtk_widget_get_clipboard(window.getWindow(), GDK_SELECTION_CLIPBOARD);
        if (auto* targets = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("TARGETS"))) {
            gtk_selection_data_free(targets);
        }

        auto* actions = control.getActionDatabase();
        actions->fireActivateAction(Action::NEW_PAGE_AFTER);
        actions->fireActivateAction(Action::NEW_PAGE_AFTER);
        ASSERT_EQ(control.getDocument()->getPageCount(), 3);

        const auto directory = Util::getCacheSubfolder("lua-set-current-page-test");
        ASSERT_TRUE(std::ofstream(directory / "plugin.ini") << R"ini([default]
enabled=true
[plugin]
mainfile=main.lua
)ini");
        ASSERT_TRUE(std::ofstream(directory / "main.lua") << R"lua(
function setCurrentPage(page)
  app.setCurrentPage(page)
end
)lua");

        Plugin plugin(&control, "LuaSetCurrentPageTest", directory);
        ASSERT_TRUE(plugin.isValid());
        plugin.loadScript();
        ASSERT_TRUE(plugin.isValid());

        constexpr struct {
            ptrdiff_t requested;
            size_t expected;
        } cases[] = {{1, 0}, {0, 0}, {-1, 0}, {3, 2}, {99, 2}};
        for (const auto& [requested, expected]: cases) {
            SCOPED_TRACE(requested);
            control.firePageSelected(1);
            ASSERT_EQ(control.getCurrentPageNo(), 1);
            ASSERT_TRUE(plugin.callFunction("setCurrentPage", requested));
            EXPECT_EQ(control.getCurrentPageNo(), expected);
        }
        g_signal_handlers_disconnect_by_data(gtk_settings_get_default(), &window);
    }
};

TEST_F(LuaSetCurrentPageTest, ClampsToFirstAndLastPage) {}

#endif
