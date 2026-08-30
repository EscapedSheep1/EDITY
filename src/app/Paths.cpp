#include "app/Paths.h"

#include "app/Utf8.h"

#include <windows.h>
#include <shlobj.h>

namespace edity {
namespace {

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw))) {
        return {};
    }
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

}  // namespace

std::filesystem::path ExecutableDir() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path UiDir() {
    return ExecutableDir() / "ui";
}

std::filesystem::path AppDataDir() {
    auto root = KnownFolder(FOLDERID_RoamingAppData);
    if (root.empty()) {
        root = std::filesystem::temp_directory_path();
    }
    const auto dir = root / "EDITY";
    EnsureDirectory(dir);
    return dir;
}

std::filesystem::path SettingsPath() {
    return AppDataDir() / "settings.json";
}

std::filesystem::path TypesCatalogPath() {
    return AppDataDir() / "types-catalog.json";
}

std::filesystem::path WorkspaceDir(const std::string& profileId) {
    const auto dir = AppDataDir() / "workspace" / Utf8ToWide(profileId);
    EnsureDirectory(dir);
    return dir;
}

std::filesystem::path BackupsDir(const std::string& profileId) {
    const auto dir = AppDataDir() / "backups" / Utf8ToWide(profileId);
    EnsureDirectory(dir);
    return dir;
}

std::filesystem::path KindDir(const std::filesystem::path& workspaceRoot, const std::string& kind) {
    const auto dir = workspaceRoot / Utf8ToWide(kind);
    EnsureDirectory(dir);
    return dir;
}

void EnsureDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
}

}  // namespace edity
