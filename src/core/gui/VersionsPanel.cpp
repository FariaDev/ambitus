/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "VersionsPanel.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <thread>
#include <utility>

#include "ambitus/RenderWindow.h"
#include "ambitus/Versions.h"
#include "ambitus/VersionsRestore.h"
#include "control/Control.h"
#include "control/PdfCache.h"
#include "control/xojfile/LoadHandler.h"
#include "gui/MainWindow.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "util/StringUtils.h"  // for char_cast
#include "util/Util.h"
#include "util/i18n.h"
#include "view/DocumentView.h"

#include "AmbitusMotion.h"

namespace ambitus {

namespace {
constexpr int PANEL_WIDTH = 250;
constexpr int THUMB_W = 30;
constexpr int THUMB_H = 38;
constexpr int THUMB_DEVICE_SCALE = 2;  ///< the thumbnail is drawn at twice its size, for the sharper screens
constexpr double PAGE_MARGIN = 18;     ///< around the pages, in the preview
constexpr double PAGE_GAP = 14;        ///< between two pages
constexpr double MAX_SCALE = 1.25;     ///< a page is not drawn larger than this, however wide the window is

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

GtkWidget* label(const std::string& text, const char* cssClass) {
    GtkWidget* l = gtk_label_new(text.c_str());
    addClass(l, cssClass);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}

std::string hourOf(versions::Clock::time_point when) {
    GDateTime* dt = g_date_time_new_from_unix_local(versions::Clock::to_time_t(when));
    gchar* s = g_date_time_format(dt, "%H:%M");
    std::string out = s;
    g_free(s);
    g_date_time_unref(dt);
    return out;
}

/// The title of a day of the list: "Today", "Yesterday", or the date
std::string dayTitle(const versions::DayGroup& g) {
    using Day = versions::DayGroup::Day;
    if (g.day == Day::Today) {
        return _("Today");
    }
    if (g.day == Day::Yesterday) {
        return _("Yesterday");
    }
    GDateTime* dt = g_date_time_new_from_unix_local(versions::Clock::to_time_t(g.first));
    GDateTime* now = g_date_time_new_now_local();
    const bool sameYear = g_date_time_get_year(dt) == g_date_time_get_year(now);
    gchar* s = g_date_time_format(dt, sameYear ? _("%B %-d") : _("%B %-d, %Y"));
    std::string out = s;
    g_free(s);
    g_date_time_unref(now);
    g_date_time_unref(dt);
    return out;
}

/// The title of a section as the command palette sets its own: small, upper case, wide
GtkWidget* sectionTitle(const char* title) {
    gchar* upper = g_utf8_strup(title, -1);
    gchar* escaped = g_markup_escape_text(upper, -1);
    gchar* markup = g_strdup_printf("<span weight=\"600\" letter_spacing=\"340\">%s</span>", escaped);
    GtkWidget* l = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(l), markup);
    g_free(markup);
    g_free(escaped);
    g_free(upper);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    addClass(l, "amb-vers-group");
    gtk_widget_show(l);
    return l;
}

/// The first page of a document, small: a white box of the thumbnail's size, the page fit in it
xoj::util::CairoSurfaceSPtr thumbnailOf(const PageRef& page) {
    const double pw = page->getWidth();
    const double ph = page->getHeight();
    if (pw <= 0 || ph <= 0) {
        return {};
    }
    xoj::util::CairoSurfaceSPtr surface(
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, THUMB_W * THUMB_DEVICE_SCALE, THUMB_H * THUMB_DEVICE_SCALE),
            xoj::util::adopt);
    cairo_surface_set_device_scale(surface.get(), THUMB_DEVICE_SCALE, THUMB_DEVICE_SCALE);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    cairo_set_source_rgb(cr.get(), 1, 1, 1);
    cairo_paint(cr.get());
    const double fit = std::min(THUMB_W / pw, THUMB_H / ph);
    cairo_translate(cr.get(), (THUMB_W - pw * fit) / 2, (THUMB_H - ph * fit) / 2);
    cairo_scale(cr.get(), fit, fit);
    cairo_rectangle(cr.get(), 0, 0, pw, ph);
    cairo_clip(cr.get());
    DocumentView().drawPage(page, cr.get(), true);
    return surface;
}

struct ThumbData {
    VersionsPanel* panel;
    fs::path file;
};

/// An opened copy, or why it could not be: read away from the window's thread
struct Loaded {
    std::unique_ptr<Document> doc;
    std::string error;
};

Loaded load(const fs::path& file) {
    Loaded out;
    try {
        LoadHandler handler;
        out.doc = handler.loadDocument(file);
    } catch (const std::exception& e) {
        out.error = "open: " + std::string(char_cast(file.u8string())) + ": " + e.what();
        out.doc.reset();
    }
    return out;
}

/// The widget fades in over the time given, as the motion tokens say; at once when the system wants less movement
void fadeIn(GtkWidget* w, gint64 duration) {
    if (motion::reducedMotion(w)) {
        gtk_widget_set_opacity(w, 1);
        return;
    }
    struct Fade {
        gint64 start = -1;
        gint64 duration;
    };
    gtk_widget_set_opacity(w, 0);
    gtk_widget_add_tick_callback(
            w,
            +[](GtkWidget* widget, GdkFrameClock* clock, gpointer data) -> gboolean {
                auto* fade = static_cast<Fade*>(data);
                const gint64 now = gdk_frame_clock_get_frame_time(clock);
                if (fade->start < 0) {
                    fade->start = now;
                }
                const double t = std::min(1.0, static_cast<double>(now - fade->start) / fade->duration);
                gtk_widget_set_opacity(widget, motion::easeOut(t));
                return t < 1 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
            },
            new Fade{-1, duration}, +[](gpointer d) { delete static_cast<Fade*>(d); });
}
}  // namespace

VersionsPanel::VersionsPanel(Control* control, GtkOverlay* overlay, GtkWidget* area, GtkWidget* focusBack):
        control(control), overlay(overlay), area(area), focusBack(focusBack) {
    // ---- the panel at the right of the page
    panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(panel, "amb-vers");
    gtk_widget_set_size_request(panel, PANEL_WIDTH, -1);

    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    addClass(head, "amb-vers-head");
    GtkWidget* title = label(_("Versions"), "amb-vers-title");
    gtk_box_pack_start(GTK_BOX(head), title, true, true, 0);
    GtkWidget* closeButton = gtk_button_new();
    addClass(closeButton, "amb-vers-close");
    gtk_widget_set_focus_on_click(closeButton, false);
    gtk_button_set_image(GTK_BUTTON(closeButton), gtk_image_new_from_icon_name("xopp-x", GTK_ICON_SIZE_BUTTON));
    gtk_image_set_pixel_size(GTK_IMAGE(gtk_button_get_image(GTK_BUTTON(closeButton))), 14);
    gtk_widget_set_tooltip_text(closeButton, _("Close"));
    g_signal_connect_swapped(closeButton, "clicked", G_CALLBACK(+[](VersionsPanel* self) { self->close(); }), this);
    gtk_box_pack_end(GTK_BOX(head), closeButton, false, false, 0);
    gtk_box_pack_start(GTK_BOX(panel), head, false, false, 0);

    listScroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(listScroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    list = gtk_list_box_new();
    addClass(list, "amb-vers-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), true);
    // the title of each day goes above its first row
    gtk_list_box_set_header_func(
            GTK_LIST_BOX(list),
            +[](GtkListBoxRow* row, GtkListBoxRow* before, gpointer) {
                auto group = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "amb-group"));
                if (!before || GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(before), "amb-group")) != group) {
                    gtk_list_box_row_set_header(
                            row, sectionTitle(static_cast<const char*>(g_object_get_data(G_OBJECT(row), "amb-title"))));
                } else {
                    gtk_list_box_row_set_header(row, nullptr);
                }
            },
            nullptr, nullptr);
    g_signal_connect(list, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, VersionsPanel* self) {
                         const auto at = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "amb-row"));
                         if (at == 0 || at > self->rows.size()) {
                             return;
                         }
                         const fs::path file = self->rows[at - 1].file;
                         if (file.empty()) {
                             self->leavePreview();
                             return;
                         }
                         for (auto const& v: self->copies) {
                             if (v.file == file) {
                                 self->show(v);
                                 return;
                             }
                         }
                     }),
                     this);
    gtk_container_add(GTK_CONTAINER(listScroll), list);
    gtk_box_pack_start(GTK_BOX(panel), listScroll, true, true, 0);

    emptyLabel = label("", "amb-vers-empty");
    gtk_label_set_line_wrap(GTK_LABEL(emptyLabel), true);
    gtk_label_set_max_width_chars(GTK_LABEL(emptyLabel), 24);  // wraps to the width of the panel, never widens it
    gtk_widget_set_valign(emptyLabel, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(panel), emptyLabel, true, true, 0);

    GtkWidget* foot = label(_("One every ten minutes of work; the 30 newest stay."), "amb-vers-foot");
    gtk_label_set_line_wrap(GTK_LABEL(foot), true);
    gtk_label_set_max_width_chars(GTK_LABEL(foot), 24);
    gtk_box_pack_end(GTK_BOX(panel), foot, false, false, 0);

    // Esc leaves the preview from the panel too
    g_signal_connect(panel, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* e, VersionsPanel* self) -> gboolean {
                         if (e->keyval == GDK_KEY_Escape && self->isPreviewing()) {
                             self->leavePreview();
                             return TRUE;
                         }
                         return FALSE;
                     }),
                     this);

    gtk_widget_show_all(panel);
    gtk_widget_hide(emptyLabel);
    gtk_widget_set_no_show_all(emptyLabel, true);
    gtk_widget_set_no_show_all(panel, true);
    gtk_widget_hide(panel);

    // ---- the preview of a copy, over the page
    preview = gtk_event_box_new();  // takes the clicks and the wheel: nothing goes through to the page below
    addClass(preview, "amb-vers-preview");
    GtkWidget* column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(preview), column);

    banner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    addClass(banner, "amb-vers-banner");
    GtkWidget* eye = gtk_image_new_from_icon_name("xopp-eye", GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(eye), 16);
    gtk_box_pack_start(GTK_BOX(banner), eye, false, false, 0);
    bannerText = label("", "amb-vers-banner-text");
    gtk_box_pack_start(GTK_BOX(banner), bannerText, false, false, 0);
    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(banner), spacer, true, true, 0);
    GtkWidget* back = gtk_button_new_with_label(_("Back to the current one"));
    addClass(back, "amb-vers-ghost");
    gtk_widget_set_focus_on_click(back, false);
    g_signal_connect_swapped(back, "clicked", G_CALLBACK(+[](VersionsPanel* self) { self->leavePreview(); }), this);
    gtk_box_pack_start(GTK_BOX(banner), back, false, false, 0);
    GtkWidget* restore = gtk_button_new_with_label(_("Restore this one"));
    addClass(restore, "amb-vers-primary");
    gtk_widget_set_focus_on_click(restore, false);
    g_signal_connect_swapped(restore, "clicked", G_CALLBACK(+[](VersionsPanel* self) {
                                 if (self->shown) {
                                     restoreVersion(self->control, self->notebook, *self->shown);
                                 }
                             }),
                             this);
    gtk_box_pack_start(GTK_BOX(banner), restore, false, false, 0);
    gtk_box_pack_start(GTK_BOX(column), banner, false, false, 0);

    stage = gtk_scrolled_window_new(nullptr, nullptr);
    addClass(stage, "amb-vers-stage");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(stage), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_can_focus(stage, true);
    pages = gtk_drawing_area_new();
    addClass(pages, "amb-vers-pages");
    g_signal_connect(pages, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, VersionsPanel* self) -> gboolean {
                         self->drawPreview(cr);
                         return TRUE;
                     }),
                     this);
    g_signal_connect(pages, "size-allocate",
                     G_CALLBACK(+[](GtkWidget*, GdkRectangle* a, VersionsPanel* self) { self->layoutPages(a->width); }),
                     this);
    gtk_container_add(GTK_CONTAINER(stage), pages);
    gtk_box_pack_start(GTK_BOX(column), stage, true, true, 0);

    g_signal_connect(preview, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* e, VersionsPanel* self) -> gboolean {
                         if (e->keyval == GDK_KEY_Escape) {
                             self->leavePreview();
                             return TRUE;
                         }
                         return FALSE;
                     }),
                     this);

    gtk_widget_show_all(preview);
    gtk_widget_set_no_show_all(preview, true);
    gtk_widget_hide(preview);
    gtk_overlay_add_overlay(overlay, preview);
    positionId = g_signal_connect(overlay, "get-child-position", G_CALLBACK(onChildPosition), this);
}

VersionsPanel::~VersionsPanel() {
    alive.reset();  // what a reader of copies still has to say is dropped
    if (sizeIdle) {
        g_source_remove(sizeIdle);
    }
    g_signal_handler_disconnect(overlay, positionId);
}

gboolean VersionsPanel::onChildPosition(GtkOverlay* overlay, GtkWidget* w, GdkRectangle* alloc, VersionsPanel* self) {
    if (w != self->preview || !self->area) {
        return false;
    }
    int x = 0, y = 0;
    const int areaW = gtk_widget_get_allocated_width(self->area);
    const int areaH = gtk_widget_get_allocated_height(self->area);
    if (areaW <= 0 || !gtk_widget_translate_coordinates(self->area, GTK_WIDGET(overlay), 0, 0, &x, &y)) {
        return false;
    }
    alloc->x = x;
    alloc->y = y;
    alloc->width = areaW;
    alloc->height = areaH;
    return true;
}

bool VersionsPanel::isOpen() const { return gtk_widget_get_visible(panel); }

void VersionsPanel::toggle() {
    if (isOpen()) {
        close();
        return;
    }
    gtk_widget_show(panel);
    notebook = activeNotebook();
    built = false;
    refresh();
}

void VersionsPanel::close() {
    leavePreview();
    gtk_widget_hide(panel);
}

fs::path VersionsPanel::activeNotebook() const {
    for (auto const& t: control->getTabs()) {
        if (t.active) {
            return t.path;
        }
    }
    return {};
}

void VersionsPanel::tabsChanged() {
    if (!isOpen()) {
        return;
    }
    const fs::path active = activeNotebook();
    if (active != notebook) {
        leavePreview();
        notebook = active;
        built = false;
    }
    refresh();
}

void VersionsPanel::restored() {
    leavePreview();
    if (isOpen()) {
        refresh();
    }
}

void VersionsPanel::refresh() {
    std::vector<versions::Version> now;
    if (!notebook.empty()) {
        now = versions::list(versions::defaultRoot(), notebook);
        // none here: the notebook may have been moved outside the program, with its copies under the old name
        if (now.empty() && versions::adoptOrphan(versions::defaultRoot(), notebook)) {
            now = versions::list(versions::defaultRoot(), notebook);
        }
    }
    const bool same = built && now.size() == copies.size() &&
                      std::equal(now.begin(), now.end(), copies.begin(),
                                 [](auto const& a, auto const& b) { return a.file == b.file && a.when == b.when; });
    if (same) {
        return;
    }
    copies = std::move(now);
    built = true;
    if (shown && std::none_of(copies.begin(), copies.end(), [&](auto const& v) { return v.file == shown->file; })) {
        leavePreview();  // the copy that was on screen was the oldest, and went away with a new one
    }
    // what is known of copies that are gone is not kept
    for (auto it = infos.begin(); it != infos.end();) {
        const bool gone = !it->first.empty() && std::none_of(copies.begin(), copies.end(),
                                                             [&](auto const& v) { return v.file == it->first; });
        it = gone ? infos.erase(it) : std::next(it);
    }
    rebuild();
}

void VersionsPanel::showEmpty(const std::string& text) {
    gtk_widget_hide(listScroll);
    gtk_label_set_text(GTK_LABEL(emptyLabel), text.c_str());
    gtk_widget_show(emptyLabel);
}

void VersionsPanel::rebuild() {
    gtk_container_foreach(GTK_CONTAINER(list), +[](GtkWidget* w, gpointer) { gtk_widget_destroy(w); }, nullptr);
    rows.clear();
    toRead.clear();
    if (notebook.empty()) {
        showEmpty(_("Save the notebook first: its versions are kept from then on."));
        return;
    }
    if (copies.empty()) {
        showEmpty(_("No version yet. The first one is kept the next time the notebook is saved."));
        return;
    }
    gtk_widget_hide(emptyLabel);
    gtk_widget_show(listScroll);
    readCurrent();

    const auto now = versions::Clock::now();
    auto groups = versions::groupByDay(copies, now);
    if (groups.empty() || groups.front().day != versions::DayGroup::Day::Today) {
        // "Now" is always under today, with or without a copy made today
        versions::DayGroup today;
        today.day = versions::DayGroup::Day::Today;
        today.first = now;
        groups.insert(groups.begin(), std::move(today));
    }

    auto addRow = [&](const fs::path& file, const std::string& primary, const std::string& secondary, guint group,
                      const std::string& groupTitle) {
        GtkWidget* row = gtk_list_box_row_new();
        addClass(row, "amb-vers-row");
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        GtkWidget* thumb = gtk_drawing_area_new();
        addClass(thumb, "amb-vers-thumb");
        gtk_widget_set_size_request(thumb, THUMB_W, THUMB_H);
        gtk_widget_set_valign(thumb, GTK_ALIGN_CENTER);
        g_object_set_data_full(
                G_OBJECT(thumb), "amb-thumb", new ThumbData{this, file},
                +[](gpointer d) { delete static_cast<ThumbData*>(d); });
        g_signal_connect(thumb, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
                             auto* data = static_cast<ThumbData*>(g_object_get_data(G_OBJECT(w), "amb-thumb"));
                             const int width = gtk_widget_get_allocated_width(w);
                             const int height = gtk_widget_get_allocated_height(w);
                             auto it = data->panel->infos.find(data->file);
                             if (it != data->panel->infos.end() && it->second->thumb) {
                                 cairo_set_source_surface(cr, it->second->thumb.get(), (width - THUMB_W) / 2.0,
                                                          (height - THUMB_H) / 2.0);
                                 cairo_paint(cr);
                             } else {
                                 cairo_set_source_rgb(cr, 1, 1, 1);
                                 cairo_paint(cr);
                             }
                             // the border takes the colour the style gives the widget
                             GdkRGBA border;
                             gtk_style_context_get_color(gtk_widget_get_style_context(w), GTK_STATE_FLAG_NORMAL,
                                                         &border);
                             gdk_cairo_set_source_rgba(cr, &border);
                             cairo_set_line_width(cr, 1);
                             cairo_rectangle(cr, .5, .5, width - 1, height - 1);
                             cairo_stroke(cr);
                             return TRUE;
                         }),
                         nullptr);
        gtk_box_pack_start(GTK_BOX(box), thumb, false, false, 0);
        GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(text), label(primary, "amb-vers-time"), false, false, 0);
        GtkWidget* sub = label(secondary, "amb-vers-sub");
        gtk_box_pack_start(GTK_BOX(text), sub, false, false, 0);
        gtk_box_pack_start(GTK_BOX(box), text, true, true, 0);
        gtk_container_add(GTK_CONTAINER(row), box);
        rows.push_back({file, sub, thumb});
        g_object_set_data(G_OBJECT(row), "amb-row", GUINT_TO_POINTER(rows.size()));
        g_object_set_data(G_OBJECT(row), "amb-group", GUINT_TO_POINTER(group));
        g_object_set_data_full(G_OBJECT(row), "amb-title", g_strdup(groupTitle.c_str()), g_free);
        gtk_widget_show_all(row);
        gtk_container_add(GTK_CONTAINER(list), row);
        updateRow(rows.back());
    };
    guint groupNo = 0;
    for (auto const& g: groups) {
        const std::string title = dayTitle(g);
        groupNo++;
        if (g.day == versions::DayGroup::Day::Today) {
            addRow({}, _("Now"), _("the current one"), groupNo, title);
        }
        for (size_t i: g.items) {
            addRow(copies[i].file, hourOf(copies[i].when), "", groupNo, title);
            if (auto it = infos.find(copies[i].file); it == infos.end() || it->second->pages < 0) {
                toRead.push_back(copies[i].file);
            }
        }
    }
    selectRow(shown ? shown->file : fs::path());
    pumpLoader();
}

void VersionsPanel::selectRow(const fs::path& file) {
    for (size_t i = 0; i < rows.size(); i++) {
        if (rows[i].file == file) {
            GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), static_cast<int>(i));
            gtk_list_box_select_row(GTK_LIST_BOX(list), row);  // row-activated is the click; this one is not heard
            return;
        }
    }
}

void VersionsPanel::updateRow(const Row& row) {
    auto it = infos.find(row.file);
    if (it == infos.end()) {
        return;
    }
    if (!row.file.empty() && it->second->pages > 0) {
        const int n = it->second->pages;
        gtk_label_set_text(GTK_LABEL(row.sub),
                           FS(PlaceholderString(ngettext("{1} page", "{1} pages", static_cast<unsigned long>(n))) %
                              std::to_string(n))
                                   .c_str());
    }
    gtk_widget_queue_draw(row.thumb);
}

/// What "Now" shows: the first page of the notebook as it is
void VersionsPanel::readCurrent() {
    auto info = std::make_shared<Info>();
    Document* d = control->getDocument();
    d->lock_shared();
    info->pages = static_cast<int>(d->getPageCount());
    if (info->pages > 0) {
        info->thumb = thumbnailOf(d->getPage(0));
    }
    d->unlock_shared();
    infos[fs::path()] = std::move(info);
}

void VersionsPanel::pumpLoader() {
    if (reading) {
        return;
    }
    while (!toRead.empty()) {
        const fs::path file = toRead.front();
        toRead.erase(toRead.begin());
        if (auto it = infos.find(file); it != infos.end() && it->second->pages >= 0) {
            continue;
        }
        reading = true;
        std::thread([file, weak = std::weak_ptr<bool>(alive), this] {
            auto info = std::make_shared<Info>();
            Loaded loaded = load(file);
            if (loaded.doc) {
                info->pages = static_cast<int>(loaded.doc->getPageCount());
                if (info->pages > 0) {
                    info->thumb = thumbnailOf(loaded.doc->getPage(0));
                }
            } else {
                info->pages = 0;
            }
            loaded.doc.reset();
            Util::execInUiThread([file, weak, info, this] {
                if (weak.lock()) {
                    infoRead(file, info);
                }
            });
        }).detach();
        return;
    }
}

void VersionsPanel::infoRead(const fs::path& file, std::shared_ptr<Info> info) {
    reading = false;
    infos[file] = std::move(info);
    for (auto const& row: rows) {
        if (row.file == file) {
            updateRow(row);
        }
    }
    pumpLoader();
}

void VersionsPanel::show(const versions::Version& v) {
    if (shown && shown->file == v.file) {
        return;
    }
    shown = v;
    const uint64_t mine = ++serial;
    doc.reset();
    cache.reset();
    surfaces.clear();
    tops.clear();
    layoutWidth = 0;
    totalHeight = 0;
    selectRow(v.file);

    const auto now = versions::Clock::now();
    const auto groups = versions::groupByDay({v}, now);
    const bool today = !groups.empty() && groups.front().day == versions::DayGroup::Day::Today;
    const std::string when = today ? hourOf(v.when) : versions::describe(v.when, now);
    gtk_label_set_text(GTK_LABEL(bannerText), FS(_F("{1} · read only") % when).c_str());
    gtk_widget_show(preview);
    fadeIn(banner, motion::FAST_US);
    gtk_widget_queue_draw(pages);
    gtk_widget_grab_focus(stage);

    std::thread([v, mine, weak = std::weak_ptr<bool>(alive), this] {
        Loaded loaded = load(v.file);
        Util::execInUiThread([v, mine, weak, this, loaded = std::move(loaded)]() mutable {
            if (weak.lock()) {
                documentRead(mine, v, std::move(loaded.doc), std::move(loaded.error));
            }
        });
    }).detach();
}

void VersionsPanel::documentRead(uint64_t request, const versions::Version& v, std::unique_ptr<Document> loaded,
                                 std::string error) {
    if (request != serial || !shown) {
        return;  // another copy was asked for since, or the preview was left
    }
    if (!loaded) {
        leavePreview();
        if (auto* win = control->getWindow()) {
            win->showToastError(_("The version could not be opened."), error);
        }
        return;
    }
    doc = std::move(loaded);
    if (doc->getPdfPageCount() > 0) {
        cache = std::make_unique<PdfCache>(doc->getPdfDocument(), nullptr);
    }
    layoutWidth = 0;
    layoutPages(gtk_widget_get_allocated_width(pages));
    gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(stage)), 0);
    gtk_widget_queue_draw(pages);
}

void VersionsPanel::leavePreview() {
    if (!shown) {
        return;
    }
    shown.reset();
    ++serial;
    doc.reset();
    cache.reset();
    surfaces.clear();
    tops.clear();
    layoutWidth = 0;
    gtk_widget_hide(preview);
    gtk_widget_set_opacity(banner, 1);
    if (isOpen()) {
        selectRow(fs::path());
    }
    if (focusBack) {
        gtk_widget_grab_focus(focusBack);
    }
}

void VersionsPanel::layoutPages(int width) {
    if (!doc || width <= 1 || width == layoutWidth) {
        return;
    }
    layoutWidth = width;
    const size_t count = doc->getPageCount();
    double widest = 1;
    for (size_t i = 0; i < count; i++) {
        widest = std::max(widest, doc->getPage(i)->getWidth());
    }
    scale = std::clamp((width - 2 * PAGE_MARGIN) / widest, 0.1, MAX_SCALE);
    surfaces.clear();
    tops.clear();
    double y = PAGE_MARGIN;
    for (size_t i = 0; i < count; i++) {
        tops.push_back(y);
        y += doc->getPage(i)->getHeight() * scale + PAGE_GAP;
    }
    totalHeight = std::max(0.0, y - PAGE_GAP + PAGE_MARGIN);
    if (!sizeIdle) {
        // the height is asked for once the allocation that called this is over
        sizeIdle = g_idle_add(
                +[](gpointer d) -> gboolean {
                    auto* self = static_cast<VersionsPanel*>(d);
                    self->sizeIdle = 0;
                    gtk_widget_set_size_request(self->pages, -1, static_cast<int>(std::ceil(self->totalHeight)));
                    return G_SOURCE_REMOVE;
                },
                this);
    }
}

cairo_surface_t* VersionsPanel::pageSurface(size_t i) {
    auto it = surfaces.find(i);
    if (it != surfaces.end()) {
        return it->second.get();
    }
    PageRef page = doc->getPage(i);
    const int dpi = gtk_widget_get_scale_factor(pages);
    if (render::isLarge(page->getWidth(), page->getHeight(), scale, dpi)) {
        return nullptr;  // too large to keep whole: drawn straight to the screen
    }
    xoj::util::CairoSurfaceSPtr surface(
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(std::ceil(page->getWidth() * scale * dpi)),
                                       static_cast<int>(std::ceil(page->getHeight() * scale * dpi))),
            xoj::util::adopt);
    cairo_surface_set_device_scale(surface.get(), dpi, dpi);
    xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
    cairo_scale(cr.get(), scale, scale);
    DocumentView view;
    view.setPdfCache(cache.get());
    view.drawPage(page, cr.get(), true);
    return (surfaces[i] = std::move(surface)).get();
}

void VersionsPanel::drawPreview(cairo_t* cr) {
    const int width = gtk_widget_get_allocated_width(pages);
    const int height = gtk_widget_get_allocated_height(pages);
    GtkStyleContext* style = gtk_widget_get_style_context(pages);
    gtk_render_background(style, cr, 0, 0, width, height);
    if (!doc) {
        return;
    }
    double clipTop = 0, clipBottom = height, ignoredX = 0, ignoredX2 = 0;
    cairo_clip_extents(cr, &ignoredX, &clipTop, &ignoredX2, &clipBottom);

    std::vector<size_t> visible;
    for (size_t i = 0; i < tops.size(); i++) {
        PageRef page = doc->getPage(i);
        const double h = page->getHeight() * scale;
        if (tops[i] + h < clipTop || tops[i] > clipBottom) {
            continue;
        }
        visible.push_back(i);
        const double w = page->getWidth() * scale;
        const double x = std::round((width - w) / 2);
        const double y = std::round(tops[i]);
        // a soft edge: three frames, each fainter than the one inside it
        for (int k = 3; k >= 1; k--) {
            cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
            cairo_rectangle(cr, x - k, y - k + 1, w + 2 * k, h + 2 * k);
            cairo_fill(cr);
        }
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, x - .5, y - .5, w + 1, h + 1);
        cairo_stroke(cr);
        if (cairo_surface_t* s = pageSurface(i)) {
            cairo_set_source_surface(cr, s, x, y);
            cairo_paint(cr);
        } else {
            cairo_save(cr);
            cairo_translate(cr, x, y);
            cairo_scale(cr, scale, scale);
            cairo_rectangle(cr, 0, 0, page->getWidth(), page->getHeight());
            cairo_clip(cr);
            DocumentView view;
            view.setPdfCache(cache.get());
            view.drawPage(page, cr, true);
            cairo_restore(cr);
        }
    }
    // the pages that scrolled out of sight are drawn again if they come back: only the ones on screen are kept
    for (auto it = surfaces.begin(); it != surfaces.end();) {
        it = std::find(visible.begin(), visible.end(), it->first) == visible.end() ? surfaces.erase(it) : std::next(it);
    }
}

}  // namespace ambitus
