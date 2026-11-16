/*
 * Ambitus
 *
 * "Keep draft": a small card over the window, in the mold of the "New notebook" one. It asks for a name and a subject
 * (the courses and folders of the library, as chips) and nothing else: what it asked for goes to the callback, and
 * MainWindow moves the draft there as a normal notebook.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "library/LibraryView.h"

#include "filesystem.h"

namespace ambitus {

class KeepDraftCard {
public:
    /// What the user asked for
    struct Request {
        fs::path file;  ///< where the notebook goes: folder/name.xopp
        std::string name;
    };

    /// What to do with a draft that is being closed
    enum class CloseChoice { DISCARD, KEEP_IN_DRAFTS, KEEP_AS_NOTEBOOK };

    /// overlay: the one over the whole window, so the card shows over the library and over the page alike
    KeepDraftCard(GtkOverlay* overlay, GtkWindow* window);
    ~KeepDraftCard();
    KeepDraftCard(const KeepDraftCard&) = delete;
    KeepDraftCard& operator=(const KeepDraftCard&) = delete;

    /// Opens with `subjects` to choose from; the last subject used is chosen, else the first. onKeep (or onCancel, when
    /// the user gives up) runs once, after the card is gone.
    void open(std::vector<library::Subject> subjects, std::function<void(const Request&)> onKeep,
              std::function<void()> onCancel = {});
    /// The prompt of a closing draft: onChoice runs once, after the card is gone; Esc or a click outside calls onCancel
    void openClose(std::function<void(CloseChoice)> onChoice, std::function<void()> onCancel = {});
    void close();
    bool isOpen() const;

private:
    enum class Mode { KEEP, CLOSE };
    void close(bool restoreFocus);
    void reveal(GtkWidget* focus);
    void keep();
    void chooseClose(CloseChoice choice);
    void chooseSubject(size_t index);
    void fillSubjects();
    void refresh();
    std::string typedName() const;
    fs::path folderOfSubject() const;
    bool onKey(GtkWidget* from, GdkEventKey* e);
    void watchKeys(GtkWidget* w);

    GtkWindow* window;
    GtkWidget* revealer = nullptr;
    GtkWidget* card = nullptr;
    GtkWidget* keepPage = nullptr;
    GtkWidget* closePage = nullptr;
    GtkWidget* entry = nullptr;
    GtkWidget* problem = nullptr;
    GtkWidget* subjectsBox = nullptr;
    GtkWidget* cancelButton = nullptr;
    GtkWidget* keepButton = nullptr;
    GtkWidget* closeDiscard = nullptr;
    GtkWidget* closeDrafts = nullptr;
    GtkWidget* closeNotebook = nullptr;

    std::vector<library::Subject> subjects;
    std::vector<GtkWidget*> subjectButtons;
    size_t subject = 0;
    bool valid = false;
    Mode mode = Mode::KEEP;
    GtkWidget* focusBack = nullptr;  ///< what had the focus before; weak
    std::function<void(const Request&)> onKeep;
    std::function<void(CloseChoice)> onCloseChoice;
    std::function<void()> onCancel;
};

}  // namespace ambitus
