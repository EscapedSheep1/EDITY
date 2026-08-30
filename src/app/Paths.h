#pragma once

#include <filesystem>
#include <string>

namespace edity {

std::filesystem::path ExecutableDir();
std::filesystem::path UiDir();
std::filesystem::path AppDataDir();
std::filesystem::path SettingsPath();
std::filesystem::path TypesCatalogPath();
std::filesystem::path WorkspaceDir(const std::string& profileId);
std::filesystem::path BackupsDir(const std::string& profileId);
std::filesystem::path KindDir(const std::filesystem::path& workspaceRoot, const std::string& kind);
void EnsureDirectory(const std::filesystem::path& path);

}  // namespace edity
