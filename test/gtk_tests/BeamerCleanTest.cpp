#include "config-features.h"

#ifdef ENABLE_PLUGINS

#include <memory>

#include <cairo-pdf.h>
#include <gtest/gtest.h>

#include "control/Control.h"
#include "control/settings/Settings.h"
#include "dialog/GtkTest.h"
#include "gui/GladeSearchpath.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "plugin/Plugin.h"
#include "util/PathUtil.h"
#include "util/StringUtils.h"
#include "util/raii/CairoWrappers.h"

#include "config-test.h"

class BeamerCleanTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GladeSearchpath paths;
        paths.addSearchDirectory(GET_UI_FOLDER);
        paths.addSearchDirectory(XOJ_TEST_RESOURCE_DIR);
        Control control(G_APPLICATION(app), &paths);
        control.getSettings()->setSidebarVisible(false);
        MainWindow window(&paths, &control, app);
        control.initWindow(&window, true);
        window.populate(&paths);
        const auto directory = Util::getCacheSubfolder("beamer-clean-test");
        const auto background = directory / "background.pdf";
        {
            xoj::util::CairoSurfaceSPtr surface(
                    cairo_pdf_surface_create(char_cast(background.u8string().c_str()), 100, 100), xoj::util::adopt);
            xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
            cairo_show_page(cr.get());
            cairo_show_page(cr.get());
            cairo_surface_finish(surface.get());
            ASSERT_EQ(cairo_surface_status(surface.get()), CAIRO_STATUS_SUCCESS);
        }
        auto* doc = control.getDocument();
        ASSERT_TRUE(doc->readPdf(background, true, false));
        auto plain = std::make_shared<XojPage>(100, 100);
        auto annotated = std::make_shared<XojPage>(100, 100);
        auto text = std::make_unique<Text>();
        text->setText("Keep this annotation");
        annotated->getLayers().front()->addElement(std::move(text));
        doc->addPage(plain);
        doc->addPage(annotated);
        control.fireDocumentChanged(DOCUMENT_CHANGE_COMPLETE);
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
        auto* clipboard = gtk_widget_get_clipboard(window.getWindow(), GDK_SELECTION_CLIPBOARD);
        if (auto* targets = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("TARGETS"))) {
            gtk_selection_data_free(targets);
        }
        Plugin plugin(&control, "BeamerPresentation", fs::path(PROJECT_SOURCE_DIR) / "plugins" / "BeamerPresentation");
        ASSERT_TRUE(plugin.isValid());
        plugin.setEnabled(true);
        plugin.loadScript();
        EXPECT_TRUE(plugin.callFunction("BeamerClean"));
        EXPECT_EQ(doc->getPageCount(), 4);
        EXPECT_EQ(doc->getPage(doc->getPageCount() - 1), annotated);
        EXPECT_EQ(annotated->getLayers().front()->getElements().size(), 1);
        doc->getPage(1)->setBackgroundPdfPageNr(0);
        EXPECT_TRUE(plugin.callFunction("BeamerClean"));
        EXPECT_EQ(doc->getPageCount(), 3);
        EXPECT_EQ(doc->getPage(doc->getPageCount() - 1), annotated);
        g_signal_handlers_disconnect_by_data(gtk_settings_get_default(), &window);
    }
};

TEST_F(BeamerCleanTest, PreservesNonPdfPagesAndDistinctPdfLabels) {}

#endif
