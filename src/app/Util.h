#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace edity {

std::string NewUuid();
std::string NowStamp();
std::uint32_t Crc32(const void* data, std::size_t size);
std::string FileStem(std::string_view filename);
std::vector<std::string> SplitLines(std::string_view text);
std::string ReadFileUtf8(const std::filesystem::path& path);
void WriteFileAtomic(const std::filesystem::path& path, std::string_view contents);
std::string RelKey(std::string_view kind, std::string_view filename);

}  // namespace edity
