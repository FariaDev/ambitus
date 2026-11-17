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
