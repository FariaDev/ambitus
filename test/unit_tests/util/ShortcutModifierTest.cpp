/*
 * Ambitus
 *
 * This file is part of the Ambitus UnitTests
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "util/ShortcutModifier.h"

namespace {
/// GTK takes the names of the modifiers from the keymap of the display, opened by gtk_init
bool hasDisplay() {
    static const bool opened = gtk_init_check(nullptr, nullptr);
    return opened;
}

constexpr guint MOD2_BIT = 0x10;
constexpr guint META_BIT = 0x10000000;
constexpr guint CONTROL_BIT = 0x4;
constexpr guint SHIFT_BIT = 0x1;
constexpr guint MOD1_BIT = 0x8;
}  // namespace

TEST(ShortcutModifier, isCmdOnMacOsAndCtrlElsewhere) {
#ifdef __APPLE__
    EXPECT_EQ(static_cast<guint>(shortcutModifier()), META_BIT);
#else
    EXPECT_EQ(static_cast<guint>(shortcutModifier()), CONTROL_BIT);
#endif
}

TEST(ShortcutModifier, shortcutAloneIsRecognized) { EXPECT_TRUE(isShortcutOnly(shortcutModifier())); }

TEST(ShortcutModifier, shortcutWithAnotherModifierIsNotRecognized) {
    EXPECT_FALSE(isShortcutOnly(static_cast<GdkModifierType>(shortcutModifier() | GDK_SHIFT_MASK)));
    EXPECT_FALSE(isShortcutOnly(static_cast<GdkModifierType>(shortcutModifier() | GDK_MOD1_MASK)));
}

TEST(ShortcutModifier, noModifierIsNotRecognized) {
    EXPECT_FALSE(isShortcutOnly(static_cast<GdkModifierType>(0)));
    EXPECT_FALSE(isShortcutOnly(GDK_SHIFT_MASK));
}

TEST(ShortcutModifier, mod2AlongWithCmdIsDroppedOnMacOsOnly) {
    const auto state = static_cast<GdkModifierType>(META_BIT | MOD2_BIT);
#ifdef __APPLE__
    EXPECT_EQ(static_cast<guint>(withoutMacMod2(state)), META_BIT);
    EXPECT_TRUE(isShortcutOnly(state));
#else
    // X11: MOD2 is NumLock and stays
    EXPECT_EQ(static_cast<guint>(withoutMacMod2(state)), META_BIT | MOD2_BIT);
    EXPECT_FALSE(isShortcutOnly(state));
#endif
}

TEST(ShortcutModifier, otherBitsAreKept) {
    const auto state = static_cast<GdkModifierType>(CONTROL_BIT | SHIFT_BIT | MOD1_BIT);
    EXPECT_EQ(static_cast<guint>(withoutMacMod2(state)), CONTROL_BIT | SHIFT_BIT | MOD1_BIT);
}

TEST(ShortcutModifier, labelOfThePrimaryShortcutIsTheSystemOne) {
    // GTK takes the names of the modifiers from the display's keymap: without a display the label is only the key
    if (!hasDisplay()) {
        GTEST_SKIP() << "no display: GTK labels need the keymap of one";
    }
#ifdef __APPLE__
    EXPECT_EQ(shortcutLabel("<Primary>c"), "⌘C");
    EXPECT_EQ(shortcutModifierLabel(), "⌘");
#else
    EXPECT_EQ(shortcutLabel("<Primary>c"), "Ctrl+C");
    EXPECT_EQ(shortcutModifierLabel(), "Ctrl");
#endif
}

TEST(ShortcutModifier, labelOfAShortcutWithAShiftIsKept) {
    if (!hasDisplay()) {
        GTEST_SKIP() << "no display: GTK labels need the keymap of one";
    }
    // the order of the modifiers in the label is GTK's; only the presence of the shift is checked
    const std::string withShift = shortcutLabel("<Primary><Shift>n");
    EXPECT_NE(withShift.find('N'), std::string::npos);
    EXPECT_NE(withShift, shortcutLabel("<Primary>n"));
    EXPECT_GT(withShift.size(), shortcutLabel("<Primary>n").size());
}
