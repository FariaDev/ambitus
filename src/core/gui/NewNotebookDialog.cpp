/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "NewNotebookDialog.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#include "gui/AmbitusMotion.h"
#include "util/PathUtil.h"
#include "util/i18n.h"

#include "SubjectPicker.h"

namespace ambitus {

using namespace newnotebook;

namespace {
constexpr int CARD_WIDTH = 500;
// the thumbnail of a paper, in the units of the prototype (r5): 86 x 104, with room around it for the ring
constexpr int PAGE_W = 86;
constexpr int PAGE_H = 104;
constexpr int RING = 4;

void addClass(GtkWidget* w, const char* cls) { gtk_style_context_add_class(gtk_widget_get_style_context(w), cls); }

GtkWidget* label(const char* text, const char* cls) {
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    addClass(l, cls);
    return l;
}

void setHex(cairo_t* cr, unsigned rgb, double alpha = 1.) {
    cairo_set_source_rgba(cr, ((rgb >> 16) & 0xff) / 255., ((rgb >> 8) & 0xff) / 255., (rgb & 0xff) / 255., alpha);
}

/// The paper as the prototype draws it (docs/design/r5): the lines of the Summa are the real ones, the other
/// thumbnails the softened versions of what Xournal++ draws
void drawPaper(cairo_t* cr, Paper paper) {
    cairo_set_line_width(cr, .6);
    auto ruled = [&](unsigned line, unsigned margin) {
        setHex(cr, line);
        for (double y = 24; y < PAGE_H; y += 10) {
            cairo_move_to(cr, 0, y);
            cairo_line_to(cr, PAGE_W, y);
        }
        cairo_stroke(cr);
        setHex(cr, margin);
        cairo_move_to(cr, 14, 0);
        cairo_line_to(cr, 14, PAGE_H);
        cairo_stroke(cr);
    };
    switch (paper) {
        case Paper::SUMMA:
            ruled(0xc6c6c6, 0xb89cea);
            break;
        case Paper::BLUE_RULED:
            ruled(0x7fb2f0, 0xf0609e);
            break;
        case Paper::GRID:
            setHex(cr, 0xdedede);
            for (double v = 8; v < PAGE_H; v += 8) {
                cairo_move_to(cr, 0, v);
                cairo_line_to(cr, PAGE_W, v);
                cairo_move_to(cr, v, 0);
                cairo_line_to(cr, v, PAGE_H);
            }
            cairo_stroke(cr);
            break;
        case Paper::DOTTED:
            setHex(cr, 0xa8a8a8);
            for (double y = 8; y < PAGE_H; y += 8) {
                for (double x = 8; x < PAGE_W; x += 8) {
                    cairo_arc(cr, x, y, .7, 0, 2 * G_PI);
                    cairo_fill(cr);
                }
            }
            break;
        case Paper::PLAIN:
            break;
    }
}

const char* paperName(Paper p) {
    switch (p) {
        case Paper::SUMMA:
            return C_("paper", "Summa");
        case Paper::BLUE_RULED:
            return C_("paper", "Blue ruled");
        case Paper::GRID:
            return C_("paper", "Grid");
        case Paper::DOTTED:
            return C_("paper", "Dotted");
        case Paper::PLAIN:
            return C_("paper", "Plain");
    }
    return "";
}

const char* sizeName(PageSize s) {
    switch (s) {
        case PageSize::A4:
            return C_("page size", "A4");
        case PageSize::LETTER:
            return C_("page size", "Letter");
        case PageSize::INFINITE:
            return C_("page size", "Infinite");
    }
    return "";
}

constexpr PageSize SIZES[] = {PageSize::A4, PageSize::LETTER, PageSize::INFINITE};

void clickOf(GtkWidget* w, std::function<void()> f) {
    auto* fn = new std::function<void()>(std::move(f));
    g_signal_connect_data(
            w, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) { (*static_cast<std::function<void()>*>(d))(); }), fn,
            [](gpointer d, GClosure*) { delete static_cast<std::function<void()>*>(d); }, GConnectFlags(0));
}

bool animationsOn(GtkWidget* w) { return !motion::reducedMotion(w); }
}  // namespace

void drawPaperThumbnail(cairo_t* cr, Paper paper) { drawPaper(cr, paper); }
const char* paperThumbnailName(Paper paper) { return paperName(paper); }

NewNotebookDialog::NewNotebookDialog(GtkOverlay* overlay, GtkWindow* window): window(window) {
    // the veil covers the window; the card is in the middle of it. The veil takes the clicks: a name half typed is
    // not lost by a slip of the mouse
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

    card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(card, "amb-cmd");
    addClass(card, "amb-dlg");
    gtk_widget_set_halign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(card, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(card, true);
    gtk_widget_set_size_request(card, CARD_WIDTH, -1);
    gtk_container_add(GTK_CONTAINER(veil), card);

    title = label(_("New notebook"), "amb-dlg-title");
    gtk_box_pack_start(GTK_BOX(card), title, false, false, 0);

    // the line under the title of a draft: the note of the "Keep draft" card
    draftLine = label(_("Stays in the drafts until you keep it in a subject."), "amb-dlg-problem");
    addClass(draftLine, "amb-dlg-note");
    gtk_label_set_line_wrap(GTK_LABEL(draftLine), true);
    gtk_box_pack_start(GTK_BOX(card), draftLine, false, false, 0);

    // the name and the subject, in a box of their own: a draft has neither, so the box is hidden for it
    notebookFields = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(card), notebookFields, false, false, 0);

    // name
    gtk_box_pack_start(GTK_BOX(notebookFields), label(_("Name"), "amb-opt-label"), false, false, 0);
    entry = gtk_entry_new();
    addClass(entry, "amb-cmd-entry");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), false);
    gtk_box_pack_start(GTK_BOX(notebookFields), entry, false, false, 0);
    problem = label("", "amb-dlg-problem");
    gtk_label_set_ellipsize(GTK_LABEL(problem), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(notebookFields), problem, false, false, 0);

    // subject: the field is the one of the "Keep draft" card (SubjectPicker)
    gtk_box_pack_start(GTK_BOX(notebookFields), label(_("Subject"), "amb-opt-label"), false, false, 0);
    subjectPicker = std::make_unique<SubjectPicker>();
    subjectPicker->setOnChanged([this] { suggest(); });
    subjectPicker->setOnPicked([this] { gtk_widget_grab_focus(entry); });
    gtk_box_pack_start(GTK_BOX(notebookFields), subjectPicker->field(), false, false, 0);

    // paper
    gtk_box_pack_start(GTK_BOX(card), label(_("Paper"), "amb-opt-label"), false, false, 0);
    papersBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    for (Paper p: allPapers()) {
        GtkWidget* b = gtk_button_new();
        addClass(b, "amb-paper");
        GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget* area = gtk_drawing_area_new();
        addClass(area, "amb-paper-pg");
        gtk_widget_set_size_request(area, PAGE_W + 2 * RING, PAGE_H + 2 * RING);
        g_object_set_data(G_OBJECT(area), "amb-paper", GINT_TO_POINTER(static_cast<int>(p)));
        g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer data) -> gboolean {
                             auto* self = static_cast<NewNotebookDialog*>(data);
                             auto p = static_cast<Paper>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(a), "amb-paper")));
                             GtkStyleContext* ctx = gtk_widget_get_style_context(a);
                             GdkRGBA ink;
                             gtk_style_context_get_color(ctx, GTK_STATE_FLAG_NORMAL, &ink);
                             const double w = gtk_widget_get_allocated_width(a);
                             const double h = gtk_widget_get_allocated_height(a);
                             if (p == self->paper) {  // the ring, with a gap, as the outline of the prototype
                                 gdk_cairo_set_source_rgba(cr, &ink);
                                 cairo_set_line_width(cr, 2);
                                 cairo_rectangle(cr, 1, 1, w - 2, h - 2);
                                 cairo_stroke(cr);
                             }
                             cairo_save(cr);
                             cairo_translate(cr, (w - PAGE_W) / 2, (h - PAGE_H) / 2);
                             cairo_rectangle(cr, 0, 0, PAGE_W, PAGE_H);
                             cairo_clip_preserve(cr);
                             cairo_set_source_rgb(cr, 1, 1, 1);  // the page stays white, in the dark theme too
                             cairo_fill(cr);
                             drawPaper(cr, p);
                             cairo_restore(cr);
                             cairo_save(cr);
                             cairo_translate(cr, (w - PAGE_W) / 2, (h - PAGE_H) / 2);
                             cairo_set_line_width(cr, 1);
                             cairo_set_source_rgba(cr, ink.red, ink.green, ink.blue, .18);  // a hairline
                             cairo_rectangle(cr, .5, .5, PAGE_W - 1, PAGE_H - 1);
                             cairo_stroke(cr);
                             cairo_restore(cr);
                             return FALSE;
                         }),
                         this);
        gtk_box_pack_start(GTK_BOX(col), area, false, false, 0);
        GtkWidget* name = gtk_label_new(paperName(p));
        addClass(name, "amb-paper-name");
        gtk_box_pack_start(GTK_BOX(col), name, false, false, 0);
        gtk_container_add(GTK_CONTAINER(b), col);
        clickOf(b, [this, p] { choosePaper(p); });
        gtk_box_pack_start(GTK_BOX(papersBox), b, false, false, 0);
        paperButtons.push_back(b);
        watchKeys(b);
    }
    gtk_box_pack_start(GTK_BOX(card), papersBox, false, false, 0);

    // size, and the buttons
    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addClass(foot, "amb-dlg-foot");
    GtkWidget* seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(seg, "amb-seg");
    gtk_widget_set_valign(seg, GTK_ALIGN_CENTER);
    for (PageSize s: SIZES) {
        GtkWidget* b = gtk_toggle_button_new_with_label(sizeName(s));
        gtk_widget_set_can_focus(b, true);
        clickOf(b, [this, s] { chooseSize(s); });
        gtk_container_add(GTK_CONTAINER(seg), b);
        sizeButtons.push_back(b);
        watchKeys(b);
    }
    gtk_box_pack_start(GTK_BOX(foot), seg, false, false, 0);
    createButton = gtk_button_new_with_label(_("Create"));
    addClass(createButton, "amb-dlg-primary");
    clickOf(createButton, [this] { create(); });
    gtk_box_pack_end(GTK_BOX(foot), createButton, false, false, 0);  // the right end: Cancel is on its left
    cancelButton = gtk_button_new_with_label(_("Cancel"));
    addClass(cancelButton, "amb-dlg-ghost");
    clickOf(cancelButton, [this] { close(); });
    gtk_box_pack_end(GTK_BOX(foot), cancelButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(card), foot, false, false, 0);

    g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable*, NewNotebookDialog* self) {
                         if (!self->settingName) {
                             self->nameEdited = true;
                         }
                         self->refresh();
                     }),
                     this);
    watchKeys(entry);
    watchKeys(subjectPicker->field());
    watchKeys(cancelButton);
    watchKeys(createButton);

    // once faded out, the revealer goes away, so it takes no clicks from the window
    g_signal_connect_swapped(revealer, "notify::child-revealed", G_CALLBACK(+[](NewNotebookDialog* self) {
                                 auto* r = GTK_REVEALER(self->revealer);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     gtk_widget_hide(self->revealer);
                                 }
                             }),
                             this);

    gtk_widget_show_all(revealer);
    gtk_widget_set_no_show_all(revealer, true);
    gtk_widget_hide(revealer);
    gtk_overlay_add_overlay(overlay, revealer);
}

NewNotebookDialog::~NewNotebookDialog() {
    if (focusBack) {
        g_object_remove_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }
}

bool NewNotebookDialog::isOpen() const {
    return revealer && GTK_IS_REVEALER(revealer) && gtk_revealer_get_reveal_child(GTK_REVEALER(revealer));
}

void NewNotebookDialog::watchKeys(GtkWidget* w) {
    g_signal_connect(w, "key-press-event",
                     G_CALLBACK(+[](GtkWidget* from, GdkEventKey* e, NewNotebookDialog* self) -> gboolean {
                         return self->onKey(from, e);
                     }),
                     this);
}

bool NewNotebookDialog::onKey(GtkWidget* from, GdkEventKey* e) {
    switch (e->keyval) {
        case GDK_KEY_Escape:
            close();
            return true;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (from == cancelButton || from == createButton) {
                return false;  // the button does what it says
            }
            create();
            return true;
        case GDK_KEY_Up:
        case GDK_KEY_Down:
            // on the subject, the arrows go through the list without opening it
            if (from == subjectPicker->field() && !subjectPicker->empty()) {
                const bool down = e->keyval == GDK_KEY_Down;
                const size_t subject = subjectPicker->selected();
                if (down && subject + 1 < subjectPicker->list().size()) {
                    subjectPicker->choose(subject + 1);
                } else if (!down && subject > 0) {
                    subjectPicker->choose(subject - 1);
                }
                return true;
            }
            return false;
        default:
            return false;
    }
}

std::string NewNotebookDialog::typedName() const { return gtk_entry_get_text(GTK_ENTRY(entry)); }

fs::path NewNotebookDialog::folderOfSubject() const {
    if (subjectPicker->empty()) {
        return {};
    }
    fs::path dir = subjectPicker->list()[subjectPicker->selected()].path;
    // as "New board" does: boards go in their own section of the folder
    return size == PageSize::INFINITE ? dir / _("Boards") : dir;
}

void NewNotebookDialog::open(std::vector<library::Subject> list, const fs::path& near,
                             std::function<void(const Request&)> create) {
    onCreate = std::move(create);
    setDraft(false);
    paper = startPaper;
    size = startSize;
    nameEdited = false;

    // the subject that holds `near`; the deepest when they are nested
    subjectPicker->setSubjects(std::move(list));
    size_t subject = 0;
    if (!near.empty()) {
        size_t bestLen = 0;
        for (size_t i = 0; i < subjectPicker->list().size(); i++) {
            if (Util::isChildOrEquivalent(near, subjectPicker->list()[i].path)) {
                const size_t len = subjectPicker->list()[i].path.native().size();
                if (len >= bestLen) {
                    bestLen = len;
                    subject = i;
                }
            }
        }
    }
    subjectPicker->choose(subject);  // and the name to start with
    choosePaper(paper);
    chooseSize(size);

    showCard(entry);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}

void NewNotebookDialog::openDraft(std::function<void(const Request&)> create) {
    onCreate = std::move(create);
    setDraft(true);
    paper = startDraftPaper;
    size = startDraftSize;
    nameEdited = false;
    choosePaper(paper);
    chooseSize(size);

    // "Create" has the focus: Enter makes the draft, as the shortcut did when it made it at once
    showCard(createButton);
}

void NewNotebookDialog::setDraft(bool isDraft) {
    draft = isDraft;
    gtk_label_set_text(GTK_LABEL(title), isDraft ? _("New draft") : _("New notebook"));
    gtk_widget_set_visible(draftLine, isDraft);
    gtk_widget_set_visible(notebookFields, !isDraft);
}

void NewNotebookDialog::showCard(GtkWidget* focus) {
    if (focusBack) {
        g_object_remove_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
        focusBack = nullptr;
    }
    focusBack = gtk_window_get_focus(window);
    if (focusBack) {
        g_object_add_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }

    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? FADE_IN_MS : 0);
    gtk_widget_show(revealer);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
    gtk_widget_realize(focus);  // the revealer realizes its child late, and the focus change needs it realized
    gtk_widget_grab_focus(focus);
}

void NewNotebookDialog::close() { close(true); }

void NewNotebookDialog::close(bool restoreFocus) {
    if (!isOpen()) {
        return;
    }
    subjectPicker->popdown();
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? FADE_OUT_MS : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), false);
    // without a transition, child-revealed does not change after the revealer is off: hide it here
    if (!animationsOn(revealer)) {
        gtk_widget_hide(revealer);
    }
    if (restoreFocus && focusBack && gtk_widget_get_mapped(focusBack)) {
        gtk_widget_grab_focus(focusBack);
    }
}

void NewNotebookDialog::create() {
    if (!isOpen() || !valid) {
        return;
    }
    Request r;
    r.paper = paper;
    r.size = size;
    if (!draft) {  // a draft has no name and no subject yet: the caller names it
        r.name = cleanName(typedName());
        const fs::path dir = folderOfSubject();
        if (!dir.empty()) {
            r.file = dir / (r.name + ".xopp");
        }
    }
    auto done = std::move(onCreate);
    onCreate = nullptr;
    close(false);
    if (done) {
        done(r);
    }
}

void NewNotebookDialog::choosePaper(Paper p) {
    paper = p;
    for (size_t i = 0; i < paperButtons.size(); i++) {
        auto* ctx = gtk_widget_get_style_context(paperButtons[i]);
        if (allPapers()[i] == p) {
            gtk_style_context_add_class(ctx, "on");
        } else {
            gtk_style_context_remove_class(ctx, "on");
        }
        gtk_widget_queue_draw(paperButtons[i]);
    }
}

void NewNotebookDialog::chooseSize(PageSize s) {
    if (settingSize) {
        return;
    }
    size = s;
    // set_active emits "clicked" too, which comes back here
    settingSize = true;
    for (size_t i = 0; i < sizeButtons.size(); i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sizeButtons[i]), SIZES[i] == s);
    }
    settingSize = false;
    // the infinite page is the board, which has its own dotted paper
    gtk_widget_set_sensitive(papersBox, s != PageSize::INFINITE);
    suggest();
}

void NewNotebookDialog::suggest() {
    if (!nameEdited && !draft) {
        const fs::path dir = folderOfSubject();
        auto exists = [&dir](const std::string& n) {
            std::error_code ec;
            return !dir.empty() && fs::exists(dir / (n + ".xopp"), ec);
        };
        const bool board = size == PageSize::INFINITE;
        const std::string newest = board || subjectPicker->empty() ?
                                           std::string() :
                                           subjectPicker->list()[subjectPicker->selected()].newest;
        const std::string name = suggestName(newest, board ? _("Untitled board") : _("Untitled notebook"), exists);
        settingName = true;
        gtk_entry_set_text(GTK_ENTRY(entry), name.c_str());
        settingName = false;
        if (gtk_widget_has_focus(entry)) {
            gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
        }
    }
    refresh();
}

void NewNotebookDialog::refresh() {
    if (draft) {  // nothing to check: "Create" is on
        valid = true;
        gtk_widget_set_sensitive(createButton, true);
        return;
    }
    const fs::path dir = folderOfSubject();
    auto exists = [&dir](const std::string& n) {
        std::error_code ec;
        return !dir.empty() && fs::exists(dir / (n + ".xopp"), ec);
    };
    const NameCheck check = checkName(typedName(), exists);
    valid = check == NameCheck::OK;
    const char* text = "";
    switch (check) {
        case NameCheck::OK:
            break;
        case NameCheck::EMPTY:
            text = _("Give the notebook a name.");
            break;
        case NameCheck::EXISTS:
            text = _("There is already a notebook with this name here.");
            break;
        case NameCheck::INVALID:
            text = _("Use a name without / or \\, and not starting with a dot.");
            break;
    }
    gtk_label_set_text(GTK_LABEL(problem), text);
    gtk_widget_set_sensitive(createButton, valid);
    auto* ctx = gtk_widget_get_style_context(entry);
    if (valid) {
        gtk_style_context_remove_class(ctx, "amb-dlg-bad");
    } else {
        gtk_style_context_add_class(ctx, "amb-dlg-bad");
    }
}

}  // namespace ambitus
