#pragma once

#include "market/Models.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace edity {

struct ParseResult {
    bool ok = false;
    std::string error;
    MarketCategory market;
    TraderFile trader;
    TraderZone zone;
};

ParseResult ParseMarket(std::string_view filename, std::string_view raw);
ParseResult ParseTrader(std::string_view filename, std::string_view raw);
ParseResult ParseZone(std::string_view filename, std::string_view raw);

std::string SerializeMarket(const MarketCategory& cat);
std::string SerializeTrader(const TraderFile& trader);
std::string SerializeZone(const TraderZone& zone);

MarketCategory DefaultMarket(std::string_view filename, int version);
TraderFile DefaultTrader(std::string_view filename, int version);
TraderZone DefaultZone(std::string_view filename, int version);

void to_json(nlohmann::json& j, const ConnectionProfile& p);
void from_json(const nlohmann::json& j, ConnectionProfile& p);
void to_json(nlohmann::json& j, const MarketItem& item);
void to_json(nlohmann::json& j, const MarketCategory& cat);
void to_json(nlohmann::json& j, const TraderCategoryRef& ref);
void to_json(nlohmann::json& j, const TraderFile& trader);
void to_json(nlohmann::json& j, const TraderZone& zone);
void to_json(nlohmann::json& j, const QuarantineFile& file);
void to_json(nlohmann::json& j, const ValidationIssue& issue);

nlohmann::json MarketToUi(const MarketCategory& cat);
nlohmann::json TraderToUi(const TraderFile& trader);
nlohmann::json ZoneToUi(const TraderZone& zone);

MarketCategory MarketFromUi(const nlohmann::json& j);
TraderFile TraderFromUi(const nlohmann::json& j);
TraderZone ZoneFromUi(const nlohmann::json& j);

std::vector<std::string> DuplicateKeysInObject(std::string_view raw, std::string_view objectName);

}  // namespace edity
