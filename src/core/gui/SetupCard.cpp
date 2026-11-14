/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "SetupCard.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <system_error>

#include "ambitus/SettingsApply.h"
#include "ambitus/SettingsRows.h"
#include "control/Control.h"
#include "control/settings/Settings.h"
#include "gui/AmbitusMotion.h"
#include "gui/NewNotebookDialog.h"
#include "util/StringUtils.h"
#include "util/i18n.h"

namespace ambitus {

using newnotebook::PageSize;
using newnotebook::Paper;

namespace {
constexpr int MARGIN = 32;          // the card is big, with the veil showing around it
constexpr double ART_SHARE = 0.38;  // .setup of the prototype: 38% for the symbol, the rest for the steps
constexpr int PAD_HEIGHT = 130;
constexpr int PAPER_RING = 4;

void addClass(GtkWidget* w, const char* cls) { gtk_style_context_add_class(gtk_widget_get_style_context(w), cls); }

GtkWidget* label(const std::string& text, const char* cls) {
    GtkWidget* l = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    addClass(l, cls);
    return l;
}

GtkWidget* wrapped(const std::string& text, const char* cls, int chars = 64) {
    GtkWidget* l = label(text, cls);
    gtk_label_set_line_wrap(GTK_LABEL(l), true);
    gtk_label_set_max_width_chars(GTK_LABEL(l), chars);
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

std::string shown(const fs::path& p) {
    const char* home = g_get_home_dir();
    return settingsrows::shownPath(std::string(char_cast(p.u8string())), home ? home : "");
}

fs::path pathOf(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string stepName(int step) {
    switch (step) {
        case 0:
            return C_("setup step", "Welcome");
        case 1:
            return C_("setup step", "Notebooks");
        case 2:
            return C_("setup step", "Pen");
        case 3:
            return C_("setup step", "Paper");
        case 4:
            return C_("setup step", "Summa");
        default:
            return C_("setup step", "Done");
    }
}

std::string stepSentence(int step) {
    switch (step) {
        case 0:
            return _("Let's get Ambitus ready.");
        case 1:
            return _("Where the notebooks live.");
        case 2:
            return _("Try the pen.");
        case 3:
            return _("The everyday sheet.");
        case 4:
            return _("The two talk to each other.");
        default:
            return _("All ready.");
    }
}

/// A segmented control: the buttons of `names`, `chosen` is on (-1: none); `picked` runs on a click
GtkWidget* segmented(const std::vector<std::string>& names, int chosen, std::function<void(int)> picked) {
    GtkWidget* seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(seg, "amb-seg");
    auto shared = std::make_shared<std::function<void(int)>>(std::move(picked));
    auto group = std::make_shared<std::vector<GtkWidget*>>();
    auto setting = std::make_shared<bool>(false);  // gtk_toggle_button_set_active also emits "clicked"
    for (size_t i = 0; i < names.size(); i++) {
        GtkWidget* b = gtk_toggle_button_new_with_label(names[i].c_str());
        gtk_widget_set_can_focus(b, true);
        gtk_widget_set_focus_on_click(b, false);
        group->push_back(b);
        gtk_container_add(GTK_CONTAINER(seg), b);
        clickOf(b, [shared, group, setting, i] {
            if (*setting) {
                return;
            }
            *setting = true;
            for (size_t k = 0; k < group->size(); k++) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON((*group)[k]), k == i);
            }
            *setting = false;
            (*shared)(static_cast<int>(i));
        });
    }
    *setting = true;
    for (size_t k = 0; k < group->size(); k++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON((*group)[k]), static_cast<int>(k) == chosen);
    }
    *setting = false;
    return seg;
}

/// A choice with a name and a grey line under it; `on` is the chosen one
GtkWidget* option(const std::string& name, const std::string& sub, bool on, const std::string& tag,
                  std::function<void()> click) {
    GtkWidget* b = gtk_button_new();
    addClass(b, "amb-opt");
    if (on) {
        addClass(b, "on");
    }
    gtk_widget_set_focus_on_click(b, false);
    GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(head), label(name, "amb-opt-name"), false, false, 0);
    if (!tag.empty()) {
        gtk_box_pack_start(GTK_BOX(head), label(tag, "amb-opt-tag"), false, false, 0);
    }
    gtk_box_pack_start(GTK_BOX(col), head, false, false, 0);
    GtkWidget* subLabel = label(sub, "amb-opt-sub");
    if (sub.size() > 40) {  // a long path gives way in the middle; a short line is shown whole
        gtk_label_set_ellipsize(GTK_LABEL(subLabel), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_max_width_chars(GTK_LABEL(subLabel), 60);
    }
    gtk_box_pack_start(GTK_BOX(col), subLabel, false, false, 0);
    gtk_container_add(GTK_CONTAINER(b), col);
    clickOf(b, std::move(click));
    return b;
}

/// A line of the step as the Settings screen has it: title, a grey description, and the control at the right
GtkWidget* row(const std::string& title, const std::string& description, GtkWidget* control, bool enabled = true) {
    GtkWidget* r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    addClass(r, "amb-set-row");
    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(text), label(title, "amb-set-row-title"), false, false, 0);
    if (!description.empty()) {
        gtk_box_pack_start(GTK_BOX(text), wrapped(description, "amb-set-row-desc", 56), false, false, 0);
    }
    gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(r), text, true, true, 0);
    gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(r), control, false, false, 0);
    if (!enabled) {
        addClass(r, "off");
        gtk_widget_set_sensitive(r, false);
    }
    return r;
}

GtkWidget* switchOf(bool on, std::function<void(bool)> changed) {
    GtkWidget* sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), on);  // before the signal: setting it up changes nothing
    auto* fn = new std::function<void(bool)>(std::move(changed));
    g_signal_connect_data(
            sw, "notify::active", G_CALLBACK(+[](GObject* o, GParamSpec*, gpointer d) {
                (*static_cast<std::function<void(bool)>*>(d))(gtk_switch_get_active(GTK_SWITCH(o)));
            }),
            fn, [](gpointer d, GClosure*) { delete static_cast<std::function<void(bool)>*>(d); }, GConnectFlags(0));
    return sw;
}

GtkWidget* kbd(const char* key) { return label(key, "amb-setup-kbd"); }
}  // namespace

// There is one card at a time
static SetupCard* openCard = nullptr;

// ---------------------------------------------------------------------------------------------------------------------
// opening

namespace {
struct Pending {
    Control* ctrl;
    GtkOverlay* overlay;
    GtkWindow* window;  // weak: the window may be gone before the main loop gets here
};
}  // namespace

void SetupCard::openIfFirstTime(Control* ctrl, GtkOverlay* overlay, GtkWindow* window) {
    if (ctrl->getSettings()->getAmbitusSetupDone()) {
        return;
    }
    auto* p = new Pending{ctrl, overlay, window};
    g_object_add_weak_pointer(G_OBJECT(window), reinterpret_cast<gpointer*>(&p->window));
    g_idle_add(
            +[](gpointer d) -> gboolean {
                auto* p = static_cast<Pending*>(d);
                if (p->window) {
                    g_object_remove_weak_pointer(G_OBJECT(p->window), reinterpret_cast<gpointer*>(&p->window));
                    SetupCard::open(p->ctrl, p->overlay, p->window);
                }
                delete p;
                return G_SOURCE_REMOVE;
            },
            p);
}

void SetupCard::open(Control* ctrl, GtkOverlay* overlay, GtkWindow* window) {
    if (openCard) {
        return;
    }
    // owns itself: dismantle() deletes it after the fade out
    auto* card = new SetupCard(ctrl, overlay);
    card->show(window);
}

SetupCard::SetupCard(Control* ctrl, GtkOverlay* overlay): ctrl(ctrl) {
    openCard = this;
    Settings* s = ctrl->getSettings();

    // everything starts chosen: from what is saved, and from the system for the language
    language = settingsapply::languageIndex(s->getPreferredLocale());
    initialLanguage = language;
    theme = initialTheme = settingsapply::themeIndex(ctrl);
    summaInstalled = settingsapply::summaInstalled();
    vault = settingsapply::summaVault();
    folder = vault.empty() ? Folder::OWN : Folder::SUMMA;
    fingerMoves = !s->getTouchDrawingEnabled();
    penRadial = s->getAmbitusStylusRadial();
    size = settingsrows::sizeFromId(s->getAmbitusNewSize()).value_or(newnotebook::DEFAULT_SIZE);
    paper = settingsrows::paperFromId(s->getAmbitusNewPaper()).value_or(newnotebook::DEFAULT_PAPER);
    bridge = s->getAmbitusSummaBridge();
    searchInk = s->getAmbitusSearchInk();

    build(overlay);
}

SetupCard::~SetupCard() {
    if (openCard == this) {
        openCard = nullptr;
    }
    if (dismantleSource) {
        g_source_remove(dismantleSource);
    }
    if (artSource) {
        g_source_remove(artSource);
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

void SetupCard::build(GtkOverlay* overlay) {
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
    addClass(card, "amb-setup");
    gtk_widget_set_halign(card, GTK_ALIGN_FILL);
    gtk_widget_set_valign(card, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(card, true);
    gtk_widget_set_margin_top(card, MARGIN);
    gtk_widget_set_margin_bottom(card, MARGIN);
    gtk_widget_set_margin_start(card, MARGIN + 8);
    gtk_widget_set_margin_end(card, MARGIN + 8);
    gtk_widget_set_can_focus(card, true);
    gtk_container_add(GTK_CONTAINER(veil), card);

    // the symbol, with the sentence of the step under it
    art = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    addClass(art, "amb-setup-art");
    gtk_widget_set_valign(art, GTK_ALIGN_FILL);
    GtkWidget* middle = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_valign(middle, GTK_ALIGN_CENTER);
    GtkWidget* glyph = gtk_label_new("∂");
    addClass(glyph, "amb-setup-glyph");
    gtk_box_pack_start(GTK_BOX(middle), glyph, false, false, 0);
    sentence = gtk_label_new("");
    addClass(sentence, "amb-setup-sentence");
    gtk_label_set_justify(GTK_LABEL(sentence), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(sentence), true);
    gtk_box_pack_start(GTK_BOX(middle), sentence, false, false, 0);
    gtk_box_pack_start(GTK_BOX(art), middle, true, true, 0);
    gtk_box_pack_start(GTK_BOX(card), art, false, false, 0);
    // 38% of the card; the width is asked for outside of the allocation, which does not lay out again by itself
    g_signal_connect(card, "size-allocate", G_CALLBACK(+[](GtkWidget*, GdkRectangle* a, SetupCard* self) {
                         self->wantedArt = static_cast<int>(a->width * ART_SHARE);
                         if (self->artSource) {
                             return;
                         }
                         self->artSource = g_idle_add(
                                 +[](gpointer d) -> gboolean {
                                     auto* self = static_cast<SetupCard*>(d);
                                     self->artSource = 0;
                                     int now = 0;
                                     gtk_widget_get_size_request(self->art, &now, nullptr);
                                     if (std::abs(now - self->wantedArt) > 1) {
                                         gtk_widget_set_size_request(self->art, self->wantedArt, -1);
                                     }
                                     return G_SOURCE_REMOVE;
                                 },
                                 self);
                     }),
                     this);

    // the steps, the title, the choices and the buttons
    GtkWidget* pane = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(pane, "amb-setup-pane");
    gtk_box_pack_start(GTK_BOX(card), pane, true, true, 0);

    chips = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    addClass(chips, "amb-setup-chips");
    gtk_widget_set_halign(chips, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(pane), chips, false, false, 0);

    title = label("", "amb-setup-title");
    gtk_label_set_line_wrap(GTK_LABEL(title), true);
    gtk_box_pack_start(GTK_BOX(pane), title, false, false, 0);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    addClass(scroll, "amb-set-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(body, "amb-setup-body");
    gtk_container_add(GTK_CONTAINER(scroll), body);
    gtk_box_pack_start(GTK_BOX(pane), scroll, true, true, 0);

    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    addClass(foot, "amb-dlg-foot");
    back = gtk_button_new_with_label(_("Back"));
    addClass(back, "amb-dlg-ghost");
    addClass(back, "amb-setup-back");
    gtk_widget_set_focus_on_click(back, false);
    clickOf(back, [this] { go(-1); });
    gtk_box_pack_start(GTK_BOX(foot), back, false, false, 0);
    gtk_box_pack_start(GTK_BOX(foot), gtk_label_new(""), true, true, 0);
    skip = gtk_button_new_with_label(_("Skip and use the default"));
    addClass(skip, "amb-dlg-ghost");
    addClass(skip, "amb-setup-skip");
    gtk_widget_set_focus_on_click(skip, false);
    clickOf(skip, [this] { finish(false); });
    gtk_box_pack_start(GTK_BOX(foot), skip, false, false, 0);
    next = gtk_button_new_with_label("");
    addClass(next, "amb-dlg-primary");
    gtk_widget_set_focus_on_click(next, false);
    clickOf(next, [this] { go(1); });
    gtk_box_pack_start(GTK_BOX(foot), next, false, false, 0);
    gtk_box_pack_end(GTK_BOX(pane), foot, false, false, 0);

    // Enter on the card itself goes forward; Esc does not close it (it is not something to leave by a slip)
    g_signal_connect(card, "key-press-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventKey* e, SetupCard* self) -> gboolean {
                         GtkWidget* top = gtk_widget_get_toplevel(w);
                         if ((e->keyval == GDK_KEY_Return || e->keyval == GDK_KEY_KP_Enter) && GTK_IS_WINDOW(top) &&
                             gtk_window_get_focus(GTK_WINDOW(top)) == w) {
                             self->go(1);
                             return TRUE;
                         }
                         return FALSE;
                     }),
                     this);

    // once faded out, the card is taken away
    g_signal_connect_swapped(revealer, "notify::child-revealed", G_CALLBACK(+[](SetupCard* self) {
                                 auto* r = GTK_REVEALER(self->revealer);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     self->scheduleDismantle();
                                 }
                             }),
                             this);
    g_signal_connect_swapped(revealer, "destroy", G_CALLBACK(+[](SetupCard* self) { self->onRevealerDestroyed(); }),
                             this);
    showStep(0);
    gtk_widget_show_all(revealer);
    gtk_widget_set_visible(back, false);
    gtk_overlay_add_overlay(overlay, revealer);
}

void SetupCard::show(GtkWindow* window) {
    focusBack = gtk_window_get_focus(window);
    if (focusBack) {
        g_object_add_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? fadeIn() : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
    gtk_widget_realize(card);
    gtk_widget_grab_focus(card);
}

void SetupCard::showStep(int to) {
    step = std::clamp(to, 0, STEPS - 1);
    gtk_label_set_text(GTK_LABEL(sentence), stepSentence(step).c_str());

    // the numbered steps: the one now is filled, the ones behind it show a check
    gtk_container_foreach(GTK_CONTAINER(chips), +[](GtkWidget* c, gpointer) { gtk_widget_destroy(c); }, nullptr);
    for (int k = 0; k < STEPS; k++) {
        GtkWidget* chip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        addClass(chip, "amb-setup-chip");
        if (k == step) {
            addClass(chip, "cur");
        } else if (k < step) {
            addClass(chip, "done");
        }
        const std::string num = k < step ? "✓" : std::to_string(k + 1);
        gtk_box_pack_start(GTK_BOX(chip), label(num, "amb-setup-num"), false, false, 0);
        gtk_box_pack_start(GTK_BOX(chip), label(stepName(k), "amb-setup-chip-name"), false, false, 0);
        gtk_box_pack_start(GTK_BOX(chips), chip, false, false, 0);
    }
    gtk_widget_show_all(chips);

    const bool last = step == STEPS - 1;
    gtk_widget_set_visible(back, step > 0);
    gtk_widget_set_visible(skip, !last);
    gtk_button_set_label(GTK_BUTTON(next), step == 0 ? C_("setup", "Start") :
                                           last      ? C_("setup", "Open the library") :
                                                       C_("setup", "Next"));
    buildBody(step);
}

void SetupCard::buildBody(int at) {
    pad = nullptr;
    gtk_container_foreach(GTK_CONTAINER(body), +[](GtkWidget* c, gpointer) { gtk_widget_destroy(c); }, nullptr);
    switch (at) {
        case 0:
            gtk_label_set_text(GTK_LABEL(title), _("Welcome to Ambitus."));
            buildWelcome();
            break;
        case 1:
            gtk_label_set_text(GTK_LABEL(title), _("Where do your notebooks live?"));
            buildNotebooks();
            break;
        case 2:
            gtk_label_set_text(GTK_LABEL(title), _("Try the pen."));
            buildPen();
            break;
        case 3:
            gtk_label_set_text(GTK_LABEL(title), _("The everyday sheet."));
            buildPaper();
            break;
        case 4:
            gtk_label_set_text(GTK_LABEL(title), summaInstalled ? _("Summa is here.") : _("Summa is not here."));
            buildSumma();
            break;
        default:
            gtk_label_set_text(GTK_LABEL(title), _("All ready."));
            buildDone();
            break;
    }
    gtk_widget_show_all(body);
}

void SetupCard::buildWelcome() {
    // language
    gtk_box_pack_start(GTK_BOX(body), label(_("Language"), "amb-opt-label"), false, false, 0);
    GtkWidget* langs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(langs, GTK_ALIGN_START);
    auto pickLanguage = [this](int l) {
        language = l;
        buildBody(step);
    };
    gtk_box_pack_start(GTK_BOX(langs),
                       option("Português", "do Brasil", language == 0, "", [pickLanguage] { pickLanguage(0); }), false,
                       false, 0);
    gtk_box_pack_start(
            GTK_BOX(langs),
            option("English", _("you can change it later"), language == 1, "", [pickLanguage] { pickLanguage(1); }),
            false, false, 0);
    gtk_box_pack_start(GTK_BOX(body), langs, false, false, 0);

    // theme: shown at once, so the card itself answers
    gtk_box_pack_start(GTK_BOX(body), label(_("Theme"), "amb-opt-label"), false, false, 0);
    GtkWidget* themes = segmented({_("System"), _("Light"), _("Dark")}, theme, [this](int i) {
        theme = i;
        settingsapply::applyTheme(ctrl, i);
    });
    gtk_widget_set_halign(themes, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(body), themes, false, false, 0);

    // the accent of Summa lives in the state of that app, which Ambitus does not read: shown off, as in the Settings
    gtk_box_pack_start(GTK_BOX(body), label(_("Accent color"), "amb-opt-label"), false, false, 0);
    GtkWidget* dots = settingsapply::makeAccentDots();
    gtk_widget_set_halign(dots, GTK_ALIGN_START);
    addClass(dots, "amb-setup-off");
    gtk_box_pack_start(GTK_BOX(body), dots, false, false, 0);
    gtk_box_pack_start(GTK_BOX(body),
                       wrapped(_("the same as Summa, if it is open; not available yet"), "amb-setup-cap"), false, false,
                       0);
    gtk_box_pack_start(GTK_BOX(body), wrapped(_("All of this can be changed later, in Settings."), "amb-setup-cap"),
                       false, false, 0);
}

fs::path SetupCard::ownFolder() const {
    const char* docs = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
    fs::path base = docs ? fs::path(std::u8string(docs, docs + strlen(docs))) : pathOf(g_get_home_dir()) / "Documents";
    return base / pathOf(_("Notebooks"));
}

std::string SetupCard::folderShown() const {
    switch (folder) {
        case Folder::SUMMA:
            return shown(pathOf(vault));
        case Folder::PICKED:
            return shown(picked);
        default:
            return shown(ownFolder());
    }
}

void SetupCard::buildNotebooks() {
    GtkWidget* list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    if (!vault.empty()) {
        gtk_box_pack_start(
                GTK_BOX(list),
                option(_("The same folder as Summa"),
                       shown(pathOf(vault)) + " · " + _("the subjects of Summa become the folders of the library"),
                       folder == Folder::SUMMA, _("recommended"),
                       [this] {
                           folder = Folder::SUMMA;
                           buildBody(step);
                       }),
                false, false, 0);
    }
    gtk_box_pack_start(GTK_BOX(list),
                       option(_("A folder only for Ambitus"), shown(ownFolder()), folder == Folder::OWN, "",
                              [this] {
                                  folder = Folder::OWN;
                                  buildBody(step);
                              }),
                       false, false, 0);
    gtk_box_pack_start(
            GTK_BOX(list),
            option(_("Choose another…"),
                   folder == Folder::PICKED ? shown(picked) : std::string(_("any folder, even a synced one")),
                   folder == Folder::PICKED, "", [this] { chooseFolder(); }),
            false, false, 0);
    gtk_box_pack_start(GTK_BOX(body), list, false, false, 0);
    gtk_box_pack_start(GTK_BOX(body),
                       wrapped(_("The notebooks are plain .xopp files: they open in Xournal++ too."), "amb-setup-cap"),
                       false, false, 0);
}

void SetupCard::chooseFolder() {
    if (chooser) {
        return;
    }
    GtkWindow* window = GTK_WINDOW(gtk_widget_get_toplevel(card));
    chooser = gtk_file_chooser_native_new(_("Folder of the notebooks"), window, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                          _("Choose"), _("_Cancel"));
    g_signal_connect(chooser, "response", G_CALLBACK(+[](GtkNativeDialog* dlg, int response, gpointer d) {
                         auto* self = static_cast<SetupCard*>(d);
                         if (response == GTK_RESPONSE_ACCEPT) {
                             GFile* f = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dlg));
                             if (f) {
                                 self->picked =
                                         Util::fromGFile(xoj::util::GObjectSPtr<GFile>(f, xoj::util::adopt).get());
                                 self->folder = Folder::PICKED;
                             }
                         }
                         self->chooser = nullptr;
                         gtk_native_dialog_destroy(dlg);
                         g_object_unref(dlg);
                         if (!self->closing && self->revealer) {
                             self->buildBody(self->step);
                         }
                     }),
                     this);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}

void SetupCard::buildPen() {
    // the scratch pad: a drawing area with a smooth stroke, not a document
    GtkWidget* overlay = gtk_overlay_new();
    pad = gtk_drawing_area_new();
    addClass(pad, "amb-setup-pad");
    gtk_widget_set_size_request(pad, -1, PAD_HEIGHT);
    gtk_widget_add_events(
            pad, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON1_MOTION_MASK);
    g_signal_connect(pad, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, SetupCard* self) -> gboolean {
                         // a drawing area paints no box of its own: the paper and the dashed edge are from the CSS
                         GtkStyleContext* ctx = gtk_widget_get_style_context(w);
                         const int pw = gtk_widget_get_allocated_width(w);
                         const int ph = gtk_widget_get_allocated_height(w);
                         gtk_render_background(ctx, cr, 0, 0, pw, ph);
                         gtk_render_frame(ctx, cr, 0, 0, pw, ph);
                         GdkRGBA ink;
                         gtk_style_context_get_color(ctx, GTK_STATE_FLAG_NORMAL, &ink);
                         gdk_cairo_set_source_rgba(cr, &ink);
                         cairo_set_line_width(cr, 2.2);
                         cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                         cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
                         for (const auto& s: self->strokes) {
                             if (s.empty()) {
                                 continue;
                             }
                             cairo_move_to(cr, s[0].first, s[0].second);
                             if (s.size() == 1) {
                                 cairo_line_to(cr, s[0].first + 0.01, s[0].second);
                             }
                             // through the midpoints, so the corners of the sampling do not show
                             for (size_t i = 1; i + 1 < s.size(); i++) {
                                 const double mx = (s[i].first + s[i + 1].first) / 2;
                                 const double my = (s[i].second + s[i + 1].second) / 2;
                                 double cx = 0;
                                 double cy = 0;
                                 cairo_get_current_point(cr, &cx, &cy);
                                 cairo_curve_to(cr, cx + 2. / 3 * (s[i].first - cx), cy + 2. / 3 * (s[i].second - cy),
                                                mx + 2. / 3 * (s[i].first - mx), my + 2. / 3 * (s[i].second - my), mx,
                                                my);
                             }
                             if (s.size() > 1) {
                                 cairo_line_to(cr, s.back().first, s.back().second);
                             }
                             cairo_stroke(cr);
                         }
                         return FALSE;
                     }),
                     this);
    g_signal_connect(pad, "button-press-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventButton* e, SetupCard* self) -> gboolean {
                         if (e->button != 1) {
                             return FALSE;
                         }
                         self->drawing = true;
                         self->strokes.emplace_back(1, std::make_pair(e->x, e->y));
                         gtk_widget_queue_draw(w);
                         return TRUE;
                     }),
                     this);
    g_signal_connect(pad, "motion-notify-event",
                     G_CALLBACK(+[](GtkWidget* w, GdkEventMotion* e, SetupCard* self) -> gboolean {
                         if (!self->drawing || self->strokes.empty()) {
                             return FALSE;
                         }
                         self->strokes.back().emplace_back(e->x, e->y);
                         gtk_widget_queue_draw(w);
                         return TRUE;
                     }),
                     this);
    g_signal_connect(pad, "button-release-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton*, SetupCard* self) -> gboolean {
                         self->drawing = false;
                         return TRUE;
                     }),
                     this);
    gtk_container_add(GTK_CONTAINER(overlay), pad);
    GtkWidget* hint = label(_("write here"), "amb-setup-padhint");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_widget_set_valign(hint, GTK_ALIGN_START);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), hint);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), hint, true);
    gtk_box_pack_start(GTK_BOX(body), overlay, false, false, 0);

    // the hand is not kept anywhere yet, so the row is shown off as in the Settings
    GtkWidget* hand = segmented({C_("handedness", "Right"), C_("handedness", "Left")}, 0, [](int) {});
    gtk_box_pack_start(GTK_BOX(body),
                       row(_("Hand you write with"),
                           _("the palette and the radial open on the other side; not available yet"), hand, false),
                       false, false, 0);
    gtk_box_pack_start(
            GTK_BOX(body),
            row(_("Finger on the screen moves the page"), _("only the pen writes; with the finger you scroll and zoom"),
                switchOf(fingerMoves, [this](bool on) { fingerMoves = on; })),
            false, false, 0);
    GtkWidget* radial = row(_("Pen button opens the radial menu"), _("off, it becomes an eraser while held"),
                            switchOf(penRadial, [this](bool on) { penRadial = on; }));
    addClass(radial, "last");
    gtk_box_pack_start(GTK_BOX(body), radial, false, false, 0);
}

void SetupCard::buildPaper() {
    GtkWidget* papers = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(papers, GTK_ALIGN_START);
    for (Paper p: newnotebook::allPapers()) {
        GtkWidget* b = gtk_button_new();
        addClass(b, "amb-paper");
        if (p == paper) {
            addClass(b, "on");
        }
        gtk_widget_set_focus_on_click(b, false);
        GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget* area = gtk_drawing_area_new();
        addClass(area, "amb-paper-pg");
        gtk_widget_set_size_request(area, PAPER_THUMB_W + 2 * PAPER_RING, PAPER_THUMB_H + 2 * PAPER_RING);
        g_object_set_data(G_OBJECT(area), "amb-paper", GINT_TO_POINTER(static_cast<int>(p)));
        g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* a, cairo_t* cr, SetupCard* self) -> gboolean {
                             auto p = static_cast<Paper>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(a), "amb-paper")));
                             GdkRGBA ink;
                             gtk_style_context_get_color(gtk_widget_get_style_context(a), GTK_STATE_FLAG_NORMAL, &ink);
                             const double w = gtk_widget_get_allocated_width(a);
                             const double h = gtk_widget_get_allocated_height(a);
                             if (p == self->paper) {  // the ring, with a gap, as the card of the new notebook
                                 gdk_cairo_set_source_rgba(cr, &ink);
                                 cairo_set_line_width(cr, 2);
                                 cairo_rectangle(cr, 1, 1, w - 2, h - 2);
                                 cairo_stroke(cr);
                             }
                             cairo_save(cr);
                             cairo_translate(cr, (w - PAPER_THUMB_W) / 2, (h - PAPER_THUMB_H) / 2);
                             cairo_rectangle(cr, 0, 0, PAPER_THUMB_W, PAPER_THUMB_H);
                             cairo_clip_preserve(cr);
                             cairo_set_source_rgb(cr, 1, 1, 1);  // the page stays white, in the dark theme too
                             cairo_fill(cr);
                             drawPaperThumbnail(cr, p);
                             cairo_restore(cr);
                             cairo_save(cr);
                             cairo_translate(cr, (w - PAPER_THUMB_W) / 2, (h - PAPER_THUMB_H) / 2);
                             cairo_set_line_width(cr, 1);
                             cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, .18);  // a hairline
                             cairo_rectangle(cr, .5, .5, PAPER_THUMB_W - 1, PAPER_THUMB_H - 1);
                             cairo_stroke(cr);
                             cairo_restore(cr);
                             return FALSE;
                         }),
                         this);
        gtk_box_pack_start(GTK_BOX(col), area, false, false, 0);
        GtkWidget* name = gtk_label_new(paperThumbnailName(p));
        addClass(name, "amb-paper-name");
        gtk_box_pack_start(GTK_BOX(col), name, false, false, 0);
        gtk_container_add(GTK_CONTAINER(b), col);
        clickOf(b, [this, p] {
            paper = p;
            buildBody(step);
        });
        gtk_box_pack_start(GTK_BOX(papers), b, false, false, 0);
    }
    gtk_box_pack_start(GTK_BOX(body), papers, false, false, 0);

    GtkWidget* sizes = segmented({C_("page size", "A4"), C_("page size", "Letter")}, size == PageSize::LETTER ? 1 : 0,
                                 [this](int i) { size = i == 1 ? PageSize::LETTER : PageSize::A4; });
    GtkWidget* r = row(_("Size"), "", sizes);
    gtk_widget_set_margin_top(r, 10);
    addClass(r, "last");
    gtk_box_pack_start(GTK_BOX(body), r, false, false, 0);
    gtk_box_pack_start(GTK_BOX(body),
                       wrapped(_("Each new notebook can change this. This is only the default."), "amb-setup-cap"),
                       false, false, 0);
}

void SetupCard::buildSumma() {
    if (!summaInstalled) {
        gtk_box_pack_start(
                GTK_BOX(body),
                wrapped(_("Summa is the study desk that reads your notebooks and tutors you. It is not installed on "
                          "this computer, so there is nothing to set up here: Ambitus works without it."),
                        "amb-set-para", 56),
                false, false, 0);
        gtk_box_pack_start(GTK_BOX(body),
                           wrapped(_("If you install it later, the bridge is in Settings."), "amb-setup-cap"), false,
                           false, 0);
        return;
    }
    gtk_box_pack_start(GTK_BOX(body),
                       row(_("Bridge with Summa"), _("“Send to Summa”, Check from the page"),
                           switchOf(bridge, [this](bool on) { bridge = on; })),
                       false, false, 0);
    GtkWidget* ink = row(_("Search handwriting too"),
                         _("pages with ink go to your model and become text for the search. It leaves the computer, so "
                           "it starts off."),
                         switchOf(searchInk, [this](bool on) { searchInk = on; }));
    addClass(ink, "last");
    gtk_box_pack_start(GTK_BOX(body), ink, false, false, 0);
}

void SetupCard::buildDone() {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    auto keys = [](std::initializer_list<const char*> names, const char* sep) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        bool first = true;
        for (const char* n: names) {
            if (!first && *sep) {
                gtk_box_pack_start(GTK_BOX(box), label(sep, "amb-setup-cap"), false, false, 0);
            }
            first = false;
            gtk_box_pack_start(GTK_BOX(box), kbd(n), false, false, 0);
        }
        return box;
    };
    auto line = [&](int at, GtkWidget* left, const std::string& text) {
        gtk_widget_set_halign(left, GTK_ALIGN_START);
        gtk_widget_set_valign(left, GTK_ALIGN_CENTER);
        GtkWidget* what = label(text, "amb-set-row-title");
        gtk_widget_set_valign(what, GTK_ALIGN_CENTER);
        gtk_grid_attach(GTK_GRID(grid), left, 0, at, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), what, 1, at, 1, 1);
    };
    line(0, keys({"P", "M", "E"}, ""), _("pen, highlighter, eraser"));
    line(1, keys({"S", "T", "X"}, ""), _("selection, text, LaTeX"));
    line(2, keys({"Ctrl", "K"}, "+"), _("search the notebooks, or any command by name"));
    GtkWidget* hold = label(_("hold still"), "amb-setup-cap");
    line(3, hold, _("straightens the stroke into a line, circle, arrow…"));
    gtk_box_pack_start(GTK_BOX(body), grid, false, false, 0);

    // what was chosen, in a line
    std::string text = folderShown() + " · " + C_("page size", size == PageSize::LETTER ? "Letter" : "A4") + " · " +
                       paperThumbnailName(paper);
    GtkWidget* sum = wrapped(text, "amb-setup-cap");
    gtk_widget_set_margin_top(sum, 14);
    gtk_box_pack_start(GTK_BOX(body), sum, false, false, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// going on, and finishing

void SetupCard::go(int delta) {
    if (closing) {
        return;
    }
    if (delta > 0 && step == STEPS - 1) {
        finish(true);
        return;
    }
    showStep(step + delta);
}

void SetupCard::finish(bool apply) {
    if (closing) {
        return;
    }
    closing = true;
    Settings* s = ctrl->getSettings();
    if (apply) {
        // the same settings, written the same way, as the Settings screen
        if (language >= 0 && language != initialLanguage) {
            settingsapply::applyLanguage(ctrl, language);
        }
        switch (folder) {
            case Folder::SUMMA:
                if (!s->getAmbitusNotebooksFolder().empty()) {
                    s->setAmbitusNotebooksFolder("");
                }
                break;
            case Folder::OWN: {
                std::error_code ec;
                fs::create_directories(ownFolder(), ec);
                settingsapply::applyNotebooksFolder(ctrl, ownFolder());
                break;
            }
            case Folder::PICKED:
                settingsapply::applyNotebooksFolder(ctrl, picked);
                break;
        }
        if (fingerMoves != !s->getTouchDrawingEnabled()) {
            settingsapply::applyFingerMovesPage(ctrl, fingerMoves);
        }
        if (penRadial != s->getAmbitusStylusRadial()) {
            settingsapply::applyPenRadial(ctrl, penRadial);
        }
        s->setAmbitusNewPaper(settingsrows::paperId(paper));
        s->setAmbitusNewSize(settingsrows::sizeId(size));
        if (summaInstalled) {
            s->setAmbitusSummaBridge(bridge);
            s->setAmbitusSearchInk(searchInk);
        }
    } else if (theme != initialTheme) {
        settingsapply::applyTheme(ctrl, initialTheme);  // the theme was shown at once; skipping keeps the default
    }
    s->setAmbitusSetupDone(true);
    close();
}

void SetupCard::close() {
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? fadeOut() : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), false);
    // without a transition, child-revealed does not change after the revealer is off
    if (!animationsOn(revealer)) {
        gtk_widget_hide(revealer);
        scheduleDismantle();
    }
    if (focusBack && gtk_widget_get_mapped(focusBack)) {
        gtk_widget_grab_focus(focusBack);
    }
}

void SetupCard::scheduleDismantle() {
    if (dismantleSource) {
        return;
    }
    dismantleSource = g_idle_add(
            +[](gpointer d) -> gboolean {
                auto* self = static_cast<SetupCard*>(d);
                self->dismantleSource = 0;
                self->dismantle();
                return G_SOURCE_REMOVE;
            },
            this);
}

void SetupCard::dismantle() {
    dismantling = true;
    if (revealer) {
        gtk_widget_destroy(revealer);  // onRevealerDestroyed runs, without deleting
    }
    delete this;
}

void SetupCard::onRevealerDestroyed() {
    revealer = nullptr;
    if (openCard == this) {
        openCard = nullptr;
    }
    if (!dismantling) {
        delete this;  // the window itself is going away
    }
}

}  // namespace ambitus
