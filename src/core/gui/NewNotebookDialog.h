/*
 * Ambitus
 *
 * "New notebook" (docs/design/r5, section 3): a small card over the window, faded in with a veil like the command
 * box. Name, subject (the courses and folders of the library) and paper, with the size of the page. It only asks:
 * what it asked for goes to the callback, and MainWindow makes the notebook. Enter creates, Esc cancels; an empty
 * name or one that is taken in the folder keeps "Create" off and says why under the field.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "ambitus/NewNotebook.h"
#include "library/LibraryView.h"

#include "filesystem.h"

namespace ambitus {

/// The thumbnail of a paper as the card draws it, 86 x 104 units from the top left of `cr` (the setup shows it too)
constexpr int PAPER_THUMB_W = 86;
constexpr int PAPER_THUMB_H = 104;
void drawPaperThumbnail(cairo_t* cr, newnotebook::Paper paper);
const char* paperThumbnailName(newnotebook::Paper paper);


namespace newnotebook {
/// The fade in and out (ms), the motion tokens of Summa
constexpr int FADE_IN_MS = 180;
constexpr int FADE_OUT_MS = 120;
}  // namespace newnotebook

class NewNotebookDialog {
public:
    /// What the user asked for
    struct Request {
        fs::path file;  ///< where it goes, in the folder of the subject (the "Boards" folder of it for an infinite
                        ///< page); empty when there is no subject to put it in
        std::string name;
        newnotebook::Paper paper = newnotebook::DEFAULT_PAPER;
        newnotebook::PageSize size = newnotebook::DEFAULT_SIZE;
    };

    /// overlay: the one over the whole window, so the card shows over the library and over the pages alike
    NewNotebookDialog(GtkOverlay* overlay, GtkWindow* window);
    ~NewNotebookDialog();
    NewNotebookDialog(const NewNotebookDialog&) = delete;
    NewNotebookDialog& operator=(const NewNotebookDialog&) = delete;

    /// Opens with `subjects` to choose from; the one that holds `near` (a folder or a file) is chosen, else the first.
    /// onCreate runs once, after the card is gone.
    void open(std::vector<library::Subject> subjects, const fs::path& near,
              std::function<void(const Request&)> onCreate);
    /// The paper chosen the next time it opens (the settings keep it)
    void setStartPaper(newnotebook::Paper paper) { startPaper = paper; }
    /// The page size chosen the next time it opens (the setup keeps it)
    void setStartSize(newnotebook::PageSize size) { startSize = size; }
    void close();
    bool isOpen() const;

private:
    void close(bool restoreFocus);
    void create();
    void chooseSubject(size_t index);
    void choosePaper(newnotebook::Paper paper);
    void chooseSize(newnotebook::PageSize size);
    void suggest();  ///< puts the suggested name in the field, while the user has not typed one
    void refresh();  ///< the sentence under the name and "Create"
    void fillSubjects();
    fs::path folderOfSubject() const;  ///< where the file goes, "Boards" included; empty with no subject
    std::string typedName() const;
    bool onKey(GtkWidget* from, GdkEventKey* e);
    void watchKeys(GtkWidget* w);

    GtkWindow* window;
    GtkWidget* revealer = nullptr;
    GtkWidget* card = nullptr;
    GtkWidget* entry = nullptr;
    GtkWidget* problem = nullptr;
    GtkWidget* subjectField = nullptr;
    GtkWidget* subjectDot = nullptr;
    GtkWidget* subjectName = nullptr;
    GtkWidget* subjectPop = nullptr;
    GtkWidget* papersBox = nullptr;
    std::vector<GtkWidget*> paperButtons;
    std::vector<GtkWidget*> sizeButtons;
    GtkWidget* cancelButton = nullptr;
    GtkWidget* createButton = nullptr;

    std::vector<library::Subject> subjects;
    size_t subject = 0;
    newnotebook::Paper startPaper = newnotebook::DEFAULT_PAPER;
    newnotebook::Paper paper = newnotebook::DEFAULT_PAPER;
    newnotebook::PageSize startSize = newnotebook::DEFAULT_SIZE;
    newnotebook::PageSize size = newnotebook::DEFAULT_SIZE;
    bool nameEdited = false;  ///< the user typed in the field: the suggestion is not put there again
    bool settingName = false;
    bool settingSize = false;
    bool valid = false;
    GtkWidget* focusBack = nullptr;  ///< what had the focus before; weak
    std::function<void(const Request&)> onCreate;
};

}  // namespace ambitus
