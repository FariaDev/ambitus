/*
 * Ambitus
 *
 * The LaTeX dialog as a card inside the window (docs/design/r6, section 8), in place of the separate window of
 * Xournal++: faded in over the page with a veil, as the "New notebook" card. The formula in a monospaced field, the
 * preview above it that follows what you type, and under the field the error of LaTeX in one line. Ctrl+Enter inserts,
 * Esc cancels.
 *
 * It is the "dialog" of the LatexController (it derives from AbstractLatexDialog to be that), so the compiling, the
 * preview and the insertion in the page are the ones of Xournal++; the .xopp gets the same TexImage as before. The card
 * owns the controller, and itself: it goes away after it has faded out.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>
#include <string>

#include <gtk/gtk.h>

#include "gui/dialog/AbstractLatexDialog.h"

class LatexController;

namespace ambitus {

class LatexCard final: public AbstractLatexDialog {
public:
    /// Opens the card over the window (`overlay` is the one over the whole window) for the formula of `ctrl`
    static void open(GtkOverlay* overlay, GtkWindow* window, std::unique_ptr<LatexController> ctrl);

    std::string getBufferContents() override;
    void setCompilationStatus(bool isTexValid, bool isCompilationDone, const std::string& compilationOutput) override;

private:
    LatexCard(GtkOverlay* overlay, GtkWindow* window, std::unique_ptr<LatexController> ctrl);
    ~LatexCard() override;

    void show(GtkWindow* window);  ///< fades in; `window` is where the focus goes back to when it closes
    void insert();                 ///< puts the formula in the page, if it compiled; else waits for the compiling
    void cancel();
    void close(bool restoreFocus);
    bool isOpen() const;
    bool onKey(GdkEventKey* e);
    void setProblem(const std::string& text);
    void growPreviewFor(PopplerPage* page);
    void scheduleDismantle();
    void dismantle();  ///< takes the card out of the window and deletes it
    void onRevealerDestroyed();

    GtkWidget* revealer = nullptr;
    GtkWidget* card = nullptr;
    GtkWidget* scroll = nullptr;
    GtkWidget* view = nullptr;
    GtkTextBuffer* buffer = nullptr;
    GtkWidget* problem = nullptr;
    GtkWidget* cancelButton = nullptr;
    GtkWidget* focusBack = nullptr;  ///< what had the focus before; weak
    guint dismantleSource = 0;
    int previewHeight = 0;
    bool insertWhenReady = false;  ///< Ctrl+Enter while the preview was still compiling
    bool dismantling = false;
};

}  // namespace ambitus
