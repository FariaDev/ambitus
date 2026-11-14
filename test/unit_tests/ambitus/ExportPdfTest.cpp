#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ambitus/ExportPdf.h"

using namespace ambitus::exportpdf;

namespace {
std::vector<size_t> pagesOf(const Selection& s) {
    std::vector<size_t> out;
    for (size_t i = 0; i < s.chosen.size(); i++) {
        if (s.chosen[i]) {
            out.push_back(i);
        }
    }
    return out;
}
using Pgs = std::vector<size_t>;
}  // namespace

TEST(ExportPdf, allIsEveryPage) {
    Selection s = select(Pages::ALL, "ignored", 2, 6);
    EXPECT_TRUE(s.usable());
    EXPECT_EQ(s.count(), 6u);
}

TEST(ExportPdf, thisIsTheCurrentPageOnly) {
    EXPECT_EQ(pagesOf(select(Pages::THIS, "", 1, 6)), (Pgs{1}));
    // a page beyond the end (a stale index) falls on the last one, never out of range
    EXPECT_EQ(pagesOf(select(Pages::THIS, "", 9, 6)), (Pgs{5}));
}

TEST(ExportPdf, aRangeIsReadWithThePagesStartingAtOne) {
    Selection s = select(Pages::RANGE, "1-3, 5", 0, 6);
    EXPECT_TRUE(s.usable());
    EXPECT_EQ(pagesOf(s), (Pgs{0, 1, 2, 4}));
}

TEST(ExportPdf, openEndedRangesAreTheOnesOfTheExportDialogOfXournalpp) {
    EXPECT_EQ(pagesOf(select(Pages::RANGE, "5-", 0, 6)), (Pgs{4, 5}));
    EXPECT_EQ(pagesOf(select(Pages::RANGE, "-2", 0, 6)), (Pgs{0, 1}));
    EXPECT_EQ(pagesOf(select(Pages::RANGE, "2; 4", 0, 6)), (Pgs{1, 3}));  // ; and : separate too
}

TEST(ExportPdf, overlapsAndOrderDoNotRepeatOrReorderPages) {
    EXPECT_EQ(pagesOf(select(Pages::RANGE, "4, 1-3, 2-5", 0, 6)), (Pgs{0, 1, 2, 3, 4}));
}

TEST(ExportPdf, nothingTypedWaitsWithoutScolding) {
    for (const char* text: {"", "  ", " , "}) {
        Selection s = select(Pages::RANGE, text, 0, 6);
        EXPECT_EQ(s.problem, RangeProblem::EMPTY) << text;
        EXPECT_FALSE(s.usable());
    }
}

TEST(ExportPdf, aBadRangeIsRejectedAndChoosesNothing) {
    for (const char* text: {"abc", "0", "7", "3-1", "1-9", "1-3, x", "1..3"}) {
        Selection s = select(Pages::RANGE, text, 0, 6);
        EXPECT_EQ(s.problem, RangeProblem::INVALID) << text;
        EXPECT_FALSE(s.usable()) << text;
        EXPECT_EQ(s.count(), 0u) << text;
    }
}

TEST(ExportPdf, aNotebookWithoutPagesHasNothingToExport) { EXPECT_FALSE(select(Pages::ALL, "", 0, 0).usable()); }

TEST(ExportPdf, theExportGetsAscendingRangesWithNeighboursJoined) {
    PageRangeVector r = toRanges({true, true, true, false, true, false, false, true});
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r[0].first, 0u);
    EXPECT_EQ(r[0].last, 2u);
    EXPECT_EQ(r[1].first, 4u);
    EXPECT_EQ(r[1].last, 4u);
    EXPECT_EQ(r[2].first, 7u);
    EXPECT_EQ(r[2].last, 7u);
    EXPECT_TRUE(toRanges({false, false}).empty());
}

TEST(ExportPdf, withoutThePaperTheRulingGoesButNotThePdfUnderTheInk) {
    EXPECT_EQ(backgroundFor(true), EXPORT_BACKGROUND_ALL);
    EXPECT_EQ(backgroundFor(false), EXPORT_BACKGROUND_UNRULED);  // "No ruling": PDF and images stay
}

TEST(ExportPdf, theParametersAreAPdfExportOfTheChosenPages) {
    ExportParameters p = parametersFor(select(Pages::RANGE, "2-3", 0, 6), false);
    EXPECT_EQ(p.format, EXPORT_GRAPHICS_PDF);
    EXPECT_EQ(p.backgroundType, EXPORT_BACKGROUND_UNRULED);
    EXPECT_FALSE(p.progressiveMode);
    ASSERT_EQ(p.pageRanges.size(), 1u);
    EXPECT_EQ(p.pageRanges[0].first, 1u);
    EXPECT_EQ(p.pageRanges[0].last, 2u);
}

TEST(ExportPdf, theNameLosesItsPdfExtensionAndGetsItBack) {
    EXPECT_EQ(cleanName("  Limits - class 3.pdf "), "Limits - class 3");
    EXPECT_EQ(cleanName("Limits.PDF"), "Limits");
    EXPECT_EQ(cleanName("notes.pdf.pdf"), "notes.pdf");  // only one
    EXPECT_EQ(fileNameOf("Limits"), "Limits.pdf");
    EXPECT_EQ(fileNameOf("Limits.pdf"), "Limits.pdf");
}

TEST(ExportPdf, aNameIsCheckedAsAFileName) {
    EXPECT_EQ(checkName("Limits - class 3"), NameCheck::OK);
    EXPECT_EQ(checkName("Cálculo — aula 3"), NameCheck::OK);
    EXPECT_EQ(checkName("   "), NameCheck::EMPTY);
    EXPECT_EQ(checkName(".pdf"), NameCheck::EMPTY);
    EXPECT_EQ(checkName("a/b"), NameCheck::INVALID);
    EXPECT_EQ(checkName("a\\b"), NameCheck::INVALID);
    EXPECT_EQ(checkName(".hidden"), NameCheck::INVALID);
}
