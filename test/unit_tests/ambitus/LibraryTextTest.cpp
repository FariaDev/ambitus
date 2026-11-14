#include <ctime>
#include <string>

#include <glib.h>
#include <gtest/gtest.h>

#include "library/LibraryText.h"
#include "util/i18n.h"  // for _

using namespace ambitus::library;

namespace {
/// A local time, so that the tests say the same in any time zone
auto at(int year, int month, int day, int hour, int minute) -> std::time_t {
    GDateTime* t = g_date_time_new_local(year, month, day, hour, minute, 0);
    const auto unix = static_cast<std::time_t>(g_date_time_to_unix(t));
    g_date_time_unref(t);
    return unix;
}

auto words(const char* text) -> std::string { return text; }

auto pageOf(size_t page, size_t total) -> std::string {
    gchar* s = g_strdup_printf(_("page %zu of %zu"), page, total);
    std::string out = s;
    g_free(s);
    return out;
}
}  // namespace

TEST(LibraryText, theCardSaysWhereAndWhenYouStopped) {
    const auto now = at(2026, 11, 14, 11, 30);
    const auto last = at(2026, 11, 13, 22, 10);  // yesterday evening
    EXPECT_EQ(resumeLine("Calculus I", 4, 6, last, now),
              "Calculus I · " + pageOf(4, 6) + " · " + words(_("yesterday")) + ", 22:10");
}

TEST(LibraryText, theCardLeavesOutWhatItDoesNotKnow) {
    const auto now = at(2026, 11, 14, 11, 30);
    const auto last = at(2026, 11, 14, 9, 5);
    const std::string today = words(_("today")) + ", 09:05";
    // the pages are not counted yet
    EXPECT_EQ(resumeLine("Calculus I", 4, 0, last, now), "Calculus I · " + today);
    // a notebook outside of any folder with a name
    EXPECT_EQ(resumeLine("", 1, 3, last, now), pageOf(1, 3) + " · " + today);
    EXPECT_EQ(resumeLine("", 1, 0, last, now), today);
}

TEST(LibraryText, daysAreCalendarDaysNotTwentyFourHours) {
    // 23:50 last night is yesterday at 00:10, 20 minutes later
    const auto now = at(2026, 11, 14, 0, 10);
    EXPECT_EQ(whenText(at(2026, 11, 13, 23, 50), now), words(_("yesterday")) + ", 23:50");
    EXPECT_EQ(whenText(at(2026, 11, 14, 0, 1), now), words(_("today")) + ", 00:01");
    // across the end of a year
    EXPECT_EQ(whenText(at(2025, 12, 31, 21, 0), at(2026, 1, 1, 8, 0)), words(_("yesterday")) + ", 21:00");
}

TEST(LibraryText, olderDaysShowTheDateAndTheYearOnlyWhenItDiffers) {
    const auto now = at(2026, 11, 14, 11, 30);
    const std::string sameYear = whenText(at(2026, 11, 2, 18, 40), now);
    EXPECT_EQ(sameYear.find("2026"), std::string::npos);
    EXPECT_EQ(sameYear.rfind(", 18:40"), sameYear.size() - 7);
    EXPECT_NE(sameYear.find('2'), std::string::npos);  // the day

    const std::string otherYear = whenText(at(2025, 3, 9, 7, 5), now);
    EXPECT_NE(otherYear.find("2025"), std::string::npos);
    EXPECT_EQ(otherYear.rfind(", 07:05"), otherYear.size() - 7);
    EXPECT_NE(otherYear.front(), ' ');  // the padding of a one digit day is gone
}

TEST(LibraryText, theHomeFolderIsATilde) {
    EXPECT_EQ(displayPath("/home/ana/Estudos/Limites.xopp", "/home/ana"), "~/Estudos/Limites.xopp");
    EXPECT_EQ(displayPath("/home/ana/Estudos/Limites.xopp", "/home/ana/"), "~/Estudos/Limites.xopp");
    EXPECT_EQ(displayPath("/home/ana", "/home/ana"), "~");
    // another folder that only starts the same way
    EXPECT_EQ(displayPath("/home/ana2/x.xopp", "/home/ana"), "/home/ana2/x.xopp");
    EXPECT_EQ(displayPath("/mnt/disk/x.xopp", "/home/ana"), "/mnt/disk/x.xopp");
    // no home, or home is the root: nothing to shorten
    EXPECT_EQ(displayPath("/mnt/disk/x.xopp", ""), "/mnt/disk/x.xopp");
    EXPECT_EQ(displayPath("/mnt/disk/x.xopp", "/"), "/mnt/disk/x.xopp");
}
