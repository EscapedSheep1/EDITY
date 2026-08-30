#include "app/Utf8.h"

#include <algorithm>
#include <windows.h>

namespace edity {

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string ToLowerAscii(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    });
    return out;
}

std::string Trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(begin, end - begin + 1));
}

bool IEquals(std::string_view a, std::string_view b) {
    return ToLowerAscii(a) == ToLowerAscii(b);
}

bool EndsWithI(std::string_view text, std::string_view suffix) {
    if (suffix.size() > text.size()) {
        return false;
    }
    return IEquals(text.substr(text.size() - suffix.size()), suffix);
}

std::string JsonFileName(std::string_view name) {
    std::string stem(name);
    if (EndsWithI(stem, ".json")) {
        stem.resize(stem.size() - 5);
    }
    std::string cleaned;
    cleaned.reserve(stem.size());
    for (const unsigned char c : stem) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            cleaned.push_back(static_cast<char>(c));
        } else if (c == ' ' || c == '.') {
            cleaned.push_back('_');
        }
    }
    if (cleaned.empty()) {
        cleaned = "NewFile";
    }
    return cleaned + ".json";
}

}  // namespace edity
