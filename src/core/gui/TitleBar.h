/*
 * Ambitus
 *
 * The title bar of the window (client side decorations): the ∂ mark, the library,
 * the name of the notebook, the screen modes (Classic, Studio, Focus), undo and a
 * menu with everything the menu bar has, for the modes where the menu bar is hidden.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>

#include <gtk/gtk.h>

namespace ambitus {

enum class EditorMode { CLASSIC = 0, STUDIO = 1, FOCUS = 2 };

class TitleBar {
public:
    /// menu: the model of the menu bar, shown under the "⋯" button
    TitleBar(GtkWindow* window, GMenuModel* menu, std::function<void(EditorMode)> onMode);

    GtkWidget* getWidget() const { return bar; }

    /// The library hides what only makes sense in the editor
    void setEditorShown(bool editor);
    /// A board has no screen modes
    void setModesShown(bool shown);
    /// Marks the mode without calling back
    void setMode(EditorMode mode);

private:
    void updateTitle();

    GtkWindow* window;
    GtkWidget* bar = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* modes = nullptr;
    GtkWidget* undo = nullptr;
    GtkWidget* modeButtons[3] = {};
    std::function<void(EditorMode)> onMode;
    bool editorShown = true;
    bool modesShown = true;
    bool updating = false;
};

}  // namespace ambitus
