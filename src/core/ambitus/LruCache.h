/*
 * Ambitus
 *
 * A least-recently-used cache with a budget in bytes. Pure C++ (no GTK, no
 * cairo), so it can be unit tested on its own.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace ambitus {

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LruCache {
public:
    explicit LruCache(std::size_t budgetBytes): budget(budgetBytes) {}

    /// The value stored for key, which becomes the most recently used one; nullopt if there is none
    auto get(const Key& key) -> std::optional<Value> {
        auto it = index.find(key);
        if (it == index.end()) {
            return std::nullopt;
        }
        entries.splice(entries.begin(), entries, it->second);
        return it->second->value;
    }

    /// Stores value, which costs `bytes`, and evicts the least recently used entries until the budget holds again.
    /// A value that alone exceeds the budget is not stored.
    void put(const Key& key, Value value, std::size_t bytes) {
        erase(key);
        if (bytes > budget) {
            return;
        }
        entries.push_front(Entry{key, std::move(value), bytes});
        index.emplace(key, entries.begin());
        used += bytes;
        while (used > budget) {
            used -= entries.back().bytes;
            index.erase(entries.back().key);
            entries.pop_back();
        }
    }

    void erase(const Key& key) {
        auto it = index.find(key);
        if (it == index.end()) {
            return;
        }
        used -= it->second->bytes;
        entries.erase(it->second);
        index.erase(it);
    }

    void clear() {
        entries.clear();
        index.clear();
        used = 0;
    }

    auto size() const -> std::size_t { return entries.size(); }
    auto bytesUsed() const -> std::size_t { return used; }
    auto budgetBytes() const -> std::size_t { return budget; }

private:
    struct Entry {
        Key key;
        Value value;
        std::size_t bytes;
    };

    std::size_t budget;
    std::size_t used = 0;
    std::list<Entry> entries;  // most recently used first
    std::unordered_map<Key, typename std::list<Entry>::iterator, Hash> index;
};

}  // namespace ambitus
