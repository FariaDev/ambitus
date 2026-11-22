#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "control/SearchControl.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

TEST(SearchControlTest, ResetAfterNoResultsFindsAddedText) {
    auto page = std::make_shared<XojPage>(100, 100);
    SearchControl search(page, nullptr);
    size_t occurrences = 0;

    EXPECT_FALSE(search.search("needle", 1, &occurrences, nullptr));
    EXPECT_EQ(occurrences, 0);
    EXPECT_TRUE(search.search("", 1, nullptr, nullptr));
    EXPECT_TRUE(search.getResults().empty());

    auto text = std::make_unique<Text>();
    text->setText("needle");
    page->getSelectedLayer()->addElement(std::move(text));

    EXPECT_TRUE(search.search("needle", 1, &occurrences, nullptr));
    EXPECT_EQ(occurrences, 1);
}

TEST(SearchControlTest, ResetAfterResultsReflectsModifiedText) {
    auto page = std::make_shared<XojPage>(100, 100);
    auto text = std::make_unique<Text>();
    text->setText("needle");
    auto* addedText = text.get();
    page->getSelectedLayer()->addElement(std::move(text));
    SearchControl search(page, nullptr);
    size_t occurrences = 0;

    ASSERT_TRUE(search.search("needle", 1, &occurrences, nullptr));
    EXPECT_EQ(occurrences, 1);
    EXPECT_TRUE(search.search("", 1, nullptr, nullptr));
    EXPECT_TRUE(search.getResults().empty());

    addedText->setText("haystack");

    EXPECT_FALSE(search.search("needle", 1, &occurrences, nullptr));
    EXPECT_EQ(occurrences, 0);
}
