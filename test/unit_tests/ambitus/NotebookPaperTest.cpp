/*
 * Ambitus
 *
 * Unit tests for the pure rule that picks the default paper of a notebook.
 *
 * @license GNU GPLv2 or later
 */

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ambitus/NewNotebook.h"
#include "ambitus/NotebookPaper.h"
#include "model/PageType.h"

using namespace ambitus;

namespace {
PageType pdf() { return PageType(PageTypeFormat::Pdf); }
PageType image() { return PageType(PageTypeFormat::Image); }
PageType unknown() { return PageType(PageTypeFormat::Staves); }
}  // namespace

TEST(NotebookPaper, recognizesTheFivePapers) {
    for (auto p: newnotebook::allPapers()) {
        EXPECT_EQ(notebookpaper::paperFromBackground(newnotebook::paperType(p)), p);
    }
}

TEST(NotebookPaper, skipsPdfImageAndUnknown) {
    EXPECT_EQ(notebookpaper::paperFromBackground(pdf()), std::nullopt);
    EXPECT_EQ(notebookpaper::paperFromBackground(image()), std::nullopt);
    EXPECT_EQ(notebookpaper::paperFromBackground(unknown()), std::nullopt);
    PageType otherRuled(PageTypeFormat::Lined);
    otherRuled.config = "f1=123456";
    EXPECT_EQ(notebookpaper::paperFromBackground(otherRuled), std::nullopt);
}

TEST(NotebookPaper, storedChoiceWins) {
    std::vector<PageType> pages{newnotebook::paperType(newnotebook::Paper::GRID)};
    EXPECT_EQ(notebookpaper::defaultPaper("dotted", pages, "plain"), newnotebook::Paper::DOTTED);
    // an unknown stored id is ignored
    EXPECT_EQ(notebookpaper::defaultPaper("lixo", pages, "plain"), newnotebook::Paper::GRID);
}

TEST(NotebookPaper, firstPageThatIsNotPdfOrImageWins) {
    std::vector<PageType> pages{pdf(), image(), newnotebook::paperType(newnotebook::Paper::BLUE_RULED),
                                newnotebook::paperType(newnotebook::Paper::GRID)};
    EXPECT_EQ(notebookpaper::defaultPaper(std::nullopt, pages, "plain"), newnotebook::Paper::BLUE_RULED);
}

TEST(NotebookPaper, aNewPageRepeatsAnOrdinaryPaperWhenNothingIsStored) {
    const std::vector<PageType> pages{newnotebook::paperType(newnotebook::Paper::GRID), unknown()};
    EXPECT_EQ(notebookpaper::paperForNewPage(std::nullopt, unknown(), pages, "plain"), std::nullopt);
    EXPECT_EQ(notebookpaper::paperForNewPage(std::nullopt, pages[0], pages, "plain"), std::nullopt);
    EXPECT_EQ(notebookpaper::paperForNewPage("lixo", pages[0], pages, "plain"), std::nullopt);
}

TEST(NotebookPaper, aStoredChoiceDecidesTheNewPageAfterAnyPage) {
    const std::vector<PageType> pages{pdf(), unknown()};
    EXPECT_EQ(notebookpaper::paperForNewPage("dotted", unknown(), pages, "plain"), newnotebook::Paper::DOTTED);
    EXPECT_EQ(notebookpaper::paperForNewPage("dotted", pdf(), pages, "plain"), newnotebook::Paper::DOTTED);
}

TEST(NotebookPaper, aPdfOrImagePageIsNeverRepeatedByItself) {
    const std::vector<PageType> pages{pdf(), newnotebook::paperType(newnotebook::Paper::GRID)};
    EXPECT_EQ(notebookpaper::paperForNewPage(std::nullopt, pdf(), pages, "plain"), newnotebook::Paper::GRID);
    EXPECT_EQ(notebookpaper::paperForNewPage(std::nullopt, image(), {pdf()}, "plain"), newnotebook::Paper::PLAIN);
    EXPECT_EQ(notebookpaper::paperForNewPage(std::nullopt, pdf(), {pdf()}, "lixo"), newnotebook::DEFAULT_PAPER);
}

TEST(NotebookPaper, fallsBackToTheSettingThenToSumma) {
    std::vector<PageType> pages{pdf(), image(), unknown()};
    EXPECT_EQ(notebookpaper::defaultPaper(std::nullopt, pages, "grid"), newnotebook::Paper::GRID);
    EXPECT_EQ(notebookpaper::defaultPaper(std::nullopt, pages, "lixo"), newnotebook::DEFAULT_PAPER);
    EXPECT_EQ(notebookpaper::defaultPaper(std::nullopt, {}, ""), newnotebook::Paper::SUMMA);
}
