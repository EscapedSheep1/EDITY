#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace edity {

struct TypeFlags {
    int countInCargo = 0;
    int countInHoarder = 0;
    int countInMap = 1;
    int countInPlayer = 0;
    int crafted = 0;
    int deloot = 0;
};

struct TypeDefinition {
    std::string name;
    int nominal = 0;
    int lifetime = 14400;
    int restock = 0;
    int min = 0;
    int quantMin = -1;
    int quantMax = -1;
    int cost = 100;
    TypeFlags flags;
    std::string category;
    std::vector<std::string> usages;
    std::vector<std::string> values;
    std::vector<std::string> tags;
};

struct TypesDocument {
    std::string relPath;
    std::vector<TypeDefinition> types;
};

struct ParseTypesResult {
    bool ok = false;
    std::string error;
    TypesDocument doc;
};

TypeDefinition DefaultType(const std::string& name);
ParseTypesResult ParseTypesDocument(std::string_view raw, std::string_view relPath);
std::string SerializeTypesDocument(const TypesDocument& doc);

nlohmann::json TypesFileToUi(const TypesDocument& doc);
TypesDocument TypesFileFromUi(const nlohmann::json& body);

std::string TypesFileName(std::string_view name);
bool EconomyCoreHasFile(std::string_view xml, std::string_view relPath);
void EconomyCoreAddFile(std::string& xml, const std::string& relPath);
void EconomyCoreRemoveFile(std::string& xml, const std::string& relPath);

}  // namespace edity
