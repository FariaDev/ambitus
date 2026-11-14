#include "LibraryText.h"

#include <glib.h>

#include "util/i18n.h"  // for _

namespace ambitus::library {

namespace {
auto formatted(GDateTime* t, const char* format) -> std::string {
    gchar* s = g_date_time_format(t, format);
    std::string out = s ? s : "";
    g_free(s);
    return out;
}

/// Calendar days from `then` to `now` (0 for the same day, 1 for yesterday)
auto daysBetween(GDateTime* then, GDateTime* now) -> int {
    int y1 = 0, m1 = 0, d1 = 0, y2 = 0, m2 = 0, d2 = 0;
    g_date_time_get_ymd(then, &y1, &m1, &d1);
    g_date_time_get_ymd(now, &y2, &m2, &d2);
    GDate* a = g_date_new_dmy(static_cast<GDateDay>(d1), static_cast<GDateMonth>(m1), static_cast<GDateYear>(y1));
    GDate* b = g_date_new_dmy(static_cast<GDateDay>(d2), static_cast<GDateMonth>(m2), static_cast<GDateYear>(y2));
    const int days = g_date_days_between(a, b);
    g_date_free(a);
    g_date_free(b);
    return days;
}
}  // namespace

auto whenText(std::time_t when, std::time_t now) -> std::string {
    GDateTime* then = g_date_time_new_from_unix_local(when);
    GDateTime* today = g_date_time_new_from_unix_local(now);
    const int days = daysBetween(then, today);
    const std::string clock = formatted(then, "%H:%M");
    std::string out;
    if (days == 0) {
        out = std::string(_("today")) + ", " + clock;
    } else if (days == 1) {
        out = std::string(_("yesterday")) + ", " + clock;
    } else {
        const bool sameYear = g_date_time_get_year(then) == g_date_time_get_year(today);
        std::string day = formatted(then, sameYear ? "%e %b" : "%e %b %Y");
        day.erase(0, day.find_first_not_of(' '));
        out = day + ", " + clock;
    }
    g_date_time_unref(then);
    g_date_time_unref(today);
    return out;
}

auto resumeLine(const std::string& place, size_t page, size_t total, std::time_t when, std::time_t now) -> std::string {
    std::string out = place;
    auto add = [&out](const std::string& part) { out += out.empty() ? part : " · " + part; };
    if (total > 0) {
        gchar* text = g_strdup_printf(_("page %zu of %zu"), page, total);
        add(text);
        g_free(text);
    }
    add(whenText(when, now));
    return out;
}

auto displayPath(const std::string& path, std::string home) -> std::string {
    while (home.size() > 1 && home.back() == '/') {
        home.pop_back();
    }
    if (home.size() < 2 || path.compare(0, home.size(), home) != 0) {  // no home, or the root: nothing to shorten
        return path;
    }
    const std::string rest = path.substr(home.size());
    // "/home/ana" is the home of "/home/ana/x", not of "/home/ana2/x"
    if (rest.empty()) {
        return "~";
    }
    return rest[0] == '/' ? "~" + rest : path;
}

}  // namespace ambitus::library
