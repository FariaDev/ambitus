/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "Versions.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include <glib.h>

#include "util/StringUtils.h"
#include "util/i18n.h"

namespace ambitus::versions {

namespace {
constexpr const char* NAME_FORMAT = "%Y%m%d-%H%M%S";  // UTC, sorts as text in time order
constexpr const char* EXTENSION = ".xopp";

std::string nameFor(Clock::time_point t) {
    GDateTime* dt = g_date_time_new_from_unix_utc(Clock::to_time_t(t));
    gchar* s = g_date_time_format(dt, NAME_FORMAT);
    std::string out = std::string(s) + EXTENSION;
    g_free(s);
    g_date_time_unref(dt);
    return out;
}

bool parseName(const std::string& name, Clock::time_point& out) {
    int y, mo, d, h, mi, s;
    char tail[8] = {};
    if (std::sscanf(name.c_str(), "%4d%2d%2d-%2d%2d%2d%7s", &y, &mo, &d, &h, &mi, &s, tail) != 7 ||
        std::string(tail) != EXTENSION) {
        return false;
    }
    GDateTime* dt = g_date_time_new_utc(y, mo, d, h, mi, s);
    if (!dt) {
        return false;
    }
    out = Clock::from_time_t(static_cast<time_t>(g_date_time_to_unix(dt)));
    g_date_time_unref(dt);
    return true;
}
}  // namespace

fs::path folderFor(const fs::path& root, const fs::path& notebook) {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(notebook, ec);
    if (ec) {
        abs = fs::absolute(notebook, ec);
    }
    const auto u8 = abs.u8string();
    const std::string_view text = char_cast(u8);
    gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_SHA1, text.data(), static_cast<gssize>(text.size()));
    std::string id(sum, 16);
    g_free(sum);
    return root / id;
}

std::vector<Version> list(const fs::path& root, const fs::path& notebook) {
    std::vector<Version> out;
    std::error_code ec;
    for (auto const& entry: fs::directory_iterator(folderFor(root, notebook), ec)) {
        Version v;
        if (!entry.is_regular_file(ec) || !parseName(entry.path().filename().string(), v.when)) {
            continue;
        }
        v.file = entry.path();
        v.size = entry.file_size(ec);
        out.push_back(std::move(v));
    }
    std::sort(out.begin(), out.end(), [](auto const& a, auto const& b) { return a.when > b.when; });
    return out;
}

bool keep(const fs::path& root, const fs::path& notebook, Clock::time_point now, std::chrono::seconds gap,
          size_t keepCount) {
    std::error_code ec;
    if (!fs::is_regular_file(notebook, ec)) {
        return false;
    }
    auto existing = list(root, notebook);
    if (gap.count() > 0 && !existing.empty() && now - existing.front().when < gap) {
        return false;
    }
    const fs::path folder = folderFor(root, notebook);
    fs::create_directories(folder, ec);
    // the save comes after this copy: never take the room it needs (a margin of 64 MiB beyond the copy)
    const auto size = fs::file_size(notebook, ec);
    const auto space = fs::space(folder, ec);
    if (ec || space.available < size + (std::uintmax_t{64} << 20)) {
        g_warning("Ambitus: no version of %s: not enough room", char_cast(notebook.u8string().c_str()));
        return false;
    }
    const fs::path tmp = folder / ".copiando";
    fs::copy_file(notebook, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        g_warning("Ambitus: no version of %s: %s", char_cast(notebook.u8string().c_str()), ec.message().c_str());
        std::error_code ignored;
        fs::remove(tmp, ignored);  // a half copy would only take room
        return false;
    }
    fs::rename(tmp, folder / nameFor(now), ec);
    if (ec) {
        return false;
    }
    // which notebook this folder is of, for whoever looks at it by hand
    std::ofstream(folder / "origem.txt", std::ios::trunc) << char_cast(notebook.u8string()) << "\n";
    existing = list(root, notebook);
    for (size_t i = keepCount; i < existing.size(); i++) {
        fs::remove(existing[i].file, ec);
    }
    return true;
}

fs::path defaultRoot() {
    // like Util::getDataSubfolder("versions"), without its error dialog: keep() makes the folder, or skips quietly
    return fs::path(g_get_user_data_dir()) / "xournalpp" / "versions";
}

void keepBeforeSave(const fs::path& notebook) {
    try {
        keep(defaultRoot(), notebook, Clock::now(), std::chrono::minutes(10), 30);
    } catch (const std::exception& e) {
        g_warning("Ambitus: no version kept: %s", e.what());
    }
}

std::vector<DayGroup> groupByDay(const std::vector<Version>& newestFirst, Clock::time_point now) {
    // a day as a number that grows with the calendar, in local time
    auto dayOf = [](Clock::time_point t) {
        GDateTime* dt = g_date_time_new_from_unix_local(Clock::to_time_t(t));
        const int day = g_date_time_get_year(dt) * 400 + g_date_time_get_day_of_year(dt);
        g_date_time_unref(dt);
        return day;
    };
    const int today = dayOf(now);
    GDateTime* n = g_date_time_new_from_unix_local(Clock::to_time_t(now));
    GDateTime* y = g_date_time_add_days(n, -1);
    const int yesterday = g_date_time_get_year(y) * 400 + g_date_time_get_day_of_year(y);
    g_date_time_unref(y);
    g_date_time_unref(n);

    std::vector<DayGroup> groups;
    int lastDay = -1;
    for (size_t i = 0; i < newestFirst.size(); i++) {
        const int day = dayOf(newestFirst[i].when);
        if (groups.empty() || day != lastDay) {
            DayGroup g;
            g.day = day == today     ? DayGroup::Day::Today :
                    day == yesterday ? DayGroup::Day::Yesterday :
                                       DayGroup::Day::Other;
            g.first = newestFirst[i].when;
            groups.push_back(std::move(g));
            lastDay = day;
        }
        groups.back().items.push_back(i);
    }
    return groups;
}

std::string describe(Clock::time_point when, Clock::time_point now) {
    GDateTime* w = g_date_time_new_from_unix_local(Clock::to_time_t(when));
    GDateTime* n = g_date_time_new_from_unix_local(Clock::to_time_t(now));
    auto day = [](GDateTime* t) { return g_date_time_get_year(t) * 400 + g_date_time_get_day_of_year(t); };
    gchar* hour = g_date_time_format(w, "%H:%M");
    std::string out;
    if (day(w) == day(n)) {
        out = FS(_F("today, {1}") % hour);
    } else {
        GDateTime* y = g_date_time_add_days(n, -1);
        if (day(w) == day(y)) {
            out = FS(_F("yesterday, {1}") % hour);
        } else {
            gchar* date =
                    g_date_time_format(w, g_date_time_get_year(w) == g_date_time_get_year(n) ? "%e %b" : "%e %b %Y");
            out = FS(_F("{1}, {2}") % g_strstrip(date) % hour);
            g_free(date);
        }
        g_date_time_unref(y);
    }
    g_free(hour);
    g_date_time_unref(w);
    g_date_time_unref(n);
    return out;
}

}  // namespace ambitus::versions
