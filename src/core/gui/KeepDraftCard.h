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

    /// overlay: the one over the whole window, so the card shows over the library and over the page alike
    KeepDraftCard(GtkOverlay* overlay, GtkWindow* window);
    ~KeepDraftCard();
    KeepDraftCard(const KeepDraftCard&) = delete;
    KeepDraftCard& operator=(const KeepDraftCard&) = delete;

    /// Opens with `subjects` to choose from; the last subject used is chosen, else the first. onKeep (or onCancel, when
    /// the user gives up) runs once, after the card is gone.
    void open(std::vector<library::Subject> subjects, std::function<void(const Request&)> onKeep,
              std::function<void()> onCancel = {});
    void close();
    bool isOpen() const;

private:
    void close(bool restoreFocus);
    void keep();
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
    GtkWidget* entry = nullptr;
    GtkWidget* problem = nullptr;
    GtkWidget* subjectsBox = nullptr;
    GtkWidget* cancelButton = nullptr;
    GtkWidget* keepButton = nullptr;

    std::vector<library::Subject> subjects;
    std::vector<GtkWidget*> subjectButtons;
    size_t subject = 0;
    bool valid = false;
    GtkWidget* focusBack = nullptr;  ///< what had the focus before; weak
    std::function<void(const Request&)> onKeep;
    std::function<void()> onCancel;
};

}  // namespace ambitus
