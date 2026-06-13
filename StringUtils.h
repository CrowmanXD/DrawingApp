#pragma once

#include <string>
#include <algorithm>
#include <cctype>

namespace StringUtils {
    /// Convert a string to lowercase in-place
    inline std::string toLower(std::string s) {
        for (char& c : s) c = std::tolower(static_cast<unsigned char>(c));
        return s;
    }

    /// Trim whitespace and line endings from the end of a string
    inline std::string trimEnd(std::string s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
            s.pop_back();
        }
        return s;
    }

    /// Trim whitespace from both ends
    inline std::string trim(std::string s) {
        // Trim end
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
            s.pop_back();
        }
        // Trim start
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
            start++;
        }
        return s.substr(start);
    }
}
