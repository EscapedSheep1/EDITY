#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace edity {

struct TypeEntry {
    std::string name;
    std::string category;
    std::string file;
};

struct TypesFileRef {
    std::string folder;
    std::string filename;
    std::string Relative() const;
};

struct TypesCatalog {
    std::string folder;
    std::string importedAt;
    std::string error;
    int fileCount = 0;
    int skippedDuplicates = 0;
    std::vector<std::string> files;
    std::vector<TypeEntry> types;

    nlohmann::json ToUi() const;
};

std::string NormalizeRemotePath(std::string path);
std::string ParentRemotePath(std::string path);
std::string JoinRemotePath(std::string dir, const std::string& name);
std::pair<std::string, std::string> SplitRemoteFile(const std::string& path);
std::string GuessMissionRoot(std::string_view zonesPath);

std::vector<TypesFileRef> ParseEconomyCore(std::string_view raw);
void EnsureVanillaTypesFile(std::vector<TypesFileRef>& refs);
std::vector<TypeEntry> ParseTypesXml(std::string_view raw, std::string_view fileLabel);
TypesCatalog BuildTypesCatalog(const std::string& missionFolder,
                               const std::vector<std::pair<std::string, std::string>>& labeledXml);
TypesCatalog LoadTypesCatalog();
void SaveTypesCatalog(const TypesCatalog& catalog);

}  // namespace edity
