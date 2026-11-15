/*
 * Ambitus
 *
 * The setup of the first time (docs/design/r5, section 1), in the form of the setup of Summa: a big card over the whole
 * window, at the left the ∂ with the sentence of the step, at the right the numbered steps, the title in STIX and the
 * choices of the step. Six steps (welcome, notebooks, pen, paper, Summa, done) and almost everything comes chosen, so
 * "Next" goes through without reading. "Skip" keeps the defaults.
 *
 * It shows once: when the settings file did not exist before this opening (Settings::getAmbitusSetupDone). The choices
 * are written when the last step is closed, the same settings the Settings screen writes (ambitus/SettingsApply).
 *
 * The card owns itself: it goes away after it has faded out.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "ambitus/NewNotebook.h"

#include "filesystem.h"

class Control;

namespace ambitus {

class SetupCard {
public:
    /// Shows the card over the window once the main loop runs, when the setup was not done yet
    static void openIfFirstTime(Control* ctrl, GtkOverlay* overlay, GtkWindow* window);
    /// Opens the card now (nothing happens if it is open)
    static void open(Control* ctrl, GtkOverlay* overlay, GtkWindow* window);

    enum class Folder { SUMMA, OWN, PICKED };
    static constexpr int STEPS = 6;

private:
    SetupCard(Control* ctrl, GtkOverlay* overlay);
    ~SetupCard();

    void build(GtkOverlay* overlay);
    void show(GtkWindow* window);
    void showStep(int step);  ///< lays out the steps, the title and the choices of a step
    void buildBody(int step);
    void buildWelcome();
    void buildNotebooks();
    void buildPen();
    void buildPaper();
    void buildSumma();
    void buildDone();
    void go(int delta);
    void finish(bool apply);  ///< writes the choices (when `apply`), marks the setup as done and closes
    void chooseFolder();
    fs::path ownFolder() const;  ///< ~/Documents/Notebooks, the folder only of Ambitus
    std::string folderShown() const;
    void close();
    void scheduleDismantle();
    void dismantle();
    void onRevealerDestroyed();

    Control* ctrl;
    int step = 0;

    // the choices; they start from what is saved (and from the system)
    int language = -1;  ///< 0 Portuguese, 1 English, -1 another one that is left alone
    int initialLanguage = -1;
    std::string summaAccent;  ///< the accent chosen in Summa ("graphite" or "violet"), empty when there is none
    int theme = 0;
    int initialTheme = 0;
    Folder folder = Folder::OWN;
    fs::path picked;
    bool fingerMoves = true;
    bool penRadial = true;
    newnotebook::PageSize size = newnotebook::DEFAULT_SIZE;
    newnotebook::Paper paper = newnotebook::DEFAULT_PAPER;
    bool summaInstalled = false;
    std::string vault;  ///< the vault of Summa, empty when there is none
    bool bridge = true;
    bool searchInk = false;

    // the scratch pad of the pen step: strokes of points, kept when going back and forth
    std::vector<std::vector<std::pair<double, double>>> strokes;
    bool drawing = false;

    GtkWidget* revealer = nullptr;
    GtkWidget* card = nullptr;
    GtkWidget* art = nullptr;
    GtkWidget* sentence = nullptr;
    GtkWidget* chips = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* body = nullptr;
    GtkWidget* back = nullptr;
    GtkWidget* skip = nullptr;
    GtkWidget* next = nullptr;
    GtkWidget* pad = nullptr;
    GtkFileChooserNative* chooser = nullptr;
    GtkWidget* focusBack = nullptr;  ///< what had the focus before; weak
    guint dismantleSource = 0;
    guint artSource = 0;
    int wantedArt = 0;
    bool dismantling = false;
    bool closing = false;
};

}  // namespace ambitus
