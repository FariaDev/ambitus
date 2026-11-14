#include "LibraryView.h"

#include <algorithm>  // for sort, find_if
#include <ctime>      // for time
#include <random>     // for random_device
#include <set>        // for set
#include <thread>     // for thread

#include <glib/gstdio.h>  // for g_stat

#include "util/PathUtil.h"             // for fromGFilename, toGFilename
#include "util/StringUtils.h"          // for char_cast, StringUtils
#include "util/XojPreviewExtractor.h"  // for XojPreviewExtractor
#include "util/i18n.h"                 // for _

namespace ambitus::library {

namespace {
constexpr int COVER_W = 152;
constexpr int COVER_H = 200;
constexpr int GRID_COLUMN_SPACING = 34;
constexpr int BOARD_W = 2 * COVER_W + GRID_COLUMN_SPACING;  ///< a board takes two columns
constexpr int MINI_W = 52;
constexpr int MINI_H = 68;
constexpr size_t MAX_RECENT = 24;
constexpr const char* SWATCHES[] = {"#5b4a86", "#2f5d6b", "#8a5a3a", "#4d6b45",
                                    "#8a7232", "#9b3a5a", "#3d3d3d", "#d9d2c3"};

auto utf8(const fs::path& p) -> std::string { return std::string(char_cast(p.u8string())); }

auto label(const std::string& text, const char* cssClass = nullptr) -> GtkWidget* {
    GtkWidget* l = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
    if (cssClass) {
        gtk_style_context_add_class(gtk_widget_get_style_context(l), cssClass);
    }
    return l;
}

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

auto iconButton(const char* icon, const char* text, const char* cssClass) -> GtkWidget* {
    GtkWidget* b = gtk_button_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(box), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_container_add(GTK_CONTAINER(box), gtk_label_new(text));
    gtk_container_add(GTK_CONTAINER(b), box);
    addClass(b, cssClass);
    return b;
}

/// "hoje", "ontem" or "12 nov"
auto relativeDate(time_t t) -> std::string {
    GDateTime* then = g_date_time_new_from_unix_local(t);
    GDateTime* now = g_date_time_new_now_local();
    const int days = g_date_time_get_day_of_year(now) - g_date_time_get_day_of_year(then);
    const bool sameYear = g_date_time_get_year(now) == g_date_time_get_year(then);
    std::string out;
    if (sameYear && days == 0) {
        out = _("today");
    } else if (sameYear && days == 1) {
        out = _("yesterday");
    } else {
        gchar* s = g_date_time_format(then, sameYear ? "%e %b" : "%b %Y");
        out = s ? s : "";
        g_free(s);
        out.erase(0, out.find_first_not_of(' '));
    }
    g_date_time_unref(then);
    g_date_time_unref(now);
    return out;
}

/// first component of the folder path: the section a notebook goes in
auto sectionOf(const Notebook& nb) -> std::string { return nb.folder.substr(0, nb.folder.find('/')); }

auto matches(const std::string& name, const std::string& filter) -> bool {
    if (filter.empty()) {
        return true;
    }
    gchar* a = g_utf8_casefold(name.c_str(), -1);
    gchar* b = g_utf8_casefold(filter.c_str(), -1);
    gchar* na = g_utf8_normalize(a, -1, G_NORMALIZE_ALL);
    gchar* nb = g_utf8_normalize(b, -1, G_NORMALIZE_ALL);
    bool found = strstr(na, nb) != nullptr;
    g_free(a), g_free(b), g_free(na), g_free(nb);
    return found;
}

/// A drawing area that paints a cover; the CoverInput lives as long as the widget.
auto coverArea(int w, int h, std::function<CoverInput()> input) -> GtkWidget* {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, w, h);
    gtk_widget_set_halign(area, GTK_ALIGN_START);
    auto* fn = new std::function<CoverInput()>(std::move(input));
    g_object_set_data_full(G_OBJECT(area), "amb-cover", fn,
                           [](gpointer p) { delete static_cast<std::function<CoverInput()>*>(p); });
    g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* a, cairo_t* cr, gpointer) -> gboolean {
                         auto* f =
                                 static_cast<std::function<CoverInput()>*>(g_object_get_data(G_OBJECT(a), "amb-cover"));
                         drawCover(cr, gtk_widget_get_allocated_width(a), gtk_widget_get_allocated_height(a), (*f)());
                         return false;
                     }),
                     nullptr);
    return area;
}

template <typename F>
void onClicked(GtkWidget* button, F&& f) {
    auto* fn = new std::function<void()>(std::forward<F>(f));
    g_signal_connect_data(
            button, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer d) { (*static_cast<std::function<void()>*>(d))(); }),
            fn, [](gpointer d, GClosure*) { delete static_cast<std::function<void()>*>(d); }, GConnectFlags(0));
}
}  // namespace

LibraryView::LibraryView(fs::path storeFile, fs::path indexFile, Actions a):
        store(std::move(storeFile)), actions(std::move(a)), indexFile(std::move(indexFile)) {
    root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(root, "library");
    g_object_ref_sink(root);
    buildSide();

    GtkWidget* main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(main, true);
    addClass(main, "amb-lib-main");

    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    addClass(head, "amb-lib-head");
    title = label("", "amb-lib-title");
    gtk_container_add(GTK_CONTAINER(head), title);
    subtitle = label("", "amb-lib-subtitle");
    gtk_widget_set_hexpand(subtitle, true);
    gtk_widget_set_valign(subtitle, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(head), subtitle);
    GtkWidget* boardBtn = iconButton("view-grid-symbolic", _("New board"), "amb-secondary");
    onClicked(boardBtn, [this] {
        if (actions.newBoard) {
            actions.newBoard(sourceFolder(current));
        }
    });
    gtk_container_add(GTK_CONTAINER(head), boardBtn);
    GtkWidget* newBtn = iconButton("list-add-symbolic", _("New notebook"), "amb-primary");
    onClicked(newBtn, [this] {
        if (actions.newNotebook) {
            actions.newNotebook(sourceFolder(current));
        }
    });
    gtk_container_add(GTK_CONTAINER(head), newBtn);
    gtk_container_add(GTK_CONTAINER(main), head);

    scroller = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, true);
    body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(body, "amb-lib-body");
    gtk_container_add(GTK_CONTAINER(scroller), body);
    gtk_container_add(GTK_CONTAINER(main), scroller);

    gtk_container_add(GTK_CONTAINER(root), main);
    gtk_widget_show_all(root);

    refresh();
}

LibraryView::~LibraryView() {
    *alive = false;
    *stopInk = true;
    for (auto* m: {&previews, &images}) {
        for (auto& [_, pb]: *m) {
            if (pb) {
                g_object_unref(pb);
            }
        }
    }
    gtk_widget_destroy(root);
    g_object_unref(root);
}

void LibraryView::buildSide() {
    GtkWidget* sideBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(sideBox, "amb-lib-side");
    // the labels inside expand; the side itself keeps its width
    gtk_widget_set_hexpand(sideBox, false);
    gtk_widget_set_size_request(sideBox, 248, -1);

    search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search), _("Search…"));
    addClass(search, "amb-lib-search");
    g_signal_connect(search, "search-changed", G_CALLBACK(+[](GtkSearchEntry* e, LibraryView* self) {
                         self->filter = gtk_entry_get_text(GTK_ENTRY(e));
                         self->fillGrid();
                     }),
                     this);
    gtk_container_add(GTK_CONTAINER(sideBox), search);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, true);
    side = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(side), GTK_SELECTION_BROWSE);
    g_signal_connect(side, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, LibraryView* self) {
                         if (!row) {
                             return;
                         }
                         auto kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "amb-kind"));
                         auto index = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(row), "amb-index"));
                         Source s;
                         s.kind = static_cast<SourceKind>(kind - 1);
                         s.index = index;
                         if (!(s == self->current)) {
                             self->showSource(s);
                         }
                     }),
                     this);
    gtk_container_add(GTK_CONTAINER(scroll), side);
    gtk_container_add(GTK_CONTAINER(sideBox), scroll);

    GtkWidget* add = iconButton("list-add-symbolic", _("Add folder"), "amb-lib-add");
    gtk_button_set_relief(GTK_BUTTON(add), GTK_RELIEF_NONE);
    onClicked(add, [this] { addFolderDialog(); });
    gtk_container_add(GTK_CONTAINER(sideBox), add);

    gtk_container_add(GTK_CONTAINER(root), sideBox);
}

void LibraryView::refresh() {
    courses = loadSummaCourses();
    courseBooks.clear();
    for (auto& c: courses) {
        courseBooks.push_back(scanNotebooks(c.path));
    }
    folderBooks.clear();
    for (auto& f: store.folders()) {
        folderBooks.push_back(scanNotebooks(f.path));
    }
    // a course or folder that disappeared falls back to Recent
    if ((current.kind == SourceKind::Course && current.index >= courses.size()) ||
        (current.kind == SourceKind::Folder && current.index >= store.folders().size())) {
        current = {SourceKind::Recent};
    }
    fillSide();
    fillGrid();
    reindex();
}

void LibraryView::focusSearch() { gtk_widget_grab_focus(search); }

auto LibraryView::colorOf(const fs::path& file) const -> std::string {
    if (file.empty()) {
        return {};
    }
    for (auto& c: courses) {
        if (!c.path.empty() && Util::isChildOrEquivalent(file, c.path)) {
            return c.color;
        }
    }
    return {};
}

auto LibraryView::allNotebooks() const -> std::vector<NotebookRef> {
    std::vector<NotebookRef> out;
    std::set<fs::path> seen;
    auto add = [&](const Entry& e) {
        if (seen.insert(e.nb.path).second) {
            out.push_back({e.nb.path, e.nb.name, e.place});
        }
    };
    for (auto& e: entriesFor({SourceKind::All})) {
        add(e);
    }
    for (auto& e: recentEntries()) {
        add(e);
    }
    return out;
}

auto LibraryView::recentNotebooks() const -> std::vector<NotebookRef> {
    std::vector<NotebookRef> out;
    for (auto& e: recentEntries()) {
        out.push_back({e.nb.path, e.nb.name, e.place});
    }
    return out;
}

void LibraryView::reindex() {
    if (indexing) {
        indexAgain = true;
        return;
    }
    indexing = true;
    indexAgain = false;
    std::vector<fs::path> files;
    auto all = entriesFor({SourceKind::All});
    for (auto& e: all) {
        files.push_back(e.nb.path);
    }
    for (auto& e: recentEntries()) {
        files.push_back(e.nb.path);
    }

    // a reason not to read (Summa closed, reading off, no model…) is retried after a while, not at every change
    constexpr auto INK_RETRY = std::chrono::minutes(2);
    const bool tryInk = inkStop == InkStop::None || inkStop == InkStop::Failed ||
                        std::chrono::steady_clock::now() - inkStoppedAt > INK_RETRY;

    struct Job {
        LibraryView* self;
        std::shared_ptr<bool> alive;
        std::shared_ptr<SearchIndex> result;
        bool last;                  ///< the indexing (and the reading) is over
        std::optional<InkRun> ink;  ///< when the handwriting was read
    };
    auto deliver = [self = this, alive = alive](std::shared_ptr<SearchIndex> result, bool last,
                                                std::optional<InkRun> ink) {
        g_idle_add(
                +[](gpointer d) -> gboolean {
                    std::unique_ptr<Job> job(static_cast<Job*>(d));
                    if (!*job->alive) {
                        return G_SOURCE_REMOVE;
                    }
                    auto* self = job->self;
                    self->index = std::move(job->result);
                    if (job->ink) {
                        self->inkStop = job->ink->stop;
                        self->inkStoppedAt = std::chrono::steady_clock::now();
                        // the time was up with pages left: another round
                        if (job->ink->read > 0 && job->ink->stop == InkStop::None) {
                            self->indexAgain = true;
                        }
                    }
                    // a search shows what is written; boards found for the first time get their wide card
                    bool boardsChanged = false;
                    for (auto& e: self->entriesFor(self->current)) {
                        if (self->isBoard(e.nb.path) != (self->shownBoards.count(e.nb.path) > 0)) {
                            boardsChanged = true;
                            break;
                        }
                    }
                    if (!self->filter.empty() || boardsChanged) {
                        self->fillGrid();
                    }
                    if (job->last) {
                        self->indexing = false;
                        if (self->indexAgain) {
                            self->reindex();
                        }
                    }
                    return G_SOURCE_REMOVE;
                },
                new Job{self, alive, std::move(result), last, std::move(ink)});
    };
    std::thread([deliver, tryInk, stop = stopInk, files = std::move(files), file = indexFile] {
        auto idx = std::make_shared<SearchIndex>(file);
        if (idx->update(files) > 0) {
            idx->save();
        }
        if (!tryInk || idx->inkProgress().first == 0) {
            deliver(std::move(idx), true, std::nullopt);
            return;
        }
        // the search gets the text at once; the handwriting is read on a copy, a page at a time
        deliver(std::make_shared<SearchIndex>(*idx), false, std::nullopt);
        constexpr auto INK_ROUND = std::chrono::seconds(90);
        InkRun run = readInk(*idx, files, std::chrono::steady_clock::now() + INK_ROUND, *stop);
        deliver(std::move(idx), true, run);
    }).detach();
}

auto LibraryView::inkStatus() const -> std::string {
    if (!index) {
        return {};
    }
    auto [pages, read] = index->inkProgress();
    if (pages == 0) {
        return {};
    }
    if (read >= pages) {
        return _("The handwriting of every page is searched too.");
    }
    switch (inkStop) {
        case InkStop::NoSumma:
            return _("The handwriting is searched while Summa is open.");
        case InkStop::Disabled:
            return _("To search the handwriting too, switch on the reading of notebooks in Summa.");
        case InkStop::NoModel:
        case InkStop::NoVision:
        case InkStop::Free:
            return _("The handwriting is not read: the model chosen in Summa does not read images, or is free.");
        case InkStop::Claude:
            return _("The handwriting is not read with the Claude engine of Summa yet.");
        case InkStop::Quota:
            return _("The reading of the handwriting stopped: the quota of the model is over for now.");
        default:
            break;
    }
    gchar* text = g_strdup_printf(_("Handwriting read on %zu of %zu pages."), read, pages);
    std::string s = text;
    g_free(text);
    return s;
}

void LibraryView::fillSide() {
    gtk_container_foreach(GTK_CONTAINER(side), [](GtkWidget* w, gpointer) { gtk_widget_destroy(w); }, nullptr);

    auto addRow = [this](Source s, const std::string& text, const char* icon, const std::string& color, size_t count) {
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        addClass(row, "amb-nav");
        if (icon) {
            gtk_container_add(GTK_CONTAINER(box), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU));
        } else {
            GtkWidget* sw = gtk_drawing_area_new();
            gtk_widget_set_size_request(sw, 10, 10);
            gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
            g_object_set_data_full(G_OBJECT(sw), "amb-color", g_strdup(color.c_str()), g_free);
            g_signal_connect(sw, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
                                 GdkRGBA c;
                                 gdk_rgba_parse(&c,
                                                static_cast<const char*>(g_object_get_data(G_OBJECT(w), "amb-color")));
                                 gdk_cairo_set_source_rgba(cr, &c);
                                 cairo_rectangle(cr, 0, 0, 10, 10);
                                 cairo_fill(cr);
                                 return false;
                             }),
                             nullptr);
            gtk_container_add(GTK_CONTAINER(box), sw);
        }
        GtkWidget* name = label(text);
        gtk_widget_set_hexpand(name, true);
        gtk_container_add(GTK_CONTAINER(box), name);
        if (count) {
            gtk_container_add(GTK_CONTAINER(box), label(std::to_string(count), "amb-count"));
        }
        gtk_container_add(GTK_CONTAINER(row), box);
        g_object_set_data(G_OBJECT(row), "amb-kind", GINT_TO_POINTER(static_cast<int>(s.kind) + 1));
        g_object_set_data(G_OBJECT(row), "amb-index", GSIZE_TO_POINTER(s.index));
        gtk_container_add(GTK_CONTAINER(side), row);
        if (s == current) {
            gtk_list_box_select_row(GTK_LIST_BOX(side), GTK_LIST_BOX_ROW(row));
        }
        return row;
    };
    auto addGroup = [this](const std::string& text, const char* from) {
        GtkWidget* row = gtk_list_box_row_new();
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), false);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), false);
        gtk_widget_set_can_focus(row, false);
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget* l = label(text);
        gtk_widget_set_hexpand(l, true);
        gtk_container_add(GTK_CONTAINER(box), l);
        if (from) {
            gtk_container_add(GTK_CONTAINER(box), label(from, "amb-from"));
        }
        gtk_container_add(GTK_CONTAINER(row), box);
        addClass(row, "amb-group");
        gtk_container_add(GTK_CONTAINER(side), row);
    };

    addRow({SourceKind::Recent}, _("Recent"), "document-open-recent-symbolic", "", 0);
    addRow({SourceKind::All}, _("All"), "view-grid-symbolic", "", entriesFor({SourceKind::All}).size());

    if (!courses.empty()) {
        addGroup(_("Courses"), "∫ Summa");
        for (size_t i = 0; i < courses.size(); i++) {
            auto color = courses[i].color.empty() ? std::string(DEFAULT_COVER_COLOR) : courses[i].color;
            addRow({SourceKind::Course, i}, courses[i].name, nullptr, color, courseBooks[i].size());
        }
    }
    if (!store.folders().empty()) {
        addGroup(_("Folders"), nullptr);
        for (size_t i = 0; i < store.folders().size(); i++) {
            GtkWidget* row = addRow({SourceKind::Folder, i}, store.folders()[i].name, "folder-symbolic", "",
                                    folderBooks[i].size());
            gtk_widget_set_tooltip_text(row, utf8(store.folders()[i].path).c_str());
            // right click: remove the folder from the library (the files stay)
            g_signal_connect(
                    row, "button-press-event",
                    G_CALLBACK(+[](GtkWidget* r, GdkEventButton* ev, LibraryView* self) -> gboolean {
                        if (ev->type != GDK_BUTTON_PRESS || ev->button != GDK_BUTTON_SECONDARY) {
                            return false;
                        }
                        GtkWidget* menu = gtk_menu_new();
                        GtkWidget* item = gtk_menu_item_new_with_label(_("Remove from library"));
                        g_object_set_data(G_OBJECT(item), "amb-index", g_object_get_data(G_OBJECT(r), "amb-index"));
                        g_signal_connect(item, "activate", G_CALLBACK(+[](GtkWidget* it, LibraryView* self) {
                                             auto i = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(it), "amb-index"));
                                             if (i < self->store.folders().size()) {
                                                 self->store.removeFolder(self->store.folders()[i].path);
                                                 self->store.save();
                                                 self->current = {SourceKind::Recent};
                                                 // the row being destroyed is ours: rebuild later
                                                 g_idle_add(
                                                         +[](gpointer d) -> gboolean {
                                                             static_cast<LibraryView*>(d)->refresh();
                                                             return G_SOURCE_REMOVE;
                                                         },
                                                         self);
                                             }
                                         }),
                                         self);
                        gtk_container_add(GTK_CONTAINER(menu), item);
                        gtk_widget_show_all(menu);
                        gtk_menu_attach_to_widget(GTK_MENU(menu), r, nullptr);
                        gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(ev));
                        return true;
                    }),
                    this);
        }
    }
    gtk_widget_show_all(side);
}

void LibraryView::showSource(Source s) {
    current = s;
    fillGrid();
}

auto LibraryView::sourceTitle(Source s) const -> std::string {
    switch (s.kind) {
        case SourceKind::Recent:
            return _("Recent");
        case SourceKind::All:
            return _("All notebooks");
        case SourceKind::Course:
            return courses[s.index].name;
        case SourceKind::Folder:
            return store.folders()[s.index].name;
    }
    return "";
}

auto LibraryView::sourceFolder(Source s) const -> fs::path {
    switch (s.kind) {
        case SourceKind::Course:
            return courses[s.index].path;
        case SourceKind::Folder:
            return store.folders()[s.index].path;
        default:
            return {};
    }
}

auto LibraryView::recentEntries() const -> std::vector<Entry> {
    std::vector<Entry> out;
    std::set<fs::path> seen;
    GList* items = gtk_recent_manager_get_items(gtk_recent_manager_get_default());
    std::vector<std::pair<time_t, fs::path>> found;
    for (GList* l = items; l; l = l->next) {
        auto* info = static_cast<GtkRecentInfo*>(l->data);
        if (gtk_recent_info_is_local(info)) {
            gchar* file = g_filename_from_uri(gtk_recent_info_get_uri(info), nullptr, nullptr);
            if (file) {
                fs::path p = Util::fromGFilename(file);
                g_free(file);
                std::error_code ec;
                if (p.extension() == ".xopp" && fs::is_regular_file(p, ec)) {
                    found.emplace_back(gtk_recent_info_get_modified(info), fs::canonical(p, ec));
                }
            }
        }
        gtk_recent_info_unref(info);
    }
    g_list_free(items);
    std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first > b.first; });
    // the course or folder a recent notebook belongs to gives its colour and place
    auto all = entriesFor({SourceKind::All});
    for (auto& [t, p]: found) {
        if (out.size() >= MAX_RECENT || !seen.insert(p).second) {
            continue;
        }
        auto it = std::find_if(all.begin(), all.end(), [&](const Entry& e) { return e.nb.path == p; });
        if (it != all.end()) {
            out.push_back(*it);
        } else {
            Notebook nb{p, utf8(p.stem()), "", 0};
            if (struct stat st{}; g_stat(Util::toGFilename(p).c_str(), &st) == 0) {
                nb.modified = st.st_mtime;
            }
            out.push_back({nb, "", utf8(p.parent_path().filename())});
        }
    }
    return out;
}

auto LibraryView::entriesFor(Source s) const -> std::vector<Entry> {
    std::vector<Entry> out;
    switch (s.kind) {
        case SourceKind::Recent:
            return recentEntries();
        case SourceKind::Course:
            for (auto& nb: courseBooks[s.index]) {
                out.push_back({nb, courses[s.index].color, courses[s.index].name});
            }
            return out;
        case SourceKind::Folder:
            for (auto& nb: folderBooks[s.index]) {
                out.push_back({nb, "", store.folders()[s.index].name});
            }
            return out;
        case SourceKind::All: {
            std::set<fs::path> seen;
            for (size_t i = 0; i < courses.size(); i++) {
                for (auto& nb: courseBooks[i]) {
                    if (seen.insert(nb.path).second) {
                        out.push_back({nb, courses[i].color, courses[i].name});
                    }
                }
            }
            for (size_t i = 0; i < folderBooks.size(); i++) {
                for (auto& nb: folderBooks[i]) {
                    if (seen.insert(nb.path).second) {
                        out.push_back({nb, "", store.folders()[i].name});
                    }
                }
            }
            std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.nb.modified > b.nb.modified; });
            return out;
        }
    }
    return out;
}

void LibraryView::fillGrid() {
    shownBoards.clear();
    gtk_container_foreach(GTK_CONTAINER(body), [](GtkWidget* w, gpointer) { gtk_widget_destroy(w); }, nullptr);
    gtk_label_set_text(GTK_LABEL(title), filter.empty() ? sourceTitle(current).c_str() : ("“" + filter + "”").c_str());
    gtk_label_set_text(GTK_LABEL(subtitle), "");

    // while searching, look everywhere
    auto entries = entriesFor(filter.empty() ? current : Source{SourceKind::All});
    if (!filter.empty()) {
        for (auto& e: recentEntries()) {
            if (std::none_of(entries.begin(), entries.end(), [&](auto& x) { return x.nb.path == e.nb.path; })) {
                entries.push_back(e);
            }
        }
    }
    const auto everywhere = entries;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](auto& e) { return !matches(e.nb.name, filter); }),
                  entries.end());

    if (!filter.empty()) {
        // notebooks whose name matches, then what is written inside
        if (!entries.empty()) {
            gtk_container_add(GTK_CONTAINER(body), label(_("Notebooks"), "amb-section"));
            std::vector<const Entry*> found;
            for (auto& e: entries) {
                found.push_back(&e);
            }
            addTiles(found);
        }
        addTextHits(everywhere);
        gtk_widget_show_all(body);
        return;
    }

    if (entries.empty()) {
        GtkWidget* empty =
                label(filter.empty() ? (current.kind == SourceKind::Recent ? _("The notebooks you open show up here.") :
                                                                             _("No notebooks here yet.")) :
                                       _("Nothing found."),
                      "amb-empty");
        gtk_container_add(GTK_CONTAINER(body), empty);
        gtk_widget_show_all(body);
        return;
    }

    // sections by subfolder inside a course or folder; one flat grid otherwise
    const bool sections = filter.empty() && (current.kind == SourceKind::Course || current.kind == SourceKind::Folder);
    std::vector<std::string> order;
    std::map<std::string, std::vector<const Entry*>> groups;
    for (auto& e: entries) {
        auto key = sections ? sectionOf(e.nb) : "";
        if (!groups.count(key)) {
            order.push_back(key);
        }
        groups[key].push_back(&e);
    }
    // loose notebooks first, then the subfolders by name
    std::stable_sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.empty() && !b.empty(); });
    if (sections) {
        std::sort(order.begin() + (order.front().empty() ? 1 : 0), order.end());
    }

    for (auto& key: order) {
        if (sections && !key.empty()) {
            gtk_container_add(GTK_CONTAINER(body), label(key, "amb-section"));
        }
        addTiles(groups[key]);
    }
    gtk_widget_show_all(body);
    gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroller)), 0);
}

void LibraryView::addTextHits(const std::vector<Entry>& everywhere) {
    gtk_container_add(GTK_CONTAINER(body), label(_("In the text"), "amb-section"));
    if (!index) {
        gtk_container_add(GTK_CONTAINER(body), label(_("Reading the notebooks…"), "amb-empty"));
        return;
    }
    std::vector<fs::path> order;
    for (auto& e: everywhere) {
        order.push_back(e.nb.path);
    }
    const auto hits = index->search(filter, order);
    const std::string ink = inkStatus();
    if (hits.empty()) {
        gtk_container_add(GTK_CONTAINER(body), label(_("Nothing found in the text or the formulas."), "amb-empty"));
        if (!ink.empty()) {
            gtk_container_add(GTK_CONTAINER(body), label(ink, "amb-ink-status"));
        }
        return;
    }
    std::set<fs::path> books;
    GtkWidget* list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(list, GTK_ALIGN_START);
    addClass(list, "amb-results");
    for (auto& h: hits) {
        books.insert(h.notebook);
        auto it = std::find_if(everywhere.begin(), everywhere.end(), [&](auto& e) { return e.nb.path == h.notebook; });
        if (it != everywhere.end()) {
            gtk_container_add(GTK_CONTAINER(list), makeHit(h, *it));
        }
    }
    gtk_container_add(GTK_CONTAINER(body), list);
    if (!ink.empty()) {
        gtk_container_add(GTK_CONTAINER(body), label(ink, "amb-ink-status"));
    }

    const std::string count = std::string(g_dngettext(nullptr, "%zu result", "%zu results", hits.size())) + " · " +
                              g_dngettext(nullptr, "%zu notebook", "%zu notebooks", books.size());
    gchar* text = g_strdup_printf(count.c_str(), hits.size(), books.size());
    gtk_label_set_text(GTK_LABEL(subtitle), text);
    g_free(text);
}

auto LibraryView::makeHit(const SearchHit& h, const Entry& e) -> GtkWidget* {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    addClass(button, "amb-hit");
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

    const Entry entry = e;
    GtkWidget* mini = coverArea(44, 58, [this, entry] {
        CoverInput in = coverInput(entry);
        in.showTitle = false;
        return in;
    });
    gtk_widget_set_valign(mini, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(row), mini);

    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(text), label(e.nb.name, "amb-hit-name"));
    gchar* where = g_strdup_printf(_("page %zu"), h.page + 1);
    std::string place = (e.place.empty() ? "" : e.place + " · ") + where + (h.latex ? " · LaTeX" : "") +
                        (h.ink ? std::string(" · ") + _("handwritten") : "");
    g_free(where);
    gtk_container_add(GTK_CONTAINER(text), label(place, "amb-hit-where"));

    // the match gets a soft band of the accent colour
    GtkWidget* top = gtk_widget_get_toplevel(root);
    const bool dark = GTK_IS_WINDOW(top) && gtk_style_context_has_class(gtk_widget_get_style_context(top), "darkMode");
    gchar* b = g_markup_escape_text(h.before.c_str(), -1);
    gchar* m = g_markup_escape_text(h.match.c_str(), -1);
    gchar* a = g_markup_escape_text(h.after.c_str(), -1);
    gchar* markup = g_strdup_printf("%s<span background=\"%s\" bgalpha=\"%s\">%s</span>%s", b,
                                    dark ? "#e6e6e6" : "#303030", dark ? "30%" : "18%", m, a);
    GtkWidget* ex = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(ex), markup);
    gtk_label_set_xalign(GTK_LABEL(ex), 0);
    gtk_label_set_line_wrap(GTK_LABEL(ex), true);
    gtk_label_set_line_wrap_mode(GTK_LABEL(ex), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(ex), 80);
    addClass(ex, h.latex ? "amb-hit-tex" : "amb-hit-ex");
    g_free(b), g_free(m), g_free(a), g_free(markup);
    gtk_container_add(GTK_CONTAINER(text), ex);
    gtk_container_add(GTK_CONTAINER(row), text);
    gtk_container_add(GTK_CONTAINER(button), row);

    const fs::path path = h.notebook;
    const size_t page = h.page;
    onClicked(button, [this, path, page] {
        if (actions.openAt) {
            actions.openAt(path, page);
        } else if (actions.open) {
            actions.open(path);
        }
    });
    return button;
}

auto LibraryView::coverInput(const Entry& e) -> CoverInput {
    CoverInput in;
    in.cover = store.cover(e.nb.path);
    in.title = e.nb.name;
    in.courseColor = e.courseColor;
    if (in.cover.style == CoverStyle::Page) {
        in.preview = previewOf(e.nb.path);
    }
    if (!in.cover.image.empty()) {
        in.image = imageOf(in.cover.image);
    }
    return in;
}

auto LibraryView::previewOf(const fs::path& p) -> GdkPixbuf* {
    if (auto it = previews.find(p); it != previews.end()) {
        return it->second;
    }
    GdkPixbuf* pb = nullptr;
    XojPreviewExtractor ex;
    if (ex.readFile(p) == PREVIEW_RESULT_IMAGE_READ) {
        gsize len = 0;
        unsigned char* data = ex.getData(len);
        GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
        if (gdk_pixbuf_loader_write(loader, data, len, nullptr) && gdk_pixbuf_loader_close(loader, nullptr)) {
            pb = gdk_pixbuf_loader_get_pixbuf(loader);
            if (pb) {
                g_object_ref(pb);
            }
        } else {
            gdk_pixbuf_loader_close(loader, nullptr);
        }
        g_object_unref(loader);
    }
    previews[p] = pb;
    return pb;
}

auto LibraryView::imageOf(const fs::path& p) -> GdkPixbuf* {
    if (auto it = images.find(p); it != images.end()) {
        return it->second;
    }
    // big photos are cut down once; a cover never needs more than twice its size
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(Util::toGFilename(p).c_str(), COVER_W * 2, COVER_H * 2 * 2, true,
                                                      nullptr);
    images[p] = pb;
    return pb;
}

auto LibraryView::isBoard(const fs::path& p) const -> bool { return index && index->isBoard(p); }

void LibraryView::addTiles(const std::vector<const Entry*>& entries) {
    std::vector<const Entry*> boards, notebooks;
    for (auto* e: entries) {
        (isBoard(e->nb.path) ? boards : notebooks).push_back(e);
    }
    for (auto* list: {&boards, &notebooks}) {
        if (list->empty()) {
            continue;
        }
        GtkWidget* grid = gtk_flow_box_new();
        gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(grid), GTK_SELECTION_NONE);
        gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid), true);
        gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid), 64);
        gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid), GRID_COLUMN_SPACING);
        gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid), 28);
        gtk_widget_set_valign(grid, GTK_ALIGN_START);
        addClass(grid, "amb-grid");
        for (auto* e: *list) {
            gtk_container_add(GTK_CONTAINER(grid), makeTile(*e));
        }
        gtk_container_add(GTK_CONTAINER(body), grid);
    }
}

auto LibraryView::makeTile(const Entry& e) -> GtkWidget* {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    addClass(button, "amb-nb");
    gtk_widget_set_tooltip_text(button, utf8(e.nb.path).c_str());
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    const fs::path path = e.nb.path;
    const std::string courseColor = e.courseColor;
    const std::string name = e.nb.name;
    const bool board = isBoard(path);
    const int width = board ? BOARD_W : COVER_W;
    GtkWidget* area = nullptr;
    if (board) {
        shownBoards.insert(path);
        area = gtk_drawing_area_new();
        gtk_widget_set_size_request(area, BOARD_W, COVER_H);
        g_object_set_data_full(G_OBJECT(area), "amb-path", new fs::path(path),
                               [](gpointer p) { delete static_cast<fs::path*>(p); });
        g_object_set_data_full(G_OBJECT(area), "amb-title", g_strdup(name.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(area), "amb-color", g_strdup(courseColor.c_str()), g_free);
        g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* a, cairo_t* cr, LibraryView* self) -> gboolean {
                             const auto& p = *static_cast<fs::path*>(g_object_get_data(G_OBJECT(a), "amb-path"));
                             CoverInput in;
                             in.cover = self->store.cover(p);
                             in.title = static_cast<const char*>(g_object_get_data(G_OBJECT(a), "amb-title"));
                             in.courseColor = static_cast<const char*>(g_object_get_data(G_OBJECT(a), "amb-color"));
                             in.preview = self->previewOf(p);
                             gchar* kicker = g_utf8_strup(_("Board"), -1);
                             drawBoardCover(cr, gtk_widget_get_allocated_width(a), gtk_widget_get_allocated_height(a),
                                            in, kicker);
                             g_free(kicker);
                             return false;
                         }),
                         this);
    } else {
        area = coverArea(COVER_W, COVER_H, [this, path, courseColor, name] {
            Entry tmp{Notebook{path, name, "", 0}, courseColor, ""};
            return coverInput(tmp);
        });
    }
    gtk_widget_set_halign(area, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(box), area);

    // the page cover shows no title, so the name goes under it
    GtkWidget* nameLabel = label(e.nb.name, "amb-nb-name");
    gtk_widget_set_size_request(nameLabel, COVER_W, -1);
    gtk_widget_set_no_show_all(nameLabel, board || store.cover(path).style != CoverStyle::Page);
    gtk_container_add(GTK_CONTAINER(box), nameLabel);
    std::string meta = relativeDate(e.nb.modified);
    if (current.kind == SourceKind::Recent || current.kind == SourceKind::All || !filter.empty()) {
        meta = e.place.empty() ? meta : e.place + " · " + meta;
    }
    GtkWidget* metaLabel = label(meta, "amb-nb-meta");
    gtk_widget_set_size_request(metaLabel, width, -1);
    gtk_container_add(GTK_CONTAINER(box), metaLabel);
    gtk_container_add(GTK_CONTAINER(button), box);

    onClicked(button, [this, path] {
        if (actions.open) {
            actions.open(path);
        }
    });

    auto* entry = new Entry(e);
    g_object_set_data_full(G_OBJECT(button), "amb-entry", entry, [](gpointer p) { delete static_cast<Entry*>(p); });
    g_object_set_data(G_OBJECT(button), "amb-self", this);
    g_signal_connect(button, "button-press-event",
                     G_CALLBACK(+[](GtkWidget* b, GdkEventButton* ev, gpointer) -> gboolean {
                         if (ev->type != GDK_BUTTON_PRESS || ev->button != GDK_BUTTON_SECONDARY) {
                             return false;
                         }
                         auto* self = static_cast<LibraryView*>(g_object_get_data(G_OBJECT(b), "amb-self"));
                         self->openCoverPopover(b, *static_cast<Entry*>(g_object_get_data(G_OBJECT(b), "amb-entry")));
                         return true;
                     }),
                     nullptr);
    g_signal_connect(button, "popup-menu", G_CALLBACK(+[](GtkWidget* b, gpointer) -> gboolean {
                         auto* self = static_cast<LibraryView*>(g_object_get_data(G_OBJECT(b), "amb-self"));
                         self->openCoverPopover(b, *static_cast<Entry*>(g_object_get_data(G_OBJECT(b), "amb-entry")));
                         return true;
                     }),
                     nullptr);
    return button;
}

void LibraryView::openCoverPopover(GtkWidget* tile, const Entry& e) {
    GtkWidget* pop = gtk_popover_new(tile);
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_RIGHT);
    addClass(pop, "amb-coverpop");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    const fs::path path = e.nb.path;
    const Entry entry = e;
    // every change goes to the store at once and redraws what shows the cover
    auto apply = [this, path, tile, pop](auto&& change) {
        Cover c = store.cover(path);
        change(c);
        store.setCover(path, c);
        store.save();
        gtk_widget_queue_draw(tile);
        gtk_widget_queue_draw(pop);
        // the name under the cover only shows on the page style
        GtkWidget* box = gtk_bin_get_child(GTK_BIN(tile));
        GList* kids = gtk_container_get_children(GTK_CONTAINER(box));
        if (GList* second = g_list_nth(kids, 1)) {
            gtk_widget_set_visible(GTK_WIDGET(second->data), c.style == CoverStyle::Page);
        }
        g_list_free(kids);
    };

    gtk_container_add(GTK_CONTAINER(box), label(_("Style"), "amb-pop-label"));
    GtkWidget* styles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_set_homogeneous(GTK_BOX(styles), true);
    const std::pair<CoverStyle, const char*> styleNames[] = {{CoverStyle::Contour, _("Contour")},
                                                             {CoverStyle::Linen, _("Linen")},
                                                             {CoverStyle::Page, _("Page")},
                                                             {CoverStyle::Plain, C_("cover style", "Plain")}};
    for (auto& [style, text]: styleNames) {
        GtkWidget* b = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        addClass(b, "amb-style");
        GtkWidget* v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        const CoverStyle st = style;
        GtkWidget* mini = coverArea(MINI_W, MINI_H, [this, entry, st] {
            CoverInput in = coverInput(entry);
            in.cover.style = st;
            in.image = nullptr;
            in.showTitle = false;
            if (st == CoverStyle::Page && !in.preview) {
                in.preview = previewOf(entry.nb.path);
            }
            return in;
        });
        gtk_widget_set_halign(mini, GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(v), mini);
        gtk_container_add(GTK_CONTAINER(v), gtk_label_new(text));
        gtk_container_add(GTK_CONTAINER(b), v);
        auto mark = [b, st, this, path] {
            auto* ctx = gtk_widget_get_style_context(b);
            if (store.cover(path).style == st && store.cover(path).image.empty()) {
                gtk_style_context_add_class(ctx, "on");
            } else {
                gtk_style_context_remove_class(ctx, "on");
            }
        };
        mark();
        g_object_set_data_full(G_OBJECT(b), "amb-mark", new std::function<void()>(mark),
                               [](gpointer p) { delete static_cast<std::function<void()>*>(p); });
        onClicked(b, [apply, st, styles] {
            apply([st](Cover& c) {
                c.style = st;
                c.image.clear();
            });
            gtk_container_foreach(
                    GTK_CONTAINER(styles),
                    [](GtkWidget* w, gpointer) {
                        (*static_cast<std::function<void()>*>(g_object_get_data(G_OBJECT(w), "amb-mark")))();
                    },
                    nullptr);
        });
        gtk_container_add(GTK_CONTAINER(styles), b);
    }
    gtk_container_add(GTK_CONTAINER(box), styles);

    gtk_container_add(GTK_CONTAINER(box), label(_("Colour"), "amb-pop-label"));
    // five per row; the last cell is "another colour"
    GtkWidget* swatches = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(swatches), 6);
    gtk_grid_set_row_spacing(GTK_GRID(swatches), 6);
    int cell = 0;
    auto place = [swatches, &cell](GtkWidget* w) {
        gtk_grid_attach(GTK_GRID(swatches), w, cell % 5, cell / 5, 1, 1);
        cell++;
    };
    std::vector<std::string> colors;
    colors.push_back("");  // the course's colour (or the default)
    for (auto* c: SWATCHES) {
        if (c != entry.courseColor) {
            colors.emplace_back(c);
        }
    }
    for (auto& color: colors) {
        GtkWidget* b = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        addClass(b, "amb-swatch");
        const std::string shown =
                color.empty() ? (entry.courseColor.empty() ? DEFAULT_COVER_COLOR : entry.courseColor) : color;
        if (color.empty()) {
            gtk_widget_set_tooltip_text(b, entry.courseColor.empty() ? _("Default colour") : _("Colour of the course"));
        }
        GtkWidget* dot = gtk_drawing_area_new();
        gtk_widget_set_size_request(dot, 24, 24);
        g_object_set_data_full(G_OBJECT(dot), "amb-color", g_strdup(shown.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(dot), "amb-value", g_strdup(color.c_str()), g_free);
        g_object_set_data(G_OBJECT(dot), "amb-self", this);
        g_object_set_data_full(G_OBJECT(dot), "amb-path", g_strdup(utf8(path).c_str()), g_free);
        g_signal_connect(
                dot, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
                    auto* self = static_cast<LibraryView*>(g_object_get_data(G_OBJECT(w), "amb-self"));
                    std::string p = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "amb-path"));
                    std::string value = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "amb-value"));
                    const bool on = self->store.cover(fs::path(std::u8string(p.begin(), p.end()))).color == value;
                    GdkRGBA c;
                    gdk_rgba_parse(&c, static_cast<const char*>(g_object_get_data(G_OBJECT(w), "amb-color")));
                    const double r = 12;
                    if (on) {
                        GdkRGBA fg;
                        gtk_style_context_get_color(gtk_widget_get_style_context(w), GTK_STATE_FLAG_NORMAL, &fg);
                        gdk_cairo_set_source_rgba(cr, &fg);
                        cairo_arc(cr, r, r, r, 0, 2 * G_PI);
                        cairo_fill(cr);
                        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                        cairo_arc(cr, r, r, r - 2, 0, 2 * G_PI);
                        cairo_fill(cr);
                        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                    }
                    gdk_cairo_set_source_rgba(cr, &c);
                    cairo_arc(cr, r, r, on ? r - 4 : r, 0, 2 * G_PI);
                    cairo_fill(cr);
                    return false;
                }),
                nullptr);
        gtk_container_add(GTK_CONTAINER(b), dot);
        onClicked(b, [apply, color] { apply([color](Cover& c) { c.color = color; }); });
        place(b);
    }
    // any other colour
    GtkWidget* custom = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(custom), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(custom, _("Other colour"));
    addClass(custom, "amb-swatch");
    GtkWidget* wheel = gtk_drawing_area_new();
    gtk_widget_set_size_request(wheel, 24, 24);
    g_signal_connect(wheel, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer) -> gboolean {
                         // a hue disc, like the prototype's conic gradient
                         const int n = 48;
                         for (int i = 0; i < n; i++) {
                             GdkRGBA c;
                             c.alpha = 1;
                             gtk_hsv_to_rgb(i / double(n), .62, .78, &c.red, &c.green, &c.blue);
                             gdk_cairo_set_source_rgba(cr, &c);
                             cairo_move_to(cr, 12, 12);
                             cairo_arc(cr, 12, 12, 12, i * 2 * G_PI / n - G_PI / 2,
                                       (i + 1.05) * 2 * G_PI / n - G_PI / 2);
                             cairo_close_path(cr);
                             cairo_fill(cr);
                         }
                         return false;
                     }),
                     nullptr);
    gtk_container_add(GTK_CONTAINER(custom), wheel);
    onClicked(custom, [this, apply, path, pop] {
        GtkWidget* dlg = gtk_color_chooser_dialog_new(_("Other colour"), GTK_WINDOW(gtk_widget_get_toplevel(root)));
        gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dlg), false);
        if (auto cur = store.cover(path).color; !cur.empty()) {
            GdkRGBA c;
            if (gdk_rgba_parse(&c, cur.c_str())) {
                gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dlg), &c);
            }
        }
        gtk_popover_popdown(GTK_POPOVER(pop));
        if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
            GdkRGBA c;
            gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dlg), &c);
            gchar* hex = g_strdup_printf("#%02x%02x%02x", static_cast<int>(c.red * 255 + .5),
                                         static_cast<int>(c.green * 255 + .5), static_cast<int>(c.blue * 255 + .5));
            std::string h = hex;
            g_free(hex);
            apply([h](Cover& cv) { cv.color = h; });
        }
        gtk_widget_destroy(dlg);
    });
    place(custom);
    gtk_container_add(GTK_CONTAINER(box), swatches);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* shuffle = iconButton("media-playlist-shuffle-symbolic", _("Another pattern"), "amb-ghost");
    gtk_button_set_relief(GTK_BUTTON(shuffle), GTK_RELIEF_NONE);
    onClicked(shuffle, [apply] {
        apply([](Cover& c) {
            c.style = CoverStyle::Contour;
            c.image.clear();
            c.seed = std::random_device{}() | 1u;
        });
    });
    gtk_container_add(GTK_CONTAINER(row), shuffle);
    GtkWidget* image = iconButton("image-x-generic-symbolic", _("Image…"), "amb-ghost");
    gtk_button_set_relief(GTK_BUTTON(image), GTK_RELIEF_NONE);
    onClicked(image, [this, apply, pop] {
        GtkFileChooserNative* dlg =
                gtk_file_chooser_native_new(_("Cover image"), GTK_WINDOW(gtk_widget_get_toplevel(root)),
                                            GTK_FILE_CHOOSER_ACTION_OPEN, nullptr, nullptr);
        GtkFileFilter* f = gtk_file_filter_new();
        gtk_file_filter_add_pixbuf_formats(f);
        gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dlg), f);
        gtk_popover_popdown(GTK_POPOVER(pop));
        if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
            gchar* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
            fs::path p = Util::fromGFilename(file);
            g_free(file);
            apply([p](Cover& c) { c.image = p; });
        }
        g_object_unref(dlg);
    });
    gtk_container_add(GTK_CONTAINER(row), image);
    gtk_container_add(GTK_CONTAINER(box), row);

    gtk_container_add(GTK_CONTAINER(pop), box);
    g_signal_connect(pop, "closed", G_CALLBACK(+[](GtkWidget* p, gpointer) {
                         g_idle_add(
                                 +[](gpointer w) -> gboolean {
                                     gtk_widget_destroy(GTK_WIDGET(w));
                                     return G_SOURCE_REMOVE;
                                 },
                                 p);
                     }),
                     nullptr);
    gtk_widget_show_all(box);
    gtk_popover_popup(GTK_POPOVER(pop));
}

void LibraryView::addFolderDialog() {
    GtkFileChooserNative* dlg =
            gtk_file_chooser_native_new(_("Add folder to the library"), GTK_WINDOW(gtk_widget_get_toplevel(root)),
                                        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, _("Add"), nullptr);
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        fs::path p = Util::fromGFilename(file);
        g_free(file);
        std::error_code ec;
        p = fs::weakly_canonical(p, ec);
        if (store.addFolder(p)) {
            store.save();
            current = {SourceKind::Folder, store.folders().size() - 1};
            refresh();
        }
    }
    g_object_unref(dlg);
}

}  // namespace ambitus::library
