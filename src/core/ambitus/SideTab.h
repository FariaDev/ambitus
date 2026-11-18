/*
 * Ambitus
 *
 * Bookkeeping of the notebook shown beside the one in the window (the side tab): it is an index into the tabs, and
 * the tabs move when others are opened or closed. Pure functions, so the rules can be tested without the window.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>

#include "Tabs.h"

namespace ambitus::sidetab {

/// The side tab after a tab is inserted at `at`: the tabs from there on move one place to the right
inline size_t afterInsert(size_t side, size_t at) {
    if (side == NO_TAB) {
        return NO_TAB;
    }
    return side >= at ? side + 1 : side;
}

/// The side tab after the tab at `erased` is removed: none when it was the side tab, the tabs after it move one place
/// to the left
inline size_t afterErase(size_t side, size_t erased) {
    if (side == NO_TAB || side == erased) {
        return NO_TAB;
    }
    return side > erased ? side - 1 : side;
}

/// The side tab after the tab `target` is activated. Activating the side tab swaps the two: the one that was in the
/// window (`previous`, NO_TAB when none) goes beside. Any other tab leaves the side tab where it is.
inline size_t afterSwap(size_t side, size_t target, size_t previous) {
    if (side == NO_TAB || side != target) {
        return side;
    }
    return previous;
}

}  // namespace ambitus::sidetab
