#include <gtest/gtest.h>

#include "ambitus/LruCache.h"

using ambitus::LruCache;

TEST(LruCache, aMissHasNoValue) {
    LruCache<int, int> cache(100);
    EXPECT_FALSE(cache.get(1).has_value());
    EXPECT_EQ(cache.size(), 0u);
}

TEST(LruCache, aPutValueIsFoundAndCosts) {
    LruCache<int, int> cache(100);
    cache.put(1, 10, 40);
    ASSERT_TRUE(cache.get(1).has_value());
    EXPECT_EQ(*cache.get(1), 10);
    EXPECT_EQ(cache.bytesUsed(), 40u);
}

TEST(LruCache, theLeastRecentlyUsedEntryGoesFirst) {
    LruCache<int, int> cache(100);
    cache.put(1, 1, 40);
    cache.put(2, 2, 40);
    cache.get(1);         // 1 is now the most recently used
    cache.put(3, 3, 40);  // 120 bytes: 2 goes
    EXPECT_TRUE(cache.get(1).has_value());
    EXPECT_FALSE(cache.get(2).has_value());
    EXPECT_TRUE(cache.get(3).has_value());
    EXPECT_EQ(cache.bytesUsed(), 80u);
    EXPECT_EQ(cache.size(), 2u);
}

TEST(LruCache, severalEntriesGoAtOnceUntilTheBudgetHolds) {
    LruCache<int, int> cache(100);
    for (int i = 0; i < 10; ++i) {
        cache.put(i, i, 10);
    }
    EXPECT_EQ(cache.size(), 10u);
    EXPECT_EQ(cache.bytesUsed(), 100u);
    cache.put(10, 10, 35);  // 135 bytes: 0, 1, 2 and 3 go, 95 bytes stay
    EXPECT_EQ(cache.bytesUsed(), 95u);
    EXPECT_EQ(cache.size(), 7u);
    EXPECT_FALSE(cache.get(3).has_value());
    EXPECT_TRUE(cache.get(4).has_value());
    EXPECT_TRUE(cache.get(10).has_value());
}

TEST(LruCache, aValueLargerThanTheBudgetIsNotKept) {
    LruCache<int, int> cache(100);
    cache.put(1, 1, 40);
    cache.put(2, 2, 101);
    EXPECT_FALSE(cache.get(2).has_value());
    EXPECT_TRUE(cache.get(1).has_value());
    EXPECT_EQ(cache.bytesUsed(), 40u);
}

TEST(LruCache, aKeyPutAgainReplacesItsValueAndCost) {
    LruCache<int, int> cache(100);
    cache.put(1, 1, 60);
    cache.put(1, 2, 30);
    ASSERT_TRUE(cache.get(1).has_value());
    EXPECT_EQ(*cache.get(1), 2);
    EXPECT_EQ(cache.bytesUsed(), 30u);
    EXPECT_EQ(cache.size(), 1u);
}

TEST(LruCache, eraseAndClearGiveBackTheBudget) {
    LruCache<int, int> cache(100);
    cache.put(1, 1, 40);
    cache.put(2, 2, 40);
    cache.erase(1);
    EXPECT_FALSE(cache.get(1).has_value());
    EXPECT_EQ(cache.bytesUsed(), 40u);
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.bytesUsed(), 0u);
    EXPECT_EQ(cache.budgetBytes(), 100u);
}
