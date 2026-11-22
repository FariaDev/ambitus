#include <memory>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "control/Control.h"
#include "control/jobs/CustomExportJob.h"
#include "control/jobs/ExportParameters.h"
#include "control/settings/Settings.h"
#include "gui/GladeSearchpath.h"
#include "gui/MainWindow.h"

#include "config-test.h"

class ObservedCustomExportJob: public CustomExportJob {
public:
    using CustomExportJob::CustomExportJob;
    bool notified = false;
    std::string reportedError;

protected:
    void afterRun() override {
        notified = true;
        reportedError = errorMsg;
    }
};

TEST(CustomExportJobTest, DeliversWriteErrorToAfterRun) {
#ifndef __linux__
    GTEST_SKIP() << "Requires /dev/full";
#else
    if (!gtk_init_check(nullptr, nullptr)) {
        GTEST_SKIP() << "Requires a GTK display";
    }
    auto* app = gtk_application_new("com.github.xournalpp.export-test", G_APPLICATION_NON_UNIQUE);
    ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
    {
        GladeSearchpath paths;
        paths.addSearchDirectory(GET_UI_FOLDER);
        paths.addSearchDirectory(GET_TEST_RESOURCE_FOLDER);
        Control control(G_APPLICATION(app), &paths);
        control.getSettings()->setSidebarVisible(false);
        MainWindow window(&paths, &control, app);
        control.initWindow(&window, true);
        window.populate(&paths);
        control.newFile();
        gtk_widget_realize(window.getWindow());
        auto* clipboard = gtk_widget_get_clipboard(window.getWindow(), GDK_SELECTION_CLIPBOARD);
        if (auto* targets = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("TARGETS"))) {
            gtk_selection_data_free(targets);
        }

        char* directory = g_dir_make_tmp("xournalpp-custom-export-XXXXXX", nullptr);
        ASSERT_NE(directory, nullptr);
        fs::path outputDirectory(directory);
        g_free(directory);
        for (auto format: {EXPORT_GRAPHICS_PNG, EXPORT_GRAPHICS_PDF, EXPORT_GRAPHICS_SVG, EXPORT_XOJ}) {
            SCOPED_TRACE(format);
            for (bool fail: {false, true}) {
                SCOPED_TRACE(fail);
                auto output = outputDirectory / "out";
                if (fail) {
                    if (format == EXPORT_GRAPHICS_PDF) {
                        output = outputDirectory / "missing" / "out.pdf";
                    } else {
                        fs::create_symlink("/dev/full", output);
                    }
                }
                auto parameters = std::make_unique<ExportParameters>();
                parameters->format = format;
                parameters->backgroundType = EXPORT_BACKGROUND_ALL;
                parameters->pdfExportBackend = PdfExportBackend::CAIRO;
                parameters->pageRanges = {{0, 0}};
                auto* job = new ObservedCustomExportJob(&control, output, std::move(parameters));
                job->run();
                bool drained = false;
                g_idle_add_full(
                        G_PRIORITY_LOW,
                        +[](gpointer data) -> gboolean {
                            *static_cast<bool*>(data) = true;
                            return G_SOURCE_REMOVE;
                        },
                        &drained, nullptr);
                while (!drained) {
                    g_main_context_iteration(nullptr, true);
                }
                EXPECT_EQ(job->notified, fail);
                EXPECT_EQ(job->reportedError.empty(), !fail);
                if (!fail) {
                    EXPECT_GT(fs::file_size(output), 0);
                }
                job->unref();
                control.unblock();
                fs::remove(output);
            }
        }
        fs::remove_all(outputDirectory);
    }
    g_object_unref(app);
#endif
}
