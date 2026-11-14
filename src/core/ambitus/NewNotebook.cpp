#include "NewNotebook.h"

#include <algorithm>
#include <cctype>

namespace ambitus::newnotebook {

PageType paperType(Paper paper) {
    switch (paper) {
        case Paper::SUMMA: {
            PageType t(PageTypeFormat::Lined);
            t.config = SUMMA_CONFIG;
            return t;
        }
        case Paper::BLUE_RULED:
            return PageType(PageTypeFormat::Lined);  // no config: the colours Xournal++ gives it
        case Paper::GRID:
            return PageType(PageTypeFormat::Graph);
        case Paper::DOTTED:
            return PageType(PageTypeFormat::Dotted);
        case Paper::PLAIN:
            return PageType(PageTypeFormat::Plain);
    }
    return PageType(PageTypeFormat::Lined);
}

Dimensions pageDimensions(PageSize size) {
    switch (size) {
        case PageSize::LETTER:
            return {612.0, 792.0};
        case PageSize::INFINITE:
            return {89 * 18.0, 56 * 18.0};  // Board.h: START_WIDTH, START_HEIGHT
        case PageSize::A4:
            break;
    }
    return {595.275591, 841.889764};
}

namespace {
bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
}  // namespace

std::string cleanName(const std::string& typed) {
    size_t b = 0;
    size_t e = typed.size();
    while (b < e && isSpace(typed[b])) {
        b++;
    }
    while (e > b && isSpace(typed[e - 1])) {
        e--;
    }
    std::string s = typed.substr(b, e - b);
    constexpr char EXT[] = ".xopp";
    constexpr size_t EXT_LEN = sizeof(EXT) - 1;
    if (s.size() > EXT_LEN) {
        std::string tail = s.substr(s.size() - EXT_LEN);
        std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return std::tolower(c); });
        if (tail == EXT) {
            s.erase(s.size() - EXT_LEN);
            while (!s.empty() && isSpace(s.back())) {
                s.pop_back();
            }
        }
    }
    return s;
}

NameCheck checkName(const std::string& typed, const std::function<bool(const std::string&)>& exists) {
    const std::string name = cleanName(typed);
    if (name.empty()) {
        return NameCheck::EMPTY;
    }
    const bool hidden = name.front() == '.';  // the library does not list hidden files
    const bool bad = std::any_of(name.begin(), name.end(), [](char c) {
        return c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f;
    });
    if (hidden || bad) {
        return NameCheck::INVALID;
    }
    return exists && exists(name) ? NameCheck::EXISTS : NameCheck::OK;
}

std::string suggestName(const std::string& newest, const std::string& fallback,
                        const std::function<bool(const std::string&)>& exists) {
    auto taken = [&](const std::string& n) { return exists && exists(n); };

    // "Limits - class 3" → "Limits - class 4"
    size_t digits = 0;
    while (digits < newest.size() && std::isdigit(static_cast<unsigned char>(newest[newest.size() - 1 - digits]))) {
        digits++;
    }
    constexpr size_t MAX_DIGITS = 6;
    if (digits > 0 && digits <= MAX_DIGITS && digits < newest.size()) {
        const std::string prefix = newest.substr(0, newest.size() - digits);
        long n = std::stol(newest.substr(newest.size() - digits));
        for (int tries = 0; tries < 10000; tries++) {
            std::string candidate = prefix + std::to_string(++n);
            if (!taken(candidate)) {
                return candidate;
            }
        }
    }

    if (!taken(fallback)) {
        return fallback;
    }
    for (int k = 2; k < 10000; k++) {
        std::string candidate = fallback + " " + std::to_string(k);
        if (!taken(candidate)) {
            return candidate;
        }
    }
    return fallback;
}

}  // namespace ambitus::newnotebook
