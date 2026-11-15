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
constexpr const char* ORIGIN_FILE = "origem.txt";
constexpr const char* SAVED_FILE = "salvo.txt";

/// SHA-1 of the content of a file, in hex; empty when it cannot be read
std::string fileSha1(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    GChecksum* sum = g_checksum_new(G_CHECKSUM_SHA1);
    char buf[64 * 1024];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        g_checksum_update(sum, reinterpret_cast<const guchar*>(buf), static_cast<gssize>(in.gcount()));
    }
    std::string out = g_checksum_get_string(sum);
    g_checksum_free(sum);
    return out;
}

/// The first line of a small text file, without the line end; empty when there is none
std::string firstLine(const fs::path& file) {
    std::ifstream in(file);
    std::string line;
    std::getline(in, line);
    return line;
}

bool hasCopies(const fs::path& folder) {
    std::error_code ec;
    Clock::time_point t;
    for (auto const& entry: fs::directory_iterator(folder, ec)) {
        if (parseName(entry.path().filename().string(), t)) {
            return true;
        }
    }
    return false;
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

bool followMove(const fs::path& root, const fs::path& from, const fs::path& to, bool keepSource) {
    const fs::path source = folderFor(root, from);
    const fs::path target = folderFor(root, to);
    std::error_code ec;
    if (source == target || !fs::is_directory(source, ec)) {
        return false;
    }
    fs::create_directories(target, ec);
    if (ec) {
        g_warning("Ambitus: the versions did not follow %s: %s", char_cast(to.u8string().c_str()),
                  ec.message().c_str());
        return false;
    }
    // the new name may already have copies (a file that was there before): both sets stay, the newer one on equal names
    std::vector<fs::path> entries;
    for (auto const& entry: fs::directory_iterator(source, ec)) {
        entries.push_back(entry.path());
    }
    if (ec) {
        g_warning("Ambitus: the versions did not follow %s: %s", char_cast(to.u8string().c_str()),
                  ec.message().c_str());
        return false;
    }
    for (auto const& entry: entries) {
        const fs::path dest = target / entry.filename();
        std::error_code existsError;
        if (fs::exists(dest, existsError)) {
            continue;
        }
        if (keepSource) {
            fs::copy_file(entry, dest, ec);
        } else {
            fs::rename(entry, dest, ec);
        }
        if (ec) {
            g_warning("Ambitus: a version did not follow %s: %s", char_cast(to.u8string().c_str()),
                      ec.message().c_str());
            return false;
        }
    }
    // the folder now belongs to the new path (a copied origem.txt would still name the old one)
    std::ofstream(target / ORIGIN_FILE, std::ios::trunc) << char_cast(to.u8string()) << "\n";
    if (keepSource) {
        return true;
    }
    fs::remove_all(source, ec);
    return !ec;
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
    std::ofstream(folder / ORIGIN_FILE, std::ios::trunc) << char_cast(notebook.u8string()) << "\n";
    existing = list(root, notebook);
    for (size_t i = keepCount; i < existing.size(); i++) {
        fs::remove(existing[i].file, ec);
    }
    return true;
}

void markSaved(const fs::path& root, const fs::path& notebook) {
    const fs::path folder = folderFor(root, notebook);
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return;  // no copies: nothing to find again
    }
    const std::string sum = fileSha1(notebook);
    if (!sum.empty()) {
        std::ofstream(folder / SAVED_FILE, std::ios::trunc) << sum << "\n";
    }
}

bool adoptOrphan(const fs::path& root, const fs::path& notebook) {
    std::error_code ec;
    const fs::path target = folderFor(root, notebook);
    if (hasCopies(target) || !fs::is_regular_file(notebook, ec)) {
        return false;
    }
    std::string sum;  // read only when there is a candidate
    for (auto const& entry: fs::directory_iterator(root, ec)) {
        const fs::path folder = entry.path();
        if (folder == target || !entry.is_directory(ec)) {
            continue;
        }
        const std::string saved = firstLine(folder / SAVED_FILE);
        const std::string origin = firstLine(folder / ORIGIN_FILE);
        std::error_code existsError;
        if (saved.empty() || origin.empty() || fs::exists(fs::u8path(origin), existsError)) {
            continue;  // no fingerprint, or its notebook is still there
        }
        if (sum.empty()) {
            sum = fileSha1(notebook);
            if (sum.empty()) {
                return false;
            }
        }
        if (saved != sum) {
            continue;
        }
        std::error_code moveError;
        fs::remove_all(target, moveError);  // an empty folder left by an earlier look
        fs::rename(folder, target, moveError);
        if (moveError) {
            g_warning("Ambitus: the versions of %s were found but not taken: %s",
                      char_cast(notebook.u8string().c_str()), moveError.message().c_str());
            return false;
        }
        std::ofstream(target / ORIGIN_FILE, std::ios::trunc) << char_cast(notebook.u8string()) << "\n";
        return true;
    }
    return false;
}

fs::path defaultRoot() {
    // like Util::getDataSubfolder("versions"), without its error dialog: keep() makes the folder, or skips quietly
    return fs::path(g_get_user_data_dir()) / "xournalpp" / "versions";
}

void keepBeforeSave(const fs::path& notebook) {
    try {
        // a notebook moved outside the program brings its copies back before the new one is made
        adoptOrphan(defaultRoot(), notebook);
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
