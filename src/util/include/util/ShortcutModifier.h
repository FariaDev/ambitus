/*
 * Ambitus
 *
 * The modifier of the keyboard shortcuts: Cmd on macOS, Ctrl elsewhere.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtk.h>

/// The modifier of the shortcuts, as GTK's <Primary> means it: GDK_META_MASK (Cmd) on macOS, where Ctrl is left to
/// the system, GDK_CONTROL_MASK elsewhere. Used wherever a shortcut is matched by hand; the menu accelerators say
/// <Primary> for the same key.
constexpr GdkModifierType shortcutModifier() {
#ifdef __APPLE__
    return GDK_META_MASK;
#else
    return GDK_CONTROL_MASK;
#endif
}

/// Drops MOD2 on macOS only. GTK keeps MOD2 in the default modifier mask, and macOS sets it along with Cmd, so a Cmd
/// key press would not match a state of Cmd alone. On X11 MOD2 is NumLock, which is left as it was.
constexpr GdkModifierType withoutMacMod2(GdkModifierType state) {
#ifdef __APPLE__
    return static_cast<GdkModifierType>(state & ~GDK_MOD2_MASK);
#else
    return state;
#endif
}

/// True when `state` holds the shortcut modifier and no other one.
constexpr bool isShortcutOnly(GdkModifierType state) { return withoutMacMod2(state) == shortcutModifier(); }

/// The shortcut as the system writes it, from a GTK accelerator such as "<Primary>c": "Ctrl+C" on Linux, "⌘C" on macOS
inline std::string shortcutLabel(const char* accelerator) {
    guint key = 0;
    GdkModifierType mods{};
    gtk_accelerator_parse(accelerator, &key, &mods);
    gchar* label = gtk_accelerator_get_label(key, mods);
    std::string text = label;
    g_free(label);
    return text;
}

/// The modifier alone, as the keys of the setup card write it: "Ctrl" on Linux, "⌘" on macOS (the label of "<Primary>k"
/// without its key)
inline std::string shortcutModifierLabel() {
    std::string text = shortcutLabel("<Primary>k");
    const std::string key = shortcutLabel("k");
    if (text.size() > key.size() && text.compare(text.size() - key.size(), key.size(), key) == 0) {
        text.erase(text.size() - key.size());
    }
    if (!text.empty() && text.back() == '+') {
        text.pop_back();
    }
    return text;
}
