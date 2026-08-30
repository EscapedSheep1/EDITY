#pragma once

#include <string>
#include <string_view>

namespace edity {

std::wstring Utf8ToWide(std::string_view utf8);
std::string WideToUtf8(std::wstring_view wide);
std::string ToLowerAscii(std::string_view text);
std::string Trim(std::string_view text);
bool IEquals(std::string_view a, std::string_view b);
bool EndsWithI(std::string_view text, std::string_view suffix);
std::string JsonFileName(std::string_view name);

}  // namespace edity
