#include <set>
#include <string>

#include <gtest/gtest.h>

#include "ambitus/Board.h"
#include "ambitus/NewNotebook.h"
#include "model/BackgroundConfig.h"

using namespace ambitus::newnotebook;

namespace {
auto taken(std::set<std::string> names) {
    return [names = std::move(names)](const std::string& n) { return names.count(n) > 0; };
}
}  // namespace

TEST(NewNotebook, theSummaPaperIsLinedWithGreyRulesAndAVioletMargin) {
    PageType t = paperType(Paper::SUMMA);
    EXPECT_EQ(t.format, PageTypeFormat::Lined);  // a type that Xournal++ already has
    BackgroundConfig cfg(t.config);
    uint32_t rule = 0;
    uint32_t margin = 0;
    ASSERT_TRUE(cfg.loadValueHex("f1", rule));
    ASSERT_TRUE(cfg.loadValueHex("f2", margin));
    EXPECT_EQ(rule, 0xc6c6c6u);
    EXPECT_EQ(margin, 0xb89ceau);
}

TEST(NewNotebook, theOtherPapersAreTheOnesXournalppHas) {
    EXPECT_EQ(paperType(Paper::BLUE_RULED).format, PageTypeFormat::Lined);
    EXPECT_TRUE(paperType(Paper::BLUE_RULED).config.empty());  // its own blue and pink
    EXPECT_EQ(paperType(Paper::GRID).format, PageTypeFormat::Graph);
    EXPECT_EQ(paperType(Paper::DOTTED).format, PageTypeFormat::Dotted);
    EXPECT_EQ(paperType(Paper::PLAIN).format, PageTypeFormat::Plain);
    EXPECT_NE(paperType(Paper::SUMMA), paperType(Paper::BLUE_RULED));
    EXPECT_EQ(allPapers().front(), DEFAULT_PAPER);  // the Summa paper is the default and comes first
}

TEST(NewNotebook, pageSizes) {
    auto a4 = pageDimensions(PageSize::A4);
    EXPECT_NEAR(a4.width, 595.28, 0.01);
    EXPECT_NEAR(a4.height, 841.89, 0.01);
    auto letter = pageDimensions(PageSize::LETTER);
    EXPECT_EQ(letter.width, 612.0);
    EXPECT_EQ(letter.height, 792.0);
    // infinite is the board that already exists
    auto inf = pageDimensions(PageSize::INFINITE);
    EXPECT_EQ(inf.width, ambitus::board::START_WIDTH);
    EXPECT_EQ(inf.height, ambitus::board::START_HEIGHT);
}

TEST(NewNotebook, cleaningTheName) {
    EXPECT_EQ(cleanName("  Limits  "), "Limits");
    EXPECT_EQ(cleanName("Limits.xopp"), "Limits");
    EXPECT_EQ(cleanName("Limits.XOPP "), "Limits");
    EXPECT_EQ(cleanName("Limits .xopp"), "Limits");
    EXPECT_EQ(cleanName(".xopp"), ".xopp");  // the name itself, not an extension
    EXPECT_EQ(cleanName("   "), "");
}

TEST(NewNotebook, aNameMustNotBeEmptyTakenOrAPath) {
    auto exists = taken({"Limits", "Derivatives"});
    EXPECT_EQ(checkName("Integrals", exists), NameCheck::OK);
    EXPECT_EQ(checkName("", exists), NameCheck::EMPTY);
    EXPECT_EQ(checkName("   ", exists), NameCheck::EMPTY);
    EXPECT_EQ(checkName(".xopp", exists), NameCheck::INVALID);  // a hidden file
    EXPECT_EQ(checkName("Limits", exists), NameCheck::EXISTS);
    EXPECT_EQ(checkName(" Limits.xopp ", exists), NameCheck::EXISTS);  // it is the file name that counts
    EXPECT_EQ(checkName("a/b", exists), NameCheck::INVALID);
    EXPECT_EQ(checkName("a\\b", exists), NameCheck::INVALID);
    EXPECT_EQ(checkName("..", exists), NameCheck::INVALID);
    EXPECT_EQ(checkName("a\nb", exists), NameCheck::INVALID);
}

TEST(NewNotebook, theSuggestionCountsOnFromTheNewestNotebook) {
    EXPECT_EQ(suggestName("Limits - class 3", "Untitled notebook", taken({})), "Limits - class 4");
    // the next one is taken: the one after
    EXPECT_EQ(suggestName("Limits - class 3", "Untitled notebook", taken({"Limits - class 4"})), "Limits - class 5");
    EXPECT_EQ(suggestName("Class 9", "Untitled notebook", taken({})), "Class 10");
}

TEST(NewNotebook, withoutANumberTheSuggestionIsTheFallbackMadeFree) {
    EXPECT_EQ(suggestName("", "Untitled notebook", taken({})), "Untitled notebook");
    EXPECT_EQ(suggestName("Limits", "Untitled notebook", taken({})), "Untitled notebook");
    EXPECT_EQ(suggestName("Limits", "Untitled notebook", taken({"Untitled notebook"})), "Untitled notebook 2");
    EXPECT_EQ(suggestName("", "Untitled notebook", taken({"Untitled notebook", "Untitled notebook 2"})),
              "Untitled notebook 3");
    // a name that is only a number has no prefix to count on
    EXPECT_EQ(suggestName("2024", "Untitled notebook", taken({})), "Untitled notebook");
}
