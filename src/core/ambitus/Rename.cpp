/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "Rename.h"

#include <algorithm>
#include <system_error>

#include "ambitus/Drafts.h"
#include "ambitus/NewNotebook.h"
#include "ambitus/Versions.h"
#include "util/StringUtils.h"

namespace ambitus::rename {

namespace {
bool forbidden(char c) {
    if (c == '/' || c == '\\') {
        return true;
    }
#ifdef __APPLE__
    if (c == ':') {  // the Finder shows a ":" in a file name as "/"
        return true;
    }
#endif
    return static_cast<unsigned char>(c) < 0x20 || c == 0x7f;
}
}  // namespace

std::string nameOf(const fs::path& notebook) { return std::string(char_cast(notebook.stem().u8string())); }

NameCheck checkName(const std::string& typed, const std::string& current,
                    const std::function<bool(const std::string&)>& exists) {
    const std::string name = newnotebook::cleanName(typed);
    if (name.empty()) {
        return NameCheck::EMPTY;
    }
    if (name == current) {
        return NameCheck::SAME;
    }
    const bool hidden = name.front() == '.';
    if (hidden || std::any_of(name.begin(), name.end(), forbidden)) {
        return NameCheck::INVALID;
    }
    return exists && exists(name) ? NameCheck::EXISTS : NameCheck::OK;
}

bool isTaken(const fs::path& notebook, const std::string& name) {
    const fs::path target = notebook.parent_path() / (name + ".xopp");
    std::error_code ec;
    if (!fs::exists(target, ec)) {
        return false;
    }
    ec.clear();
    const bool itself = fs::equivalent(target, notebook, ec) && !ec;
    return !itself;
}

std::optional<fs::path> moveNotebook(const fs::path& from, const std::string& newName, const fs::path& versionsRoot) {
    std::error_code ec;
    if (from.empty() || !fs::is_regular_file(from, ec)) {
        return std::nullopt;
    }
    const auto exists = [&from](const std::string& name) { return isTaken(from, name); };
    if (checkName(newName, nameOf(from), exists) != NameCheck::OK) {
        return std::nullopt;
    }
    const fs::path to = from.parent_path() / (newnotebook::cleanName(newName) + ".xopp");
    if (!drafts::moveWithSideFiles(from, to)) {
        return std::nullopt;
    }
    versions::followMove(versionsRoot, from, to);
    return to;
}

}  // namespace ambitus::rename
