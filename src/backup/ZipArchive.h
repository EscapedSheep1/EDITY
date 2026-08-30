#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace edity {

struct ZipEntry {
    std::string name;
    std::string data;
};

void WriteZip(const std::filesystem::path& zipPath, const std::vector<ZipEntry>& entries);
std::string ZipWorkspaceFolder(const std::filesystem::path& workspaceRoot, const std::filesystem::path& zipPath);

}  // namespace edity
