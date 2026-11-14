#include "LatexError.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace ambitus::latex {

namespace {
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        a++;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        b--;
    }
    return s.substr(a, b - a);
}

std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> lines;
    size_t from = 0;
    while (from <= text.size()) {
        size_t to = text.find('\n', from);
        if (to == std::string::npos) {
            to = text.size();
        }
        lines.push_back(trim(text.substr(from, to - from)));  // trim also drops a "\r"
        from = to + 1;
    }
    return lines;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

/// "l.5 \foo" -> "\foo"; empty when the line is not one of those
std::string sourceOfContext(const std::string& line) {
    if (!startsWith(line, "l.") || line.size() < 3 || !std::isdigit(static_cast<unsigned char>(line[2]))) {
        return {};
    }
    size_t i = 2;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
        i++;
    }
    return trim(line.substr(i));
}

/// "./tex.tex:5: message" -> "message"; empty when the line is not one of those
std::string afterFileLine(const std::string& line) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) {
        return {};
    }
    size_t i = colon + 1;
    const size_t digits = i;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
        i++;
    }
    if (i == digits || i >= line.size() || line[i] != ':') {
        return {};
    }
    return trim(line.substr(i + 1));
}

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string shorten(const std::string& s, size_t maxLength) {
    if (s.size() <= maxLength) {
        return s;
    }
    size_t cut = maxLength;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
        cut--;  // not in the middle of a UTF-8 character
    }
    return trim(s.substr(0, cut)) + "\xE2\x80\xA6";
}
}  // namespace

std::string firstErrorLine(const std::string& output, size_t maxLength) {
    const std::vector<std::string> lines = linesOf(output);

    for (size_t i = 0; i < lines.size(); i++) {
        std::string message;
        size_t contextFrom = i + 1;
        if (startsWith(lines[i], "!")) {
            message = trim(lines[i].substr(1));
        } else if (std::string m = afterFileLine(lines[i]); !m.empty()) {
            message = m;
        } else {
            continue;
        }
        if (message.empty()) {
            continue;
        }
        // the source the error points at: the "l.5 ..." line that follows, within the help text of the error
        for (size_t j = contextFrom; j < lines.size() && j < contextFrom + 8; j++) {
            if (startsWith(lines[j], "!")) {
                break;  // the next error
            }
            std::string source = sourceOfContext(lines[j]);
            if (!source.empty()) {
                message += " \xE2\x80\x94 " + source;  // em dash
                break;
            }
        }
        return shorten(message, maxLength);
    }

    for (const std::string& l: lines) {
        if (lowered(l).find("error") != std::string::npos) {
            return shorten(l, maxLength);
        }
    }
    for (const std::string& l: lines) {
        if (!l.empty()) {
            return shorten(l, maxLength);
        }
    }
    return {};
}

}  // namespace ambitus::latex
