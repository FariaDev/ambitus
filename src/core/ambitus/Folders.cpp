#include "Folders.h"

#include <cstdlib>
#include <system_error>

#include <glib.h>

#include "util/PathUtil.h"

namespace ambitus::folders {

bool copyIfMissing(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::exists(to, ec) || !fs::is_directory(from, ec)) {
        return false;
    }
    fs::create_directories(to.parent_path(), ec);
    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec) {
        g_warning("Ambitus: could not copy %s to %s: %s", from.string().c_str(), to.string().c_str(),
                  ec.message().c_str());
        fs::remove_all(to, ec);
        return false;
    }
    g_message("Ambitus: copied %s to %s", from.string().c_str(), to.string().c_str());
    return true;
}

namespace {
fs::path fromG(const char* dir) { return Util::GFilename(dir).toPath().value_or(fs::path()); }

fs::path stateDir() {
    // like Util::getStateSubfolder: $XDG_STATE_HOME or ~/.local/state (Windows: the data folder)
#ifdef _WIN32
    return fromG(g_get_user_data_dir());
#else
    const char* xdgStateHome = std::getenv("XDG_STATE_HOME");
    if (xdgStateHome && xdgStateHome[0]) {
        return fromG(xdgStateHome);
    }
    return fromG(g_get_home_dir()) / ".local/state";
#endif
}
}  // namespace

void adoptXournalppFolders() {
    for (const auto& base:
         {fromG(g_get_user_config_dir()), fromG(g_get_user_data_dir()), stateDir(), fromG(g_get_user_cache_dir())}) {
        if (!base.empty()) {
            copyIfMissing(base / "xournalpp", base / "ambitus");
        }
    }
}

}  // namespace ambitus::folders
