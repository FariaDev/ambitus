/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "SettingsApply.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#include <glib.h>
#include <gtk/gtk.h>

#include "ambitus/SettingsRows.h"
#include "control/Control.h"
#include "control/actions/ActionDatabase.h"
#include "control/settings/Settings.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "library/SummaCourses.h"
#include "util/StringUtils.h"

namespace ambitus::settingsapply {

int languageIndex(const std::string& preferred) {
    std::string lang = preferred;
    if (lang.empty()) {
        const char* const* names = g_get_language_names();
        lang = names && names[0] ? names[0] : "";
    }
    if (lang.rfind("pt", 0) == 0) {
        return 0;
    }
    if (lang.rfind("en", 0) == 0) {
        return 1;
    }
    return -1;
}

int themeIndex(Control* ctrl) {
    switch (ctrl->getSettings()->getThemeVariant()) {
        case THEME_VARIANT_FORCE_LIGHT:
            return 1;
        case THEME_VARIANT_FORCE_DARK:
            return 2;
        default:
            return 0;
    }
}

std::string summaVault() {
    std::error_code ec;
    const fs::path config = library::summaConfigPath();
    if (!fs::exists(config, ec)) {
        return {};
    }
    std::ifstream in(config, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return settingsrows::vaultPathOf(out.str());
}

bool summaInstalled() {
    std::error_code ec;
    return fs::exists(library::summaConfigPath(), ec);
}

GtkWidget* makeAccentDots(const std::string& accent) {
    GtkWidget* dots = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    const bool chosenViolet = accent == "violet";
    for (bool violet: {false, true}) {
        GtkWidget* dot = gtk_drawing_area_new();
        gtk_widget_set_size_request(dot, 24, 24);
        g_object_set_data(G_OBJECT(dot), "amb-violet", GINT_TO_POINTER(violet ? 1 : 0));
        g_object_set_data(G_OBJECT(dot), "amb-chosen", GINT_TO_POINTER(violet == chosenViolet ? 1 : 0));
        g_signal_connect(dot, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
                             const bool v = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "amb-violet")) != 0;
                             const bool chosen = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "amb-chosen")) != 0;
                             const double cx = gtk_widget_get_allocated_width(w) / 2.0;
                             const double cy = gtk_widget_get_allocated_height(w) / 2.0;
                             // the graphite of the Summa is light on the dark theme
                             GtkWidget* top = gtk_widget_get_toplevel(w);
                             const bool dark =
                                     gtk_style_context_has_class(gtk_widget_get_style_context(top), "darkMode");
                             if (v) {
                                 cairo_set_source_rgb(cr, 0x80 / 255.0, 0x44 / 255.0, 0xca / 255.0);
                             } else if (dark) {
                                 cairo_set_source_rgb(cr, 0xe6 / 255.0, 0xe6 / 255.0, 0xe6 / 255.0);
                             } else {
                                 cairo_set_source_rgb(cr, 0x30 / 255.0, 0x30 / 255.0, 0x30 / 255.0);
                             }
                             cairo_arc(cr, cx, cy, 8, 0, 2 * G_PI);
                             cairo_fill(cr);
                             if (chosen) {  // the one in use, with the ring 2 px apart
                                 cairo_arc(cr, cx, cy, 11, 0, 2 * G_PI);
                                 cairo_set_line_width(cr, 2);
                                 cairo_stroke(cr);
                             }
                             return FALSE;
                         }),
                         nullptr);
        gtk_box_pack_start(GTK_BOX(dots), dot, false, false, 0);
    }
    return dots;
}

void applyTheme(Control* ctrl, int index) {
    ctrl->getSettings()->setThemeVariant(index == 1 ? THEME_VARIANT_FORCE_LIGHT :
                                         index == 2 ? THEME_VARIANT_FORCE_DARK :
                                                      THEME_VARIANT_USE_SYSTEM);
    if (auto* win = ctrl->getWindow()) {
        win->updateColorscheme();
    }
}

void applyLanguage(Control* ctrl, int index) {
    Settings* s = ctrl->getSettings();
    s->setPreferredLocale(index == 0 ? "pt_BR" : "en");
    s->customSettingsChanged();
    setenv("LANGUAGE", s->getPreferredLocale().c_str(), 1);
}

void inputChanged(Control* ctrl) {
    if (auto* win = ctrl->getWindow()) {
        win->setGtkTouchscreenScrollingForDeviceMapping();
        win->getXournal()->onSettingsChanged();
    }
    ctrl->initButtonTool();
}

void applyFingerMovesPage(Control* ctrl, bool moves) {
    ctrl->getSettings()->setTouchDrawingEnabled(!moves);
    ctrl->getActionDatabase()->setActionState(Action::TOGGLE_TOUCH_DRAWING, !moves);
    inputChanged(ctrl);
}

void applyPenRadial(Control* ctrl, bool radial) {
    ctrl->getSettings()->setAmbitusStylusRadial(radial);
    inputChanged(ctrl);
}

void applyNotebooksFolder(Control* ctrl, const fs::path& chosen) {
    Settings* s = ctrl->getSettings();
    const std::string before = s->getAmbitusNotebooksFolder();
    s->setAmbitusNotebooksFolder(std::string(char_cast(chosen.u8string())));
    if (auto* win = ctrl->getWindow()) {
        win->setLibraryFolder(chosen, fs::path(std::u8string(before.begin(), before.end())));
    }
}

}  // namespace ambitus::settingsapply
