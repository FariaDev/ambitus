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

TEST(StudioPalette, favoritesRoundTripInTheSavedFormat) {
    std::vector<PaletteFavorite> favs = {{"pencil", 0x8044ca, 2}, {"highlighter", 0xfde047, 3}};
    EXPECT_EQ(serializePaletteFavorites(favs), "pencil:8044ca:2;highlighter:fde047:3");
    EXPECT_EQ(parsePaletteFavorites("pencil:8044ca:2;highlighter:fde047:3"), favs);
    EXPECT_EQ(parsePaletteFavorites(serializePaletteFavorites(favs)), favs);
}

TEST(StudioPalette, noFavoritesIsNotTheDefaultOfAnything) {
    EXPECT_TRUE(parsePaletteFavorites("").empty());
    EXPECT_TRUE(parsePaletteFavorites(";;").empty());
    EXPECT_EQ(serializePaletteFavorites({}), "");
}

TEST(StudioPalette, invalidFavoritesAreDropped) {
    // unknown tool, bad colour, size out of range or not a number, wrong number of fields: only the last entry is good
    EXPECT_EQ(parsePaletteFavorites("eraser:8044ca:2;pencil:nothex:2;pencil:8044ca:5;pencil:8044ca:x;pencil:8044ca;"
                                    "pencil:8044ca:2:1;pencil:8044ca:-1;pencil:12345:1;highlighter:fde047:0"),
              (std::vector<PaletteFavorite>{{"highlighter", 0xfde047, 0}}));
}

TEST(StudioPalette, favoritesAcceptCapitalsAHashAndSpaces) {
    EXPECT_EQ(parsePaletteFavorites(" pencil : #8044CA : 4 ; "),
              (std::vector<PaletteFavorite>{{"pencil", 0x8044ca, 4}}));
}

TEST(StudioPalette, repeatedFavoritesAreDroppedButTheSameColourWithAnotherSizeIsNot) {
    EXPECT_EQ(parsePaletteFavorites("pencil:8044ca:2;pencil:8044ca:2;pencil:8044ca:3;highlighter:8044ca:2"),
              (std::vector<PaletteFavorite>{
                      {"pencil", 0x8044ca, 2}, {"pencil", 0x8044ca, 3}, {"highlighter", 0x8044ca, 2}}));
}

TEST(StudioPalette, noMoreThanFourFavorites) {
    std::string many;
    for (int i = 0; i < 7; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%spencil:%06x:1", i ? ";" : "", (i + 1) * 0x101010);
        many += buf;
    }
    auto favs = parsePaletteFavorites(many);
    EXPECT_EQ(favs.size(), PALETTE_MAX_FAVORITES);
    EXPECT_EQ(favs.front().color, 0x101010u);
    EXPECT_EQ(favs.back().color, 0x404040u);
}

TEST(StudioPalette, theIconOnALightColourIsDark) {
    EXPECT_TRUE(paletteColorIsLight(0xfde047));  // yellow marker
    EXPECT_TRUE(paletteColorIsLight(0xffffff));
    EXPECT_FALSE(paletteColorIsLight(0x8044ca));  // violet
    EXPECT_FALSE(paletteColorIsLight(0xc2410c));  // orange
    EXPECT_FALSE(paletteColorIsLight(0x161616));
}

TEST(StudioPalette, radialDefaultHasEightKnownPositions) {
    auto d = defaultRadialSlots();
    EXPECT_EQ(d.size(), RADIAL_SLOTS);
    for (auto const& id: d) {
        EXPECT_NE(findRadialOption(id), nullptr) << id;
    }
    EXPECT_EQ(serializeRadialSlots(d), "pencil,highlighter,eraser,lasso,undo,convert,send,hand");
    EXPECT_EQ(parseRadialSlots(serializeRadialSlots(d)), d);
}

TEST(StudioPalette, emptyOrWrongSizedRadialGoesBackToTheDefault) {
    EXPECT_EQ(parseRadialSlots(""), defaultRadialSlots());
    EXPECT_EQ(parseRadialSlots("pencil,eraser"), defaultRadialSlots());
    EXPECT_EQ(parseRadialSlots("pencil,pencil,pencil,pencil,pencil,pencil,pencil"), defaultRadialSlots());
    EXPECT_EQ(parseRadialSlots("pencil,pencil,pencil,pencil,pencil,pencil,pencil,pencil,pencil"), defaultRadialSlots());
}

TEST(StudioPalette, anUnknownRadialIdTakesTheDefaultOfItsPosition) {
    auto d = defaultRadialSlots();
    auto got = parseRadialSlots("pencil,bogus,eraser,lasso,undo,,send,hand");
    EXPECT_EQ(got[0], "pencil");
    EXPECT_EQ(got[1], d[1]);  // bogus
    EXPECT_EQ(got[5], d[5]);  // empty position
    EXPECT_EQ(got[6], "send");
}

TEST(StudioPalette, radialPositionsMayRepeatAnOption) {
    std::vector<std::string> slots = {"laser", "laser", "shapes", "redo", "copy", "image", "vspace", "latex"};
    EXPECT_EQ(parseRadialSlots(serializeRadialSlots(slots)), slots);
    EXPECT_EQ(parseRadialSlots(" laser , laser,shapes,redo,copy,image,vspace,latex "), slots);
}

TEST(StudioPalette, everyRadialOptionHasAnIdAnIconAndAName) {
    for (auto const& o: radialOptions()) {
        EXPECT_EQ(findRadialOption(o.id), &o) << o.id;
        EXPECT_NE(std::string(o.icon), "");
        EXPECT_NE(std::string(o.name), "");
        if (o.kind == RadialKind::ACTION) {
            EXPECT_NE(o.action, nullptr) << o.id;
        }
    }
    EXPECT_EQ(findRadialOption("nothing"), nullptr);
}
