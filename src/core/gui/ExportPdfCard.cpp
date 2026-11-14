/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "ExportPdfCard.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <system_error>

#include "control/Control.h"
#include "control/jobs/CustomExportJob.h"
#include "control/jobs/XournalScheduler.h"
#include "control/settings/Settings.h"
#include "gui/AmbitusMotion.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfPage.h"
#include "util/PathUtil.h"
#include "util/StringUtils.h"
#include "util/i18n.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"

namespace ambitus {

using namespace exportpdf;

namespace {
constexpr int CARD_WIDTH = 560;     // the .dlg of the prototype is 520; this one has the name and the folder too
constexpr int PREVIEW_WIDTH = 150;  // .pdfgrid: 150px for the thumbnails
constexpr int PREVIEW_HEIGHT = 292;
constexpr int THUMB_W = 96;  // .pdfprev .pp
constexpr int THUMB_MIN_H = 40;
constexpr int THUMB_MAX_H = 136;
constexpr int THUMB_SCALE = 2;  // drawn at twice its size, for the sharper screens

void addClass(GtkWidget* w, const char* cls) { gtk_style_context_add_class(gtk_widget_get_style_context(w), cls); }
void removeClass(GtkWidget* w, const char* cls) {
    gtk_style_context_remove_class(gtk_widget_get_style_context(w), cls);
}
void setClass(GtkWidget* w, const char* cls, bool on) { on ? addClass(w, cls) : removeClass(w, cls); }

GtkWidget* label(const char* text, const char* cls) {
    GtkWidget* l = gtk_label_new(text);
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

/// One row of the prototype's .row: the text at the left and an interruptor at the right
GtkWidget* switchRow(const char* text, GtkWidget** sw, bool on) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    addClass(row, "amb-dlg-row");
    GtkWidget* l = label(text, "amb-dlg-row-label");
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
    *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(*sw), on);
    gtk_widget_set_valign(*sw, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(row), *sw, false, false, 0);
    return row;
}

GtkWidget* icon(const char* name) {
    GtkWidget* img = gtk_image_new_from_icon_name(name, GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(img), 16);
    return img;
}

/// The path as the person knows it: "~" for the home folder
std::string shown(const fs::path& p) {
    std::string s(char_cast(p.u8string()));
    if (const char* home = g_get_home_dir(); home && *home) {
        const std::string h = home;
        if (s == h) {
            return "~";
        }
        if (s.size() > h.size() && s.compare(0, h.size(), h) == 0 && s[h.size()] == '/') {
            return "~" + s.substr(h.size());
        }
    }
    return s;
}

/// The "Open"/"Open folder" of the notice: what the desktop opens for a path
void openPath(GtkWindow* window, const fs::path& p) {
    auto file = Util::toGFile(p);
    char* uri = g_file_get_uri(file.get());
    GError* err = nullptr;
    if (uri && !gtk_show_uri_on_window(window, uri, GDK_CURRENT_TIME, &err)) {
        g_warning("Could not open %s: %s", uri, err ? err->message : "?");
    }
    g_clear_error(&err);
    g_free(uri);
}
}  // namespace

void ExportPdfCard::open(Control* ctrl, GtkOverlay* overlay, GtkWindow* window, const fs::path& suggested) {
    // owns itself: dismantle() deletes it after the fade out
    auto* card = new ExportPdfCard(ctrl, overlay, suggested);
    card->show(window);
}

ExportPdfCard::ExportPdfCard(Control* ctrl, GtkOverlay* overlay, const fs::path& suggested): ctrl(ctrl) {
    Document* doc = ctrl->getDocument();
    {
        std::shared_lock<Document> lock(*doc);
        pageCount = doc->getPageCount();
        backgroundPdf = doc->getPdfFilepath();
    }
    current = std::min(ctrl->getCurrentPageNo(), pageCount ? pageCount - 1 : 0);
    folder = suggested.parent_path();
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) {
        folder = g_get_home_dir();  // nothing to start from: the home folder, not a card that cannot export
    }
    fs::path name = suggested.filename();
    name.replace_extension();
    suggestedName = std::string(char_cast(name.u8string()));
    measurePages();
    build(overlay);
}

ExportPdfCard::~ExportPdfCard() {
    stopRender();
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

void ExportPdfCard::measurePages() {
    Document* doc = ctrl->getDocument();
    std::shared_lock<Document> lock(*doc);
    thumbs.resize(pageCount);
    for (size_t i = 0; i < pageCount; i++) {
        ConstPageRef page = doc->getPage(i);
        const double pw = page ? page->getWidth() : 0;
        const double ph = page ? page->getHeight() : 0;
        thumbs[i].width = THUMB_W;
        thumbs[i].height = pw > 0 && ph > 0 ? std::clamp(static_cast<int>(std::lround(THUMB_W * ph / pw)), THUMB_MIN_H,
                                                         THUMB_MAX_H) :
                                              THUMB_MAX_H;
    }
}

void ExportPdfCard::build(GtkOverlay* overlay) {
    // the veil covers the window and takes the clicks: a choice half made is not lost by a slip of the mouse
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

    gtk_box_pack_start(GTK_BOX(card), label(_("Export as PDF"), "amb-dlg-title"), false, false, 0);

    // .pdfgrid: the thumbnails at the left, the choices at the right
    GtkWidget* grid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_margin_top(grid, 8);
    gtk_box_pack_start(GTK_BOX(grid), buildPreview(), false, false, 0);
    gtk_box_pack_start(GTK_BOX(grid), buildOptions(), true, true, 0);
    gtk_box_pack_start(GTK_BOX(card), grid, true, true, 0);

    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addClass(foot, "amb-dlg-foot");
    advancedButton = gtk_button_new_with_label(_("Advanced…"));
    addClass(advancedButton, "amb-dlg-ghost");
    clickOf(advancedButton, [this] { advanced(); });
    gtk_box_pack_start(GTK_BOX(foot), advancedButton, false, false, 0);
    exportButton = gtk_button_new_with_label(_("Export"));
    addClass(exportButton, "amb-dlg-primary");
    clickOf(exportButton, [this] { exportNow(); });
    gtk_box_pack_end(GTK_BOX(foot), exportButton, false, false, 0);  // the right end: Cancel is on its left
    cancelButton = gtk_button_new_with_label(_("Cancel"));
    addClass(cancelButton, "amb-dlg-ghost");
    clickOf(cancelButton, [this] { cancel(); });
    gtk_box_pack_end(GTK_BOX(foot), cancelButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(card), foot, false, false, 0);

    for (GtkWidget* w:
         {rangeEntry, nameEntry, paperSwitch, openSwitch, folderButton, advancedButton, cancelButton, exportButton}) {
        watchKeys(w);
    }
    for (GtkWidget* b: pageButtons) {
        watchKeys(b);
    }

    // once faded out, the card is taken away
    g_signal_connect_swapped(revealer, "notify::child-revealed", G_CALLBACK(+[](ExportPdfCard* self) {
                                 auto* r = GTK_REVEALER(self->revealer);
                                 if (!gtk_revealer_get_child_revealed(r) && !gtk_revealer_get_reveal_child(r)) {
                                     self->scheduleDismantle();
                                 }
                             }),
                             this);
    g_signal_connect_swapped(revealer, "destroy", G_CALLBACK(+[](ExportPdfCard* self) { self->onRevealerDestroyed(); }),
                             this);
    gtk_widget_show_all(revealer);
    gtk_overlay_add_overlay(overlay, revealer);
}

GtkWidget* ExportPdfCard::buildPreview() {
    GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(col, PREVIEW_WIDTH, -1);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    addClass(scroll, "amb-pdf-prev");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    gtk_widget_set_vexpand(scroll, true);
    GtkWidget* pagesBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    addClass(pagesBox, "amb-pdf-pages");
    gtk_widget_set_halign(pagesBox, GTK_ALIGN_CENTER);
    for (size_t i = 0; i < pageCount; i++) {
        GtkWidget* one = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget* area = gtk_drawing_area_new();
        addClass(area, "amb-pdf-pp");
        gtk_widget_set_size_request(area, thumbs[i].width, thumbs[i].height);
        gtk_widget_set_halign(area, GTK_ALIGN_CENTER);
        g_object_set_data(G_OBJECT(area), "amb-page", GSIZE_TO_POINTER(i));
        g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer d) -> gboolean {
                             auto* self = static_cast<ExportPdfCard*>(d);
                             const size_t page = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(a), "amb-page"));
                             self->drawThumb(page, cr, gtk_widget_get_allocated_width(a),
                                             gtk_widget_get_allocated_height(a));
                             return TRUE;
                         }),
                         this);
        thumbs[i].area = area;
        gtk_box_pack_start(GTK_BOX(one), area, false, false, 0);
        GtkWidget* num = gtk_label_new(std::to_string(i + 1).c_str());
        addClass(num, "amb-pdf-num");
        gtk_box_pack_start(GTK_BOX(one), num, false, false, 0);
        gtk_box_pack_start(GTK_BOX(pagesBox), one, false, false, 0);
    }
    gtk_container_add(GTK_CONTAINER(scroll), pagesBox);
    gtk_box_pack_start(GTK_BOX(col), scroll, true, true, 0);

    count = gtk_label_new("");
    addClass(count, "amb-pdf-count");
    gtk_box_pack_start(GTK_BOX(col), count, false, false, 0);
    return col;
}

GtkWidget* ExportPdfCard::buildOptions() {
    GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    // which pages
    GtkWidget* first = label(_("Pages"), "amb-opt-label");
    gtk_widget_set_margin_top(first, 0);
    gtk_box_pack_start(GTK_BOX(col), first, false, false, 0);
    GtkWidget* seg = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addClass(seg, "amb-seg");
    gtk_widget_set_halign(seg, GTK_ALIGN_START);
    struct Choice {
        Pages which;
        const char* text;
    };
    for (const Choice& c: {Choice{Pages::ALL, C_("export pages", "All")}, Choice{Pages::THIS, _("This page")},
                           Choice{Pages::RANGE, _("Range")}}) {
        GtkWidget* b = gtk_toggle_button_new_with_label(c.text);
        gtk_widget_set_can_focus(b, true);
        const Pages which = c.which;
        clickOf(b, [this, which] { choosePages(which); });
        gtk_container_add(GTK_CONTAINER(seg), b);
        pageButtons.push_back(b);
    }
    gtk_box_pack_start(GTK_BOX(col), seg, false, false, 0);
    rangeEntry = gtk_entry_new();
    addClass(rangeEntry, "amb-cmd-entry");
    addClass(rangeEntry, "amb-pdf-range");
    gtk_entry_set_placeholder_text(GTK_ENTRY(rangeEntry), "1-3, 5");
    gtk_entry_set_activates_default(GTK_ENTRY(rangeEntry), false);
    g_signal_connect(rangeEntry, "changed", G_CALLBACK(+[](GtkEditable*, ExportPdfCard* self) { self->refresh(); }),
                     this);
    gtk_widget_set_margin_top(rangeEntry, 8);
    gtk_box_pack_start(GTK_BOX(col), rangeEntry, false, false, 0);
    rangeProblem = label("", "amb-dlg-problem");
    gtk_label_set_ellipsize(GTK_LABEL(rangeProblem), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(rangeProblem), 1);
    gtk_box_pack_start(GTK_BOX(col), rangeProblem, false, false, 0);

    // the paper, and whether to open the PDF
    GtkWidget* paperRow = switchRow(_("With the paper (ruling, margin)"), &paperSwitch, true);
    gtk_box_pack_start(GTK_BOX(col), paperRow, false, false, 0);
    g_signal_connect(paperSwitch, "notify::active", G_CALLBACK(+[](GObject* sw, GParamSpec*, ExportPdfCard* self) {
                         self->setPaper(gtk_switch_get_active(GTK_SWITCH(sw)));
                     }),
                     this);
    GtkWidget* openRow = switchRow(_("Open the PDF afterwards"), &openSwitch, false);
    addClass(openRow, "last");
    gtk_box_pack_start(GTK_BOX(col), openRow, false, false, 0);

    // the file
    gtk_box_pack_start(GTK_BOX(col), label(_("File name"), "amb-opt-label"), false, false, 0);
    GtkWidget* nameRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    nameEntry = gtk_entry_new();
    addClass(nameEntry, "amb-cmd-entry");
    gtk_entry_set_activates_default(GTK_ENTRY(nameEntry), false);
    gtk_entry_set_text(GTK_ENTRY(nameEntry), suggestedName.c_str());
    g_signal_connect(nameEntry, "changed", G_CALLBACK(+[](GtkEditable*, ExportPdfCard* self) { self->refresh(); }),
                     this);
    gtk_box_pack_start(GTK_BOX(nameRow), nameEntry, true, true, 0);
    GtkWidget* ext = label(".pdf", "amb-pdf-ext");
    gtk_box_pack_start(GTK_BOX(nameRow), ext, false, false, 0);
    gtk_box_pack_start(GTK_BOX(col), nameRow, false, false, 0);

    gtk_box_pack_start(GTK_BOX(col), label(_("Save in"), "amb-opt-label"), false, false, 0);
    folderButton = gtk_button_new();
    addClass(folderButton, "amb-dlg-field");
    GtkWidget* fieldRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(fieldRow), icon("xopp-folder"), false, false, 0);
    folderLabel = label("", "amb-dlg-field-name");
    gtk_label_set_ellipsize(GTK_LABEL(folderLabel), PANGO_ELLIPSIZE_START);
    gtk_label_set_max_width_chars(GTK_LABEL(folderLabel), 1);  // the card keeps its width, however long the path
    gtk_box_pack_start(GTK_BOX(fieldRow), folderLabel, true, true, 0);
    gtk_container_add(GTK_CONTAINER(folderButton), fieldRow);
    clickOf(folderButton, [this] { chooseFolder(); });
    gtk_box_pack_start(GTK_BOX(col), folderButton, false, false, 0);

    problem = label("", "amb-dlg-problem");
    gtk_label_set_ellipsize(GTK_LABEL(problem), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(problem), 1);
    gtk_box_pack_start(GTK_BOX(col), problem, false, false, 0);
    return col;
}

void ExportPdfCard::show(GtkWindow* window) {
    focusBack = gtk_window_get_focus(window);
    if (focusBack) {
        g_object_add_weak_pointer(G_OBJECT(focusBack), reinterpret_cast<gpointer*>(&focusBack));
    }
    gtk_label_set_text(GTK_LABEL(folderLabel), shown(folder).c_str());
    choosePages(Pages::ALL);

    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), animationsOn(revealer) ? fadeIn() : 0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), true);
    gtk_widget_realize(nameEntry);  // the revealer realizes its child late, and the focus change needs it realized
    gtk_widget_grab_focus(nameEntry);
    gtk_editable_select_region(GTK_EDITABLE(nameEntry), 0, -1);  // Enter exports; typing replaces the name
    queueRender();
}

bool ExportPdfCard::isOpen() const {
    return revealer && GTK_IS_REVEALER(revealer) && gtk_revealer_get_reveal_child(GTK_REVEALER(revealer));
}

void ExportPdfCard::watchKeys(GtkWidget* w) {
    g_signal_connect(w, "key-press-event",
                     G_CALLBACK(+[](GtkWidget* from, GdkEventKey* e, ExportPdfCard* self) -> gboolean {
                         return self->onKey(from, e);
                     }),
                     this);
}

bool ExportPdfCard::onKey(GtkWidget* from, GdkEventKey* e) {
    if (!isOpen()) {
        return false;
    }
    switch (e->keyval) {
        case GDK_KEY_Escape:
            cancel();
            return true;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            if (from == folderButton || from == advancedButton || from == cancelButton || from == exportButton) {
                return false;  // the button does what it says
            }
            exportNow();
            return true;
        default:
            return false;
    }
}

std::string ExportPdfCard::typedName() const { return gtk_entry_get_text(GTK_ENTRY(nameEntry)); }

fs::path ExportPdfCard::targetFile() const {
    const std::string file = fileNameOf(typedName());
    return folder / fs::path(std::u8string(file.begin(), file.end()));
}

void ExportPdfCard::choosePages(Pages which) {
    if (settingPages) {
        return;
    }
    pages = which;
    // set_active emits "clicked" too, which comes back here
    settingPages = true;
    const Pages all[] = {Pages::ALL, Pages::THIS, Pages::RANGE};
    for (size_t i = 0; i < pageButtons.size(); i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pageButtons[i]), all[i] == which);
    }
    settingPages = false;
    refresh();  // first: the field of the range is only sensitive, and so can take the focus, once it is the choice
    if (which == Pages::RANGE && gtk_widget_get_realized(rangeEntry)) {
        gtk_widget_grab_focus(rangeEntry);
    }
}

void ExportPdfCard::setPaper(bool on) {
    withPaper = on;
    refresh();
    queueRender();
}

void ExportPdfCard::refresh() {
    selection = select(pages, gtk_entry_get_text(GTK_ENTRY(rangeEntry)), current, pageCount);

    // the pages
    gtk_widget_set_sensitive(rangeEntry, pages == Pages::RANGE);
    const bool badRange = pages == Pages::RANGE && selection.problem == RangeProblem::INVALID;
    setClass(rangeEntry, "amb-dlg-bad", badRange);
    gtk_label_set_text(GTK_LABEL(rangeProblem),
                       badRange ? FS(_F("Use pages like 1-3, 5. The notebook has {1}.") % pageCount).c_str() : "");
    for (size_t i = 0; i < thumbs.size(); i++) {
        setClass(thumbs[i].area, "off", i >= selection.chosen.size() || !selection.chosen[i]);
    }

    const size_t n = selection.count();
    std::string text;
    if (n == 0) {
        text = _("No pages");
    } else if (pages == Pages::THIS) {
        text = FS(_F("1 page (page {1})") % (current + 1));
    } else if (n == 1) {
        text = _("1 page");
    } else {
        text = FS(_F("{1} pages") % n);
    }
    gtk_label_set_text(GTK_LABEL(count), text.c_str());

    // the file
    const NameCheck name = checkName(typedName());
    std::error_code ec;
    const bool folderOk = !folder.empty() && fs::is_directory(folder, ec);
    const fs::path target = targetFile();
    bool overwritesBackground = false;
    if (!backgroundPdf.empty() && name == NameCheck::OK && folderOk) {
        overwritesBackground = fs::weakly_canonical(target, ec) == fs::weakly_canonical(backgroundPdf, ec);
    }
    const bool exists = name == NameCheck::OK && folderOk && !overwritesBackground && fs::exists(target, ec);

    const char* line = "";
    switch (name) {
        case NameCheck::OK:
            break;
        case NameCheck::EMPTY:
            line = _("Give the file a name.");
            break;
        case NameCheck::INVALID:
            line = _("Use a name without / or \\, and not starting with a dot.");
            break;
    }
    bool isNote = false;
    if (!*line && !folderOk) {
        line = _("This folder does not exist.");
    } else if (!*line && overwritesBackground) {
        line = _("Do not overwrite the PDF under the notebook.");
    } else if (!*line && exists) {
        line = _("There is already a file with this name here.");
        isNote = true;  // not an error: it says what the button will do
    }
    gtk_label_set_text(GTK_LABEL(problem), line);
    setClass(problem, "amb-dlg-note", isNote);
    setClass(nameEntry, "amb-dlg-bad", name != NameCheck::OK);

    canExport = selection.usable() && name == NameCheck::OK && folderOk && !overwritesBackground;
    gtk_button_set_label(GTK_BUTTON(exportButton), exists ? _("Replace") : _("Export"));
    gtk_widget_set_sensitive(exportButton, canExport);
}

// ---- the thumbnails ----

void ExportPdfCard::drawThumb(size_t page, cairo_t* cr, int w, int h) {
    cairo_set_source_rgb(cr, 1, 1, 1);  // the page stays white, in the dark theme too
    cairo_paint(cr);
    if (page < thumbs.size()) {
        if (cairo_surface_t* s = thumbs[page].drawn[withPaper ? 1 : 0].get()) {
            cairo_set_source_surface(cr, s, 0, 0);
            cairo_paint(cr);
        }
    }
    (void)w;
    (void)h;
}

void ExportPdfCard::queueRender() {
    if (dismantling) {
        return;
    }
    renderNextPage = 0;  // from the first again: the paper may have changed under a loop that is running
    if (renderSource) {
        return;
    }
    renderSource = g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            +[](gpointer d) -> gboolean {
                auto* self = static_cast<ExportPdfCard*>(d);
                if (self->renderNext()) {
                    return G_SOURCE_CONTINUE;
                }
                self->renderSource = 0;
                return G_SOURCE_REMOVE;
            },
            this, nullptr);
}

void ExportPdfCard::stopRender() {
    if (renderSource) {
        g_source_remove(renderSource);
        renderSource = 0;
    }
}

/// Draws the next page that is missing for the paper as it is now chosen; false when there is none left
bool ExportPdfCard::renderNext() {
    const int set = withPaper ? 1 : 0;
    while (renderNextPage < thumbs.size() && thumbs[renderNextPage].drawn[set]) {
        renderNextPage++;
    }
    if (renderNextPage >= thumbs.size()) {
        return false;
    }
    const size_t i = renderNextPage;
    Thumb& t = thumbs[i];
    Document* doc = ctrl->getDocument();
    {
        std::shared_lock<Document> lock(*doc);
        if (i < doc->getPageCount()) {
            ConstPageRef page = doc->getPage(i);
            const double pw = page->getWidth();
            const double ph = page->getHeight();
            if (pw > 0 && ph > 0) {
                xoj::util::CairoSurfaceSPtr surface(
                        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, t.width * THUMB_SCALE, t.height * THUMB_SCALE),
                        xoj::util::adopt);
                cairo_surface_set_device_scale(surface.get(), THUMB_SCALE, THUMB_SCALE);
                xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
                cairo_set_source_rgb(cr.get(), 1, 1, 1);
                cairo_paint(cr.get());
                const double fit = std::min(t.width / pw, t.height / ph);
                cairo_translate(cr.get(), (t.width - pw * fit) / 2, (t.height - ph * fit) / 2);
                cairo_scale(cr.get(), fit, fit);
                cairo_rectangle(cr.get(), 0, 0, pw, ph);
                cairo_clip(cr.get());
                if (page->getBackgroundType().isPdfPage()) {
                    if (XojPdfPageSPtr pdf = doc->getPdfPage(page->getPdfPageNr())) {
                        pdf->render(cr.get());  // what is under the ink stays with or without the paper
                    }
                }
                const xoj::view::BackgroundFlags flags = {
                        xoj::view::HIDE_PDF_BACKGROUND, xoj::view::SHOW_IMAGE_BACKGROUND,
                        withPaper ? xoj::view::SHOW_RULING_BACKGROUND : xoj::view::HIDE_RULING_BACKGROUND};
                DocumentView().drawPage(page, cr.get(), true, flags);
                t.drawn[set] = std::move(surface);
            }
        }
        if (!t.drawn[set]) {
            // nothing to draw (a page that is gone): a blank one, so the loop moves on
            t.drawn[set] = xoj::util::CairoSurfaceSPtr(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1),
                                                       xoj::util::adopt);
        }
    }
    gtk_widget_queue_draw(t.area);
    renderNextPage++;
    return true;
}

// ---- the choices that leave the card ----

void ExportPdfCard::chooseFolder() {
    if (chooser) {
        return;
    }
    GtkWindow* window = GTK_WINDOW(gtk_widget_get_toplevel(card));
    chooser = gtk_file_chooser_native_new(_("Save the PDF in"), window, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                          _("Choose"), _("_Cancel"));
    std::error_code ec;
    if (!folder.empty() && fs::is_directory(folder, ec)) {
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(chooser), Util::toGFile(folder).get(), nullptr);
    }
    g_signal_connect(chooser, "response", G_CALLBACK(+[](GtkNativeDialog* dlg, int response, gpointer d) {
                         auto* self = static_cast<ExportPdfCard*>(d);
                         if (response == GTK_RESPONSE_ACCEPT) {
                             GFile* f = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dlg));
                             if (f) {
                                 self->folder =
                                         Util::fromGFile(xoj::util::GObjectSPtr<GFile>(f, xoj::util::adopt).get());
                                 gtk_label_set_text(GTK_LABEL(self->folderLabel), shown(self->folder).c_str());
                                 self->refresh();
                             }
                         }
                         self->chooser = nullptr;
                         gtk_native_dialog_destroy(dlg);
                         g_object_unref(dlg);
                         if (self->nameEntry && gtk_widget_get_mapped(self->nameEntry)) {
                             gtk_widget_grab_focus(self->nameEntry);
                         }
                     }),
                     this);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}

void ExportPdfCard::advanced() {
    Control* c = ctrl;
    close(false);
    c->exportAs();  // the dialog of Xournal++: format, layers, quality, and then where to save
}

void ExportPdfCard::cancel() {
    if (!isOpen()) {
        return;
    }
    close(true);
}

void ExportPdfCard::exportNow() {
    if (!isOpen() || !canExport) {
        return;
    }
    const fs::path file = targetFile();
    const bool openAfter = gtk_switch_get_active(GTK_SWITCH(openSwitch));
    auto params = std::make_unique<ExportParameters>(parametersFor(selection, withPaper));
    Control* c = ctrl;
    GtkWindow* window = GTK_WINDOW(gtk_widget_get_toplevel(card));
    close(true);

    if (auto* win = c->getWindow()) {
        win->showToast(_("Exporting the PDF…"), true);
    }
    auto* job = new CustomExportJob(c, file, std::move(params));
    job->setOnPdfDone([c, window, file, openAfter](bool ok, const std::string& error) {
        MainWindow* win = c->getWindow();
        if (!ok) {
            if (win) {
                win->showToastError(_("Could not export the PDF."), error);
            }
            return;
        }
        c->getSettings()->setLastSavePath(file.parent_path());
        if (openAfter) {
            openPath(window, file);
        }
        if (win) {
            // opened already: the folder is what is left to show; otherwise the PDF itself
            if (openAfter) {
                win->showToastAction(_("PDF exported."), _("Open folder"),
                                     [window, dir = file.parent_path()] { openPath(window, dir); });
            } else {
                win->showToastAction(_("PDF exported."), _("Open"), [window, file] { openPath(window, file); });
            }
        }
    });
    c->getScheduler()->addJob(job, JOB_PRIORITY_NONE);
    job->unref();
}

// ---- closing ----

void ExportPdfCard::close(bool restoreFocus) {
    stopRender();
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

void ExportPdfCard::scheduleDismantle() {
    if (dismantleSource) {
        return;
    }
    dismantleSource = g_idle_add(
            +[](gpointer d) -> gboolean {
                auto* self = static_cast<ExportPdfCard*>(d);
                self->dismantleSource = 0;
                self->dismantle();
                return G_SOURCE_REMOVE;
            },
            this);
}

void ExportPdfCard::dismantle() {
    dismantling = true;
    if (revealer) {
        gtk_widget_destroy(revealer);  // onRevealerDestroyed runs, without deleting
    }
    delete this;
}

void ExportPdfCard::onRevealerDestroyed() {
    revealer = nullptr;
    stopRender();
    if (!dismantling) {
        delete this;  // the window itself is going away
    }
}

}  // namespace ambitus
