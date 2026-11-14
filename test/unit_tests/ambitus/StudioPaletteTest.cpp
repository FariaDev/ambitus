/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "gui/StudioPalette.h"

using namespace ambitus;

TEST(StudioPalette, emptyOrBrokenToolsGoBackToTheDefault) {
    EXPECT_EQ(parsePaletteTools(""), defaultPaletteTools());
    EXPECT_EQ(parsePaletteTools(" , ,"), defaultPaletteTools());
    EXPECT_EQ(parsePaletteTools("nothing,at-all"), defaultPaletteTools());
}

TEST(StudioPalette, toolsKeepTheOrderTheyWerePinned) {
    std::vector<std::string> tools = {"hand", "pencil", "ellipse", "audio"};
    EXPECT_EQ(parsePaletteTools(serializePaletteTools(tools)), tools);
    EXPECT_EQ(serializePaletteTools(tools), "hand,pencil,ellipse,audio");
}

TEST(StudioPalette, unknownAndRepeatedToolsAreDropped) {
    EXPECT_EQ(parsePaletteTools("pencil,bogus, text ,pencil,latex"),
              (std::vector<std::string>{"pencil", "text", "latex"}));
}

TEST(StudioPalette, everyDefaultToolIsKnown) {
    auto d = defaultPaletteTools();
    EXPECT_EQ(parsePaletteTools(serializePaletteTools(d)), d);
    EXPECT_EQ(d.size(), 6u);
}

TEST(StudioPalette, emptyOrBrokenColorsGoBackToTheDefault) {
    EXPECT_EQ(parsePaletteColors(""), defaultPaletteColors());
    EXPECT_EQ(parsePaletteColors("red,12345,1234567,gggggg"), defaultPaletteColors());
    EXPECT_EQ(defaultPaletteColors().size(), 5u);
}

TEST(StudioPalette, colorsRoundTripAsLowercaseHex) {
    std::vector<uint32_t> colors = {0x161616, 0xa21caf, 0x000001};
    EXPECT_EQ(serializePaletteColors(colors), "161616,a21caf,000001");
    EXPECT_EQ(parsePaletteColors(serializePaletteColors(colors)), colors);
}

TEST(StudioPalette, colorsAcceptCapitalsAndAHash) {
    EXPECT_EQ(parsePaletteColors("#A21CAF, 0F766E"), (std::vector<uint32_t>{0xa21caf, 0x0f766e}));
}

TEST(StudioPalette, invalidAndRepeatedColorsAreDropped) {
    EXPECT_EQ(parsePaletteColors("161616,nope,161616,8044CA"), (std::vector<uint32_t>{0x161616, 0x8044ca}));
}

TEST(StudioPalette, noMoreThanTenColors) {
    std::string many;
    for (int i = 1; i <= 14; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%06x", i > 1 ? "," : "", i * 0x111111 & 0xffffff);
        many += buf;
    }
    auto colors = parsePaletteColors(many);
    EXPECT_EQ(colors.size(), PALETTE_MAX_COLORS);
    EXPECT_EQ(colors.front(), 0x111111u);
}

TEST(StudioPalette, theTipOfAColorWithoutNameIsItsHexInCapitals) {
    EXPECT_EQ(paletteColorHex(0xa21caf), "#A21CAF");
    EXPECT_EQ(paletteColorHex(0x000001), "#000001");
}
