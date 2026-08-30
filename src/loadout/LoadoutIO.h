#pragma once

#include "market/Models.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace edity {

struct ParseLoadoutResult {
    bool ok = false;
    std::string error;
    LoadoutFile file;
};

LoadoutNode DefaultLoadoutNode(const std::string& className = "SurvivorM_Mirek");
LoadoutFile DefaultLoadout(std::string_view filename);
ParseLoadoutResult ParseLoadout(std::string_view filename, std::string_view raw);
std::string SerializeLoadout(const LoadoutFile& file);
nlohmann::json LoadoutToUi(const LoadoutFile& file);
LoadoutFile LoadoutFromUi(const nlohmann::json& body);

}  // namespace edity
