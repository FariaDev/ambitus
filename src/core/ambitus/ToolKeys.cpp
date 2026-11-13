/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include "ToolKeys.h"

#include <gdk/gdk.h>

namespace ambitus {

namespace {
struct ToolKey {
    unsigned int key;
    ToolType type;
};
constexpr ToolKey TOOL_KEYS[] = {
        {GDK_KEY_p, TOOL_PEN},           {GDK_KEY_m, TOOL_HIGHLIGHTER}, {GDK_KEY_e, TOOL_ERASER},
        {GDK_KEY_s, TOOL_SELECT_REGION}, {GDK_KEY_t, TOOL_TEXT},        {GDK_KEY_x, TOOL_LATEX},
};
}  // namespace

std::optional<ToolType> toolForKey(unsigned int keyval) {
    const unsigned int key = gdk_keyval_to_lower(keyval);
    for (auto const& k: TOOL_KEYS) {
        if (k.key == key) {
            return k.type;
        }
    }
    return std::nullopt;
}

std::string withToolKey(const char* tip, ToolType type) {
    std::string out = tip ? tip : "";
    for (auto const& k: TOOL_KEYS) {
        if (k.type == type) {
            out += " (";
            out += static_cast<char>(gdk_keyval_to_upper(k.key));
            out += ")";
            break;
        }
    }
    return out;
}

}  // namespace ambitus
