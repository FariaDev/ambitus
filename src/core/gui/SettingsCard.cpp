/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "SettingsCard.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>

#include "ambitus/NewNotebook.h"
#include "ambitus/SettingsApply.h"
#include "ambitus/SettingsRows.h"
#include "control/Control.h"
#include "control/actions/ActionDatabase.h"
#include "control/settings/Settings.h"
#include "gui/AmbitusMotion.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "gui/inputdevices/HandRecognition.h"
#include "library/SummaCourses.h"
#include "util/PathUtil.h"
#include "util/StringUtils.h"
#include "util/i18n.h"

namespace ambitus {

using newnotebook::Paper;

namespace {
constexpr int MARGIN = 32;          // the card is big, with the veil showing around it
constexpr int NAV_WIDTH = 260;      // the nav of the prototype is 220 in its small window; the Summa has 288
constexpr int CONTENT_MAX = 720;    // .settings-content of the Summa
constexpr int CONTENT_GUTTER = 96;  // 48 px at each side of the rows
constexpr int FOLDER_FIELD = 300;

enum Section : size_t { NOTEBOOKS, PEN, TOOLS, APPEARANCE, SUMMA, ADVANCED, SECTIONS };

std::string sectionName(size_t s) {
    switch (s) {
        case NOTEBOOKS:
            return _("Notebooks");
        case PEN:
            return _("Pen and touch");
        case TOOLS:
            return _("Tools");
        case APPEARANCE:
            return _("Appearance");
        case SUMMA:
            return _("Summa");
        default:
            return _("Advanced");
    }
}

void addClass(GtkWidget* w, const char* cls) { gtk_style_context_add_class(gtk_widget_get_style_context(w), cls); }
void removeClass(GtkWidget* w, const char* cls) {
    gtk_style_context_remove_class(gtk_widget_get_style_context(w), cls);
}
void setClass(GtkWidget* w, const char* cls, bool on) { on ? addClass(w, cls) : removeClass(w, cls); }

GtkWidget* label(const std::string& text, const char* cls) {
    GtkWidget* l = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    addClass(l, cls);
    return l;
}

void clickOf(GtkWidget* w, std::function<void()> f) {
    auto* fn = new std::function<void()>(std::move(f));
    g_signal_connect_data(
            w, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) { (*static_cast<std::function<void()>*>(d))(); }), fn,
            [](gpointer d, GClosure*) { delete static_cast<std::function<void()>*>(d); }, GConnectFlags(0));
}

bool animationsOn(GtkWidget* w) { return !motion::reducedMotion(w); }
int fadeIn() { return static_cast<int>(motion::BASE_US / 1000); }
int fadeOut() { return static_cast<int>(motion::FAST_US / 1000); }

GtkWidget* icon(const char* name) {
    GtkWidget* img = gtk_image_new_from_icon_name(name, GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(img), 16);
    return img;
}

std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos) {
        return {};
    }
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

std::string shown(const fs::path& p) {
    const char* home = g_get_home_dir();
    return settingsrows::shownPath(std::string(char_cast(p.u8string())), home ? home : "");
}

using settingsapply::languageIndex;
using settingsapply::summaVault;
}  // namespace

// There is one card at a time
static SettingsCard* openCard = nullptr;

void SettingsCard::open(Control* ctrl, GtkOverlay* overlay, GtkWindow* window) {
    if (openCard) {
        return;
    }
    // owns itself: dismantle() deletes it after the fade out
    auto* card = new SettingsCard(ctrl, overlay);
    card->show(window);
}

SettingsCard::SettingsCard(Control* ctrl, GtkOverlay* overlay): ctrl(ctrl) {
    openCard = this;
    build(overlay);
}

SettingsCard::~SettingsCard() {
    if (openCard == this) {
        openCard = nullptr;
    }
    if (dismantleSource) {
        g_source_remove(dismantleSource);
    }
    if (chooser) {
        gtk_native_dialog_destroy(GTK_NATIVE_DIALOG(chooser));
        g_object_unref(chooser);
    }
    if (focusBack) {
        g_object_remove_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// the widgets

void SettingsCard::build(GtkOverlay* overlay) {
    // the veil covers the window and takes the clicks: a slip of the mouse does not close the card
    revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    GtkWidget* clicks = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(clicks), false);
    g_signal_connect(clicks, "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer) -> gboolean { return TRUE; }), nullptr);
    GtkWidget* veil = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(veil, "amb-cmd-veil");
    gtk_container_add(GTK_CONTAINER(clicks), veil);
    gtk_container_add(GTK_CONTAINER(revealer), clicks);

    card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    addClass(card, "amb-cmd");
    addClass(card, "amb-dlg");
    addClass(card, "amb-set");
    gtk_widget_set_halign(card, GTK_ALIGN_FILL);
    gtk_widget_set_valign(card, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(card, true);
    gtk_widget_set_margin_top(card, MARGIN);
    gtk_widget_set_margin_bottom(card, MARGIN);
    gtk_widget_set_margin_start(card, MARGIN + 8);
    gtk_widget_set_margin_end(card, MARGIN + 8);
    gtk_container_add(GTK_CONTAINER(veil), card);

    gtk_box_pack_start(GTK_BOX(card), buildNav(), false, false, 0);
    gtk_box_pack_start(GTK_BOX(card), buildPane(), true, true, 0);
    buildSections();

    // the keys of the card are read on its own widgets: the window hands the keys to the one that has the focus first
    std::function<void(GtkWidget*)> walk = [&](GtkWidget* w) {
        if (gtk_widget_get_can_focus(w) || GTK_IS_BUTTON(w) || GTK_IS_ENTRY(w) || GTK_IS_SWITCH(w)) {
            watchKeys(w);
        }
        if (GTK_IS_CONTAINER(w)) {
            gtk_container_foreach(
                    GTK_CONTAINER(w),
                    +[](GtkWidget* child, gpointer d) { (*static_cast<std::function<void(GtkWidget*)>*>(d))(child); },
                    &walk);
        }
    };
    walk(card);
    if (morePapers) {
        walk(morePapers);
    }

    // once faded out, the card is taken away
    g_signal_connect_swapped(revealer, "notify::child-revealed", G_CALLBACK(+[](SettingsCard* self) {
                                 auto* r = GTK_REVEALER(self->revealer);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     self->scheduleDismantle();
                                 }
                             }),
                             this);
    g_signal_connect_swapped(revealer, "destroy", G_CALLBACK(+[](SettingsCard* self) { self->onRevealerDestroyed(); }),
                             this);
    showSection(NOTEBOOKS);
    gtk_widget_show_all(revealer);
    refreshLayout();  // show_all showed the sections that the layout hides
    gtk_overlay_add_overlay(overlay, revealer);
}

GtkWidget* SettingsCard::buildNav() {
    GtkWidget* nav = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(nav, "amb-set-nav");
    gtk_widget_set_size_request(nav, NAV_WIDTH, -1);

    gtk_box_pack_start(GTK_BOX(nav), label(_("Settings"), "amb-set-title"), false, false, 0);

    // a plain entry, with the hint as a label over it, as the command box does (GTK hides the placeholder of the
    // entry that has the focus)
    searchEntry = gtk_entry_new();
    addClass(searchEntry, "amb-cmd-entry");
    addClass(searchEntry, "amb-set-search");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(searchEntry), GTK_ENTRY_ICON_PRIMARY, "xopp-search");
    gtk_entry_set_icon_activatable(GTK_ENTRY(searchEntry), GTK_ENTRY_ICON_PRIMARY, false);
    GtkWidget* area = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(area), searchEntry);
    searchHint = gtk_label_new(C_("settings search", "Search"));
    addClass(searchHint, "amb-cmd-hint");
    gtk_widget_set_halign(searchHint, GTK_ALIGN_START);
    gtk_widget_set_valign(searchHint, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(area), searchHint);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(area), searchHint, true);
    g_signal_connect(searchEntry, "changed", G_CALLBACK(+[](GtkEditable* e, SettingsCard* self) {
                         const std::string text = gtk_entry_get_text(GTK_ENTRY(e));
                         gtk_widget_set_visible(self->searchHint, text.empty());
                         self->search(text);
                     }),
                     this);
    gtk_box_pack_start(GTK_BOX(nav), area, false, false, 0);
    gtk_widget_set_margin_bottom(area, 12);

    for (size_t i = 0; i < SECTIONS; i++) {
        GtkWidget* b = gtk_button_new();
        addClass(b, "amb-set-tab");
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        gtk_widget_set_focus_on_click(b, false);  // the ring is for the keyboard, not for the mouse
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);  // the mark of the chosen one, 2 px at the left
        addClass(bar, "amb-set-bar");
        gtk_widget_set_size_request(bar, 2, 26);
        gtk_widget_set_valign(bar, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(row), bar, false, false, 0);
        GtkWidget* l = label(sectionName(i), "amb-set-tab-name");
        gtk_widget_set_margin_start(l, 10);
        gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
        gtk_container_add(GTK_CONTAINER(b), row);
        clickOf(b, [this, i] { showSection(i); });
        gtk_box_pack_start(GTK_BOX(nav), b, false, false, 0);
        navButtons.push_back(b);
    }
    return nav;
}

GtkWidget* SettingsCard::buildPane() {
    GtkWidget* pane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(pane, "amb-set-pane");

    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    closeButton = gtk_button_new();
    addClass(closeButton, "amb-set-close");
    gtk_button_set_relief(GTK_BUTTON(closeButton), GTK_RELIEF_NONE);
    gtk_button_set_image(GTK_BUTTON(closeButton), icon("xopp-x"));
    gtk_widget_set_tooltip_text(closeButton, _("Close"));
    clickOf(closeButton, [this] { close(true); });
    gtk_box_pack_end(GTK_BOX(top), closeButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(pane), top, false, false, 0);

    paneScroll = gtk_scrolled_window_new(nullptr, nullptr);
    addClass(paneScroll, "amb-set-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(paneScroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(content, "amb-set-content");
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(content, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(paneScroll), content);
    // the rows keep the width of the Summa, or the window's if it is narrower
    g_signal_connect(paneScroll, "size-allocate", G_CALLBACK(+[](GtkWidget*, GdkRectangle* a, SettingsCard* self) {
                         const int w = std::clamp(a->width - CONTENT_GUTTER, 360, CONTENT_MAX);
                         if (w != self->contentWidth) {
                             self->contentWidth = w;
                             gtk_widget_set_size_request(self->content, w, -1);
                         }
                     }),
                     this);
    gtk_box_pack_start(GTK_BOX(pane), paneScroll, true, true, 0);
    return pane;
}

void SettingsCard::buildSections() {
    for (size_t i = 0; i < SECTIONS; i++) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        addClass(box, "amb-set-section");
        GtkWidget* head = label(sectionName(i), "amb-set-head");
        gtk_box_pack_start(GTK_BOX(box), head, false, false, 0);
        gtk_box_pack_start(GTK_BOX(content), box, false, false, 0);
        sectionBoxes.push_back(box);
        sectionHeads.push_back(head);
    }
    empty = label(_("No setting matches this search."), "amb-set-empty");
    gtk_box_pack_start(GTK_BOX(content), empty, false, false, 0);
    gtk_widget_set_no_show_all(empty, true);

    buildNotebooks();
    buildPenAndTouch();
    buildTools();
    buildAppearance();
    buildSumma();
    buildAdvanced();
}

SettingsCard::Row& SettingsCard::addRow(size_t section, const std::string& title, const std::string& description,
                                        GtkWidget* control, bool enabled) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    addClass(row, "amb-set-row");
    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(text), label(title, "amb-set-row-title"), false, false, 0);
    GtkWidget* desc = nullptr;
    if (!description.empty()) {
        desc = label(description, "amb-set-row-desc");
        gtk_label_set_line_wrap(GTK_LABEL(desc), true);
        gtk_label_set_max_width_chars(GTK_LABEL(desc), 64);
        gtk_box_pack_start(GTK_BOX(text), desc, false, false, 0);
    }
    gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(row), text, true, true, 0);
    if (control) {
        gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
        gtk_box_pack_end(GTK_BOX(row), control, false, false, 0);
    }
    if (!enabled) {
        addClass(row, "off");
        gtk_widget_set_sensitive(row, false);
    }
    gtk_box_pack_start(GTK_BOX(sectionBoxes[section]), row, false, false, 0);
    rows.push_back({section, title, description, row, desc});
    return rows.back();
}

SettingsCard::Row& SettingsCard::addBlock(size_t section, const std::string& title, const std::string& paragraph,
                                          GtkWidget* control) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    addClass(row, "amb-set-row");
    addClass(row, "amb-set-block");
    GtkWidget* p = label(paragraph, "amb-set-para");
    gtk_label_set_line_wrap(GTK_LABEL(p), true);
    gtk_label_set_max_width_chars(GTK_LABEL(p), 72);
    gtk_box_pack_start(GTK_BOX(row), p, false, false, 0);
    gtk_widget_set_halign(control, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(row), control, false, false, 0);
    gtk_box_pack_start(GTK_BOX(sectionBoxes[section]), row, false, false, 0);
    rows.push_back({section, title, paragraph, row, p});
    return rows.back();
}

GtkWidget* SettingsCard::addSwitch(size_t section, const std::string& title, const std::string& description, bool on,
                                   std::function<void(bool)> changed, bool enabled) {
    GtkWidget* sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), on);  // before the signal: setting it up writes nothing
    auto* fn = new std::function<void(bool)>(std::move(changed));
    g_signal_connect_data(
            sw, "notify::active", G_CALLBACK(+[](GObject* o, GParamSpec*, gpointer d) {
                (*static_cast<std::function<void(bool)>*>(d))(gtk_switch_get_active(GTK_SWITCH(o)));
            }),
            fn, [](gpointer d, GClosure*) { delete static_cast<std::function<void(bool)>*>(d); }, GConnectFlags(0));
    addRow(section, title, description, sw, enabled);
    return sw;
}

GtkWidget* SettingsCard::makeSegmented(const std::vector<std::string>& names, int chosen,
                                       std::function<void(int)> picked, std::vector<GtkWidget*>* buttons) {
    GtkWidget* seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(seg, "amb-seg");
    std::vector<GtkWidget*> list;
    auto shared = std::make_shared<std::function<void(int)>>(std::move(picked));
    auto group = std::make_shared<std::vector<GtkWidget*>>();
    for (size_t i = 0; i < names.size(); i++) {
        GtkWidget* b = gtk_toggle_button_new_with_label(names[i].c_str());
        gtk_widget_set_can_focus(b, true);
        group->push_back(b);
        gtk_container_add(GTK_CONTAINER(seg), b);
        clickOf(b, [this, shared, group, i] {
            if (settingSegmented) {
                return;  // gtk_toggle_button_set_active also emits "clicked"
            }
            setSegmented(*group, static_cast<int>(i));
            (*shared)(static_cast<int>(i));
        });
    }
    setSegmented(*group, chosen);
    if (buttons) {
        *buttons = *group;
    }
    return seg;
}

void SettingsCard::setSegmented(const std::vector<GtkWidget*>& buttons, int chosen) {
    settingSegmented = true;
    for (size_t i = 0; i < buttons.size(); i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttons[i]), static_cast<int>(i) == chosen);
    }
    settingSegmented = false;
}

// ---- the sections ----

void SettingsCard::buildNotebooks() {
    Settings* settings = ctrl->getSettings();

    // the folder: a field that shows the path and opens the folder chooser
    folderButton = gtk_button_new();
    addClass(folderButton, "amb-dlg-field");
    addClass(folderButton, "amb-set-field");
    GtkWidget* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(line), icon("xopp-folder"), false, false, 0);
    folderLabel = label("", "amb-dlg-field-name");
    gtk_label_set_ellipsize(GTK_LABEL(folderLabel), PANGO_ELLIPSIZE_START);
    gtk_label_set_max_width_chars(GTK_LABEL(folderLabel), 1);
    gtk_box_pack_start(GTK_BOX(line), folderLabel, true, true, 0);
    gtk_container_add(GTK_CONTAINER(folderButton), line);
    gtk_widget_set_size_request(folderButton, FOLDER_FIELD, -1);
    clickOf(folderButton, [this] { chooseFolder(); });
    addRow(NOTEBOOKS, _("Notebooks folder"), _("the same as Summa"), folderButton);
    folderRowIndex = rows.size() - 1;
    updateFolderLine();

    // saving by itself: the timer of Xournal++, and the tabs that wait aside
    const int minutes = std::max(1, settings->getAutosaveTimeout());
    std::string when;
    if (minutes == 1) {
        when = _("every minute");
    } else {
        gchar* text = g_strdup_printf(_("every %d minutes"), minutes);
        when = text;
        g_free(text);
    }
    addSwitch(NOTEBOOKS, _("Save automatically"), when, settings->isAutosaveEnabled(), [this](bool on) {
        ctrl->getSettings()->setAutosaveEnabled(on);
        ctrl->enableAutosave(on);
    });

    addSwitch(NOTEBOOKS, _("Keep earlier versions"), _("one per ten minutes of work; the 30 newest"),
              settings->getAmbitusKeepVersions(), [this](bool on) { ctrl->getSettings()->setAmbitusKeepVersions(on); });

    // the paper a new notebook starts with: three at hand and the others under "…"
    const auto saved = settingsrows::paperFromId(settings->getAmbitusNewPaper()).value_or(newnotebook::DEFAULT_PAPER);
    const Paper at[3] = {Paper::SUMMA, Paper::GRID, Paper::PLAIN};
    int chosen = saved == Paper::BLUE_RULED || saved == Paper::DOTTED ? 3 : -1;
    for (int i = 0; i < 3; i++) {
        if (at[i] == saved) {
            chosen = i;
        }
    }
    auto paperName = [](Paper p) -> std::string {
        switch (p) {
            case Paper::SUMMA:
                return C_("paper", "Summa");
            case Paper::BLUE_RULED:
                return C_("paper", "Blue ruled");
            case Paper::GRID:
                return C_("paper", "Grid");
            case Paper::DOTTED:
                return C_("paper", "Dotted");
            default:
                return C_("paper", "Plain");
        }
    };
    auto choosePaper = [this](Paper p) { ctrl->getSettings()->setAmbitusNewPaper(settingsrows::paperId(p)); };
    GtkWidget* seg = makeSegmented(
            {paperName(at[0]), paperName(at[1]), paperName(at[2]), "…"}, chosen,
            [this, at, choosePaper](int i) {
                if (i < 3) {
                    choosePaper(at[i]);
                } else {
                    // "…": the other papers, in a small list under the button
                    gtk_popover_popup(GTK_POPOVER(morePapers));
                    // the choice is made in the list; until then the button keeps what was on
                    const auto now = settingsrows::paperFromId(ctrl->getSettings()->getAmbitusNewPaper())
                                             .value_or(newnotebook::DEFAULT_PAPER);
                    int on = now == Paper::BLUE_RULED || now == Paper::DOTTED ? 3 : -1;
                    for (int k = 0; k < 3; k++) {
                        if (at[k] == now) {
                            on = k;
                        }
                    }
                    setSegmented(paperButtons, on);
                }
            },
            &paperButtons);
    morePapers = gtk_popover_new(paperButtons[3]);
    addClass(morePapers, "amb-dlg-pop");
    GtkWidget* list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    for (Paper p: {Paper::BLUE_RULED, Paper::DOTTED}) {
        GtkWidget* b = gtk_button_new_with_label(paperName(p).c_str());
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        addClass(b, "amb-set-more");
        gtk_widget_set_halign(gtk_bin_get_child(GTK_BIN(b)), GTK_ALIGN_START);
        clickOf(b, [this, p, choosePaper] {
            choosePaper(p);
            setSegmented(paperButtons, 3);
            gtk_popover_popdown(GTK_POPOVER(morePapers));
        });
        gtk_box_pack_start(GTK_BOX(list), b, false, false, 0);
    }
    gtk_widget_show_all(list);
    gtk_container_add(GTK_CONTAINER(morePapers), list);
    addRow(NOTEBOOKS, _("Paper of new notebooks"), "", seg);
}

void SettingsCard::buildPenAndTouch() {
    Settings* settings = ctrl->getSettings();

    // nothing in the program puts the palette or the radial menu on one side yet: the row says so
    {
        GtkWidget* seg = makeSegmented({C_("handedness", "Right"), C_("handedness", "Left")}, 0, [](int) {});
        addRow(PEN, _("Hand you write with"), _("the palette and the radial open on the other side; not available yet"),
               seg, false);
    }
    addSwitch(PEN, _("Pen pressure changes the thickness"), "", settings->isPressureSensitivity(), [this](bool on) {
        ctrl->getSettings()->setPressureSensitivity(on);
        inputChanged();
    });
    // the finger moves the page when it does not draw
    addSwitch(PEN, _("Finger on the screen moves the page"), "", !settings->getTouchDrawingEnabled(),
              [this](bool on) { settingsapply::applyFingerMovesPage(ctrl, on); });
    addSwitch(PEN, _("Pen button opens the radial menu"), "", settings->getAmbitusStylusRadial(),
              [this](bool on) { settingsapply::applyPenRadial(ctrl, on); });
    // the palm: the touchscreen is switched off while the pen is near (the "Hand recognition" of Xournal++)
    bool palm = false;
    settings->getCustomElement("touch").getBool("disableTouch", palm);
    addSwitch(PEN, _("Ignore the palm of the hand"), "", palm, [this](bool on) {
        Settings* s = ctrl->getSettings();
        s->getCustomElement("touch").setBool("disableTouch", on);
        s->customSettingsChanged();
        if (auto* win = ctrl->getWindow()) {
            win->getXournal()->getHandRecognition()->reload();
        }
    });
}

void SettingsCard::buildTools() {
    Settings* settings = ctrl->getSettings();
    addSwitch(TOOLS, _("Hold still to straighten the stroke"), _("line, circle, rectangle, arrow"),
              settings->getAmbitusHoldToSnap(), [this](bool on) { ctrl->getSettings()->setAmbitusHoldToSnap(on); });
    addSwitch(TOOLS, _("One key per tool"), _("P, M, E, S, T, X"), settings->getAmbitusToolKeys(),
              [this](bool on) { ctrl->getSettings()->setAmbitusToolKeys(on); });
    GtkWidget* button = gtk_button_new_with_label(_("Customize…"));
    addClass(button, "amb-set-btn");
    clickOf(button, [this] { customizePalette(); });
    addRow(TOOLS, _("Palette and radial menu"), _("what is at hand, the colors and the favorites"), button);
}

void SettingsCard::buildAppearance() {
    Settings* settings = ctrl->getSettings();

    addRow(APPEARANCE, _("Theme"), "",
           makeSegmented({_("System"), _("Light"), _("Dark")}, settingsapply::themeIndex(ctrl),
                         [this](int i) { settingsapply::applyTheme(ctrl, i); }));

    // the accent of Summa lives in the state of that app, which Ambitus does not read; the row is there, off
    GtkWidget* dots = settingsapply::makeAccentDots();
    addRow(APPEARANCE, _("Accent color"), _("the same as Summa, if it is open; not available yet"), dots, false);

    // the language: Xournal++ keeps the locale and reads it when it starts
    addRow(APPEARANCE, _("Language"), _("applies the next time Ambitus opens"),
           makeSegmented({"Português", "English"}, languageIndex(settings->getPreferredLocale()),
                         [this](int i) { settingsapply::applyLanguage(ctrl, i); }));
}

void SettingsCard::buildSumma() {
    Settings* settings = ctrl->getSettings();
    addSwitch(SUMMA, _("Bridge with Summa"), _("“Send to Summa”, Check from the page"),
              settings->getAmbitusSummaBridge(), [this](bool on) {
                  ctrl->getSettings()->setAmbitusSummaBridge(on);
                  // the handwriting goes through the bridge: without it that row has nothing to do
                  GtkWidget* row = gtk_widget_get_parent(searchInkSwitch);
                  gtk_widget_set_sensitive(row, on);
                  setClass(row, "off", !on);
              });
    searchInkSwitch = addSwitch(
            SUMMA, _("Search handwriting too"), _("pages with ink go to your model and become text for the search"),
            settings->getAmbitusSearchInk(), [this](bool on) { ctrl->getSettings()->setAmbitusSearchInk(on); },
            settings->getAmbitusSummaBridge());
}

void SettingsCard::buildAdvanced() {
    GtkWidget* button = gtk_button_new_with_label(_("Open the full Xournal++ Preferences"));
    addClass(button, "amb-set-btn");
    clickOf(button, [this] { openFullPreferences(); });
    addBlock(ADVANCED, _("Open the full Xournal++ Preferences"),
             _("LaTeX, audio, input devices, default file names, plugins and everything else that Xournal++ offers."),
             button);
}

// ---------------------------------------------------------------------------------------------------------------------
// showing, searching, closing

void SettingsCard::show(GtkWindow* window) {
    focusBack = gtk_window_get_focus(window);
    if (focusBack) {
        g_object_add_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? fadeIn() : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
    gtk_widget_realize(searchEntry);  // the revealer realizes its child late, and the focus change needs it realized
    gtk_widget_grab_focus(searchEntry);
}

bool SettingsCard::isOpen() const {
    return revealer && GTK_IS_REVEALER(revealer) && gtk_revealer_get_reveal_child(GTK_REVEALER(revealer));
}

void SettingsCard::watchKeys(GtkWidget* w) {
    g_signal_connect(w, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* e, SettingsCard* self) -> gboolean {
                         return self->onKey(e);
                     }),
                     this);
}

bool SettingsCard::onKey(GdkEventKey* e) {
    if (!isOpen() || e->keyval != GDK_KEY_Escape) {
        return false;
    }
    // Esc clears a search first, and then closes
    if (!query.empty() && gtk_widget_has_focus(searchEntry)) {
        gtk_entry_set_text(GTK_ENTRY(searchEntry), "");
        return true;
    }
    close(true);
    return true;
}

void SettingsCard::showSection(size_t section) {
    current = section;
    if (gtk_entry_get_text(GTK_ENTRY(searchEntry))[0]) {
        gtk_entry_set_text(GTK_ENTRY(searchEntry), "");  // runs search(""), which lays the card out
        return;
    }
    query.clear();
    refreshLayout();
}

void SettingsCard::search(const std::string& text) {
    query = trimmed(text);
    refreshLayout();
}

void SettingsCard::refreshLayout() {
    const bool searching = !query.empty();
    bool any = false;
    for (size_t s = 0; s < SECTIONS; s++) {
        Row* last = nullptr;
        size_t shownRows = 0;
        for (auto& r: rows) {
            if (r.section != s) {
                continue;
            }
            const bool match = !searching || settingsrows::matches(query, r.title, r.description, sectionName(s));
            gtk_widget_set_visible(r.widget, match);
            removeClass(r.widget, "last");
            if (match) {
                last = &r;
                shownRows++;
            }
        }
        if (last) {
            addClass(last->widget, "last");
        }
        const bool visible = searching ? shownRows > 0 : s == current;
        gtk_widget_set_visible(sectionBoxes[s], visible);
        any = any || visible;
        // while searching nothing is the chosen section
        setClass(navButtons[s], "on", !searching && s == current);
    }
    setClass(content, "searching", searching);
    gtk_widget_set_visible(empty, searching && !any);
    // the section changed: from the top
    if (paneScroll) {
        gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(paneScroll)), 0);
    }
}

void SettingsCard::close(bool restoreFocus) {
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? fadeOut() : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), false);
    // without a transition, child-revealed does not change after the revealer is off
    if (!animationsOn(revealer)) {
        gtk_widget_hide(revealer);
        scheduleDismantle();
    }
    if (restoreFocus && focusBack && gtk_widget_get_mapped(focusBack)) {
        gtk_widget_grab_focus(focusBack);
    }
}

void SettingsCard::scheduleDismantle() {
    if (dismantleSource) {
        return;
    }
    dismantleSource = g_idle_add(
            +[](gpointer d) -> gboolean {
                auto* self = static_cast<SettingsCard*>(d);
                self->dismantleSource = 0;
                self->dismantle();
                return G_SOURCE_REMOVE;
            },
            this);
}

void SettingsCard::dismantle() {
    dismantling = true;
    if (revealer) {
        gtk_widget_destroy(revealer);  // onRevealerDestroyed runs, without deleting
    }
    delete this;
}

void SettingsCard::onRevealerDestroyed() {
    revealer = nullptr;
    if (openCard == this) {
        openCard = nullptr;
    }
    if (!dismantling) {
        delete this;  // the window itself is going away
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// what a change does besides being saved

void SettingsCard::inputChanged() { settingsapply::inputChanged(ctrl); }

std::string SettingsCard::notebooksFolder() const {
    const std::string own = ctrl->getSettings()->getAmbitusNotebooksFolder();
    return own.empty() ? summaVault() : own;
}

void SettingsCard::updateFolderLine() {
    const std::string folder = notebooksFolder();
    const bool own = !ctrl->getSettings()->getAmbitusNotebooksFolder().empty();
    if (folder.empty()) {
        gtk_label_set_text(GTK_LABEL(folderLabel), _("Choose a folder"));
    } else {
        gtk_label_set_text(GTK_LABEL(folderLabel),
                           shown(fs::path(std::u8string(folder.begin(), folder.end()))).c_str());
    }
    Row& row = rows[folderRowIndex];
    row.description = own ? _("added to the library; Summa keeps its own") : _("the same as Summa");
    gtk_label_set_text(GTK_LABEL(row.descriptionLabel), row.description.c_str());
}

void SettingsCard::chooseFolder() {
    if (chooser) {
        return;
    }
    GtkWindow* window = GTK_WINDOW(gtk_widget_get_toplevel(card));
    chooser = gtk_file_chooser_native_new(_("Folder of the notebooks"), window, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                          _("Choose"), _("_Cancel"));
    std::error_code ec;
    const std::string now = notebooksFolder();
    const fs::path start(std::u8string(now.begin(), now.end()));
    if (!now.empty() && fs::is_directory(start, ec)) {
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(chooser), Util::toGFile(start).get(), nullptr);
    }
    g_signal_connect(chooser, "response", G_CALLBACK(+[](GtkNativeDialog* dlg, int response, gpointer d) {
                         auto* self = static_cast<SettingsCard*>(d);
                         if (response == GTK_RESPONSE_ACCEPT) {
                             GFile* f = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dlg));
                             if (f) {
                                 const fs::path chosen =
                                         Util::fromGFile(xoj::util::GObjectSPtr<GFile>(f, xoj::util::adopt).get());
                                 settingsapply::applyNotebooksFolder(self->ctrl, chosen);
                                 self->updateFolderLine();
                             }
                         }
                         self->chooser = nullptr;
                         gtk_native_dialog_destroy(dlg);
                         g_object_unref(dlg);
                     }),
                     this);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}

void SettingsCard::customizePalette() {
    Control* c = ctrl;
    close(false);
    // after the card is gone from the way: the panel is a popover of the palette
    g_idle_add(
            +[](gpointer d) -> gboolean {
                if (auto* win = static_cast<Control*>(d)->getWindow()) {
                    win->openPalettePanel();
                }
                return G_SOURCE_REMOVE;
            },
            c);
}

void SettingsCard::openFullPreferences() {
    Control* c = ctrl;
    close(false);
    c->showSettings();  // the dialog of Xournal++, with everything
}

}  // namespace ambitus
