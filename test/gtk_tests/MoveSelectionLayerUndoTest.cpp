#include <memory>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "control/Control.h"
#include "control/actions/ActionDatabase.h"
#include "control/layer/LayerController.h"
#include "control/settings/Settings.h"
#include "dialog/GtkTest.h"
#include "gui/GladeSearchpath.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"

#include "config-test.h"

class MoveSelectionLayerUndoTest: public GtkTest {
protected:
    virtual bool moveUp() const = 0;

    void runTest(GtkApplication* app) override {
        GladeSearchpath paths;
        paths.addSearchDirectory(GET_UI_FOLDER);
        paths.addSearchDirectory(XOJ_TEST_RESOURCE_DIR);
        Control control(G_APPLICATION(app), &paths);
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
        auto* clipboard = gtk_widget_get_clipboard(window.getWindow(), GDK_SELECTION_CLIPBOARD);
        if (auto* targets = gtk_clipboard_wait_for_contents(clipboard, gdk_atom_intern_static_string("TARGETS"))) {
            gtk_selection_data_free(targets);
        }

        auto page = control.getCurrentPage();
        control.getLayerController()->insertLayer(page, new Layer(), 1);
        auto sourceId = moveUp() ? 1u : 2u;
        auto destinationId = moveUp() ? 2u : 1u;
        control.getLayerController()->switchToLay(sourceId);
        auto* source = page->getLayers().at(sourceId - 1);
        auto* destination = page->getLayers().at(destinationId - 1);
        auto stroke = std::make_unique<Stroke>();
        stroke->addPoint(Point(10, 10));
        stroke->addPoint(Point(30, 30));
        auto* movedStroke = stroke.get();
        source->addElement(std::move(stroke));
        auto* actions = control.getActionDatabase();
        actions->fireActivateAction(Action::SELECT_ALL);
        actions->fireActivateAction(moveUp() ? Action::MOVE_SELECTION_LAYER_UP : Action::MOVE_SELECTION_LAYER_DOWN);
        control.clearSelection();
        ASSERT_TRUE(source->getElements().empty());
        ASSERT_EQ(destination->getElements().size(), 1);
        EXPECT_EQ(destination->getElements().front().get(), movedStroke);
        EXPECT_EQ(page->getSelectedLayerId(), destinationId);

        actions->fireActivateAction(Action::UNDO);
        ASSERT_EQ(source->getElements().size(), 1);
        EXPECT_EQ(source->getElements().front().get(), movedStroke);
        EXPECT_TRUE(destination->getElements().empty());
        EXPECT_EQ(page->getSelectedLayerId(), sourceId);

        actions->fireActivateAction(Action::REDO);
        EXPECT_TRUE(source->getElements().empty());
        ASSERT_EQ(destination->getElements().size(), 1);
        EXPECT_EQ(destination->getElements().front().get(), movedStroke);
        EXPECT_EQ(page->getSelectedLayerId(), destinationId);
        g_signal_handlers_disconnect_by_data(gtk_settings_get_default(), &window);
    }
};

class MoveSelectionLayerDownUndoTest: public MoveSelectionLayerUndoTest {
    bool moveUp() const override { return false; }
};

class MoveSelectionLayerUpUndoTest: public MoveSelectionLayerUndoTest {
    bool moveUp() const override { return true; }
};

TEST_F(MoveSelectionLayerDownUndoTest, RestoresStrokeAndActiveLayer) {}
TEST_F(MoveSelectionLayerUpUndoTest, RestoresStrokeAndActiveLayer) {}
