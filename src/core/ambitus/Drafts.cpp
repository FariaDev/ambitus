/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "Drafts.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <system_error>
#include <vector>

#include <glib.h>

#include "util/i18n.h"

namespace ambitus::drafts {

namespace {
std::optional<fs::path> folderOverride;

fs::path normalized(const fs::path& path) {
    std::error_code ec;
    fs::path out = fs::weakly_canonical(path, ec);
    if (ec) {
        ec.clear();
        out = fs::absolute(path, ec);
    }
    return ec ? path.lexically_normal() : out.lexically_normal();
}

bool inside(const fs::path& file, const fs::path& dir) {
    const fs::path f = normalized(file);
    const fs::path d = normalized(dir);
    auto fi = f.begin();
    for (auto di = d.begin(); di != d.end(); ++di, ++fi) {
        if (fi == f.end() || *fi != *di) {
            return false;
        }
    }
    return fi != f.end();
}

bool isDigits(const std::string& s, size_t at, size_t count) {
    if (at + count > s.size()) {
        return false;
    }
    return std::all_of(s.begin() + static_cast<std::ptrdiff_t>(at), s.begin() + static_cast<std::ptrdiff_t>(at + count),
                       [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

bool isDate(const std::string& s) {
    return s.size() == 10 && isDigits(s, 0, 4) && s[4] == '-' && isDigits(s, 5, 2) && s[7] == '-' && isDigits(s, 8, 2);
}

bool isTime(const std::string& s) { return s.size() == 5 && isDigits(s, 0, 2) && s[2] == '-' && isDigits(s, 3, 2); }

/// The date and time tokens of a draft name ("Draft 2026-11-15 14-32 2"): the first date, and the time after it
bool stampOf(const std::string& stem, std::string& date, std::string& time) {
    size_t begin = 0;
    while (begin <= stem.size()) {
        size_t end = stem.find(' ', begin);
        if (end == std::string::npos) {
            end = stem.size();
        }
        const std::string token = stem.substr(begin, end - begin);
        if (isDate(token)) {
            date = token;
            const size_t next = end + 1;
            if (next >= stem.size()) {
                return false;
            }
            size_t nextEnd = stem.find(' ', next);
            if (nextEnd == std::string::npos) {
                nextEnd = stem.size();
            }
            const std::string t = stem.substr(next, nextEnd - next);
            if (!isTime(t)) {
                return false;
            }
            time = t;
            return true;
        }
        if (end == stem.size()) {
            return false;
        }
        begin = end + 1;
    }
    return false;
}

/// The ".xopp.bak" and autosave files Xournal++ leaves beside a notebook
std::vector<fs::path> sideFiles(const fs::path& file) {
    std::vector<fs::path> out;
    const std::string base = file.string();
    out.emplace_back(base + "~");
    out.emplace_back(base + ".bak");
    out.emplace_back(base + ".autosave.xopp");
    fs::path hidden = file;
    hidden.replace_filename(fs::path(".") += file.filename());
    hidden.replace_extension();
    hidden += ".autosave.xopp";
    out.push_back(std::move(hidden));
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool moveFile(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) {
        return true;
    }
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::none, ec);
    if (ec) {
        g_warning("Ambitus: could not keep the draft: %s", ec.message().c_str());
        return false;
    }
    fs::remove(from, ec);
    return true;
}
}  // namespace

void setFolderForTesting(const fs::path& folder) {
    if (folder.empty()) {
        folderOverride.reset();
    } else {
        folderOverride = folder;
    }
}

fs::path folder() {
    if (folderOverride) {
        return *folderOverride;
    }
    return fs::path(g_get_user_data_dir()) / "ambitus" / "drafts";
}

bool isDraft(const fs::path& file) { return !file.empty() && inside(file, folder()); }

std::string newName(std::tm now, const std::function<bool(const std::string&)>& exists) {
    char stamp[64] = {};
    if (std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H-%M", &now) == 0) {
        stamp[0] = '\0';
    }
    const std::string base = std::string(_("Draft")) + " " + stamp;
    auto taken = [&](const std::string& name) { return exists && exists(name); };
    if (!taken(base)) {
        return base;
    }
    for (int n = 2; n < 10000; n++) {
        const std::string candidate = base + " " + std::to_string(n);
        if (!taken(candidate)) {
            return candidate;
        }
    }
    return base;
}

std::string tabLabel(const std::string& fileName) {
    std::string date;
    std::string time;
    if (!stampOf(fs::path(fileName).stem().string(), date, time)) {
        return std::string(_("Draft"));
    }
    return std::string(_("Draft")) + " · " + time.substr(0, 2) + ":" + time.substr(3, 2);
}

std::string displayName(const std::string& stem) {
    const std::string draft = _("Draft");
    std::string date;
    std::string time;
    if (!stampOf(stem, date, time)) {
        return draft;
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (std::sscanf(date.c_str(), "%4d-%2d-%2d", &year, &month, &day) != 3 ||
        std::sscanf(time.c_str(), "%2d-%2d", &hour, &minute) != 2) {
        return draft;
    }
    GDateTime* when = g_date_time_new_local(year, month, day, hour, minute, 0);
    if (!when) {
        return draft;
    }
    gchar* text = g_date_time_format(when, "%e %b");
    g_date_time_unref(when);
    std::string dayMonth = text ? text : "";
    g_free(text);
    const size_t first = dayMonth.find_first_not_of(' ');
    if (first == std::string::npos) {
        dayMonth.clear();
    } else if (first > 0) {
        dayMonth.erase(0, first);
    }
    return draft + " · " + dayMonth + ", " + time.substr(0, 2) + ":" + time.substr(3, 2);
}

std::optional<fs::path> keep(const fs::path& draft, const fs::path& subjectFolder, const std::string& name) {
    if (draft.empty() || subjectFolder.empty() || name.empty()) {
        return std::nullopt;
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        return std::nullopt;
    }
    std::error_code ec;
    if (!fs::is_regular_file(draft, ec)) {
        return std::nullopt;
    }
    fs::create_directories(subjectFolder, ec);
    if (ec) {
        return std::nullopt;
    }
    const fs::path target = subjectFolder / (name + ".xopp");
    if (fs::exists(target, ec)) {
        return std::nullopt;
    }
    if (!moveFile(draft, target)) {
        return std::nullopt;
    }
    const std::vector<fs::path> from = sideFiles(draft);
    const std::vector<fs::path> to = sideFiles(target);
    for (size_t i = 0; i < from.size(); i++) {
        if (fs::is_regular_file(from[i], ec) && !fs::exists(to[i], ec)) {
            ec.clear();
            moveFile(from[i], to[i]);
        }
        ec.clear();
    }
    return target;
}

bool discard(const fs::path& draft) {
    if (!isDraft(draft)) {
        return false;
    }
    std::error_code ec;
    for (const auto& side: sideFiles(draft)) {
        ec.clear();
        fs::remove(side, ec);
    }
    ec.clear();
    fs::remove(draft, ec);
    return !ec;
}

}  // namespace ambitus::drafts
