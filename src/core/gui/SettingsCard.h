/*
 * Ambitus
 *
 * The settings as a big card inside the window (docs/design/r5, section 4), in the form of the Settings screen of
 * Summa: at the left the title, a search field and the sections, with a mark on the chosen one; at the right the rows
 * of the section, each with its title, a grey description and the control (a switch, a segmented control, a button).
 * The search filters the rows of every section, without minding accents or capitals, and shows the section of each.
 * Esc or the cross closes.
 *
 * Every control reads and writes the settings that Xournal++ already has (the same ones of its Preferences dialog,
 * applied the way SettingsDialog::save applies them), or the ambitus* ones next to them; a change is saved at once.
 * What the card does not offer is in "Advanced", which opens the full Preferences of Xournal++.
 *
 * The card owns itself: it goes away after it has faded out.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "filesystem.h"

class Control;

namespace ambitus {

class SettingsCard {
public:
    /// Opens the card over the window (`overlay` is the one over the whole window); nothing happens if it is open
    static void open(Control* ctrl, GtkOverlay* overlay, GtkWindow* window);

private:
    SettingsCard(Control* ctrl, GtkOverlay* overlay);
    ~SettingsCard();

    /// A line of the card: it belongs to a section and is shown or hidden by the search
    struct Row {
        size_t section = 0;
        std::string title;
        std::string description;
        GtkWidget* widget = nullptr;
        GtkWidget* descriptionLabel = nullptr;
    };

    void build(GtkOverlay* overlay);
    GtkWidget* buildNav();
    GtkWidget* buildPane();
    void buildSections();
    void buildNotebooks();
    void buildPenAndTouch();
    void buildTools();
    void buildAppearance();
    void buildSumma();
    void buildAdvanced();

    /// Puts a row in a section; `control` is at the right and may be null (a row that is only text)
    Row& addRow(size_t section, const std::string& title, const std::string& description, GtkWidget* control,
                bool enabled = true);
    GtkWidget* addSwitch(size_t section, const std::string& title, const std::string& description, bool on,
                         std::function<void(bool)> changed, bool enabled = true);
    /// A line with a paragraph and the control under it, as "Advanced" has
    Row& addBlock(size_t section, const std::string& title, const std::string& paragraph, GtkWidget* control);
    /// The buttons of a segmented control; `chosen` is the one that is on (-1: none)
    GtkWidget* makeSegmented(const std::vector<std::string>& names, int chosen, std::function<void(int)> picked,
                             std::vector<GtkWidget*>* buttons = nullptr);
    void setSegmented(const std::vector<GtkWidget*>& buttons, int chosen);

    void show(GtkWindow* window);  ///< fades in; `window` is where the focus goes back to when it closes
    void showSection(size_t section);
    void search(const std::string& query);
    void refreshLayout();  ///< which rows and sections show; the last row of each loses its line
    void close(bool restoreFocus);
    bool isOpen() const;
    bool onKey(GdkEventKey* e);
    void watchKeys(GtkWidget* w);
    void scheduleDismantle();
    void dismantle();  ///< takes the card out of the window and deletes it
    void onRevealerDestroyed();

    // the settings that need something done after they change, as SettingsDialog::save does
    void inputChanged();  ///< pressure, touch, pen button: the tools and the touchscreen follow
    void chooseFolder();
    void updateFolderLine();
    std::string notebooksFolder() const;  ///< the one chosen here, else the one of Summa; empty if there is none
    void customizePalette();
    void openFullPreferences();

    Control* ctrl;
    size_t current = 0;  ///< the section shown when the search is empty
    std::string query;

    std::vector<GtkWidget*> navButtons;
    std::vector<GtkWidget*> sectionBoxes;
    std::vector<GtkWidget*> sectionHeads;
    std::vector<Row> rows;
    GtkWidget* empty = nullptr;

    GtkWidget* revealer = nullptr;
    GtkWidget* card = nullptr;
    GtkWidget* searchEntry = nullptr;
    GtkWidget* searchHint = nullptr;
    GtkWidget* paneScroll = nullptr;
    GtkWidget* content = nullptr;
    GtkWidget* closeButton = nullptr;
    GtkWidget* folderLabel = nullptr;
    GtkWidget* folderButton = nullptr;
    GtkWidget* searchInkSwitch = nullptr;
    size_t folderRowIndex = 0;
    std::vector<GtkWidget*> paperButtons;  ///< Summa, Grid, Plain and "…"
    GtkWidget* morePapers = nullptr;       ///< the popover of "…"
    GtkFileChooserNative* chooser = nullptr;
    GtkWidget* focusBack = nullptr;  ///< what had the focus before; weak
    guint dismantleSource = 0;
    int contentWidth = 0;
    bool settingSegmented = false;  ///< a segmented control is being set by the code, not clicked
    bool dismantling = false;
};

}  // namespace ambitus
