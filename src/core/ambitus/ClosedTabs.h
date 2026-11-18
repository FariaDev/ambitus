/*
 * Ambitus
 *
 * The notebooks closed during this session, to bring back with Ctrl+Shift+T.
 * Only the path and the page that was shown are kept: the notebook itself is
 * read from disk again. The newest closed comes out first.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <utility>

#include "filesystem.h"

namespace ambitus {

struct ClosedTab {
    fs::path path;
    int page = 0;
};

/// A bounded stack of closed files. Files without a path (unsaved new documents) are not remembered.
class ClosedTabs {
public:
    static constexpr size_t CAPACITY = 20;

    /// Remember a closed file; the oldest falls off past the capacity.
    void push(fs::path path, int page) {
        if (path.empty()) {
            return;
        }
        stack.push_front(ClosedTab{std::move(path), page});
        if (stack.size() > CAPACITY) {
            stack.pop_back();
        }
    }

    /// Take the newest file that is still on disk and not already open. Files that are gone or open are dropped.
    /// Returns nothing when no file is left to bring back.
    std::optional<ClosedTab> pop(const std::function<bool(const fs::path&)>& isOpen,
                                 const std::function<bool(const fs::path&)>& exists) {
        while (!stack.empty()) {
            ClosedTab candidate = std::move(stack.front());
            stack.pop_front();
            if (isOpen(candidate.path) || !exists(candidate.path)) {
                continue;
            }
            return candidate;
        }
        return std::nullopt;
    }

    bool empty() const { return stack.empty(); }
    size_t size() const { return stack.size(); }
    void clear() { stack.clear(); }

private:
    std::deque<ClosedTab> stack;
};

}  // namespace ambitus
