#include "market/JsonIO.h"

#include "app/Utf8.h"
#include "app/Util.h"

#include <cctype>
#include <initializer_list>
#include <sstream>
#include <unordered_set>

namespace edity {
namespace {

int JsonInt(const nlohmann::json& j, const char* key, int fallback) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (j[key].is_number_integer()) {
        return j[key].get<int>();
    }
    if (j[key].is_number()) {
        return static_cast<int>(j[key].get<double>());
    }
    if (j[key].is_string()) {
        try {
            return std::stoi(j[key].get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

double JsonDouble(const nlohmann::json& j, const char* key, double fallback) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (j[key].is_number()) {
        return j[key].get<double>();
    }
    if (j[key].is_string()) {
        try {
            return std::stod(j[key].get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::string JsonString(const nlohmann::json& j, const char* key, const std::string& fallback = {}) {
    if (!j.contains(key) || !j[key].is_string()) {
        return fallback;
    }
    return j[key].get<std::string>();
}

std::vector<std::string> JsonStringArray(const nlohmann::json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.contains(key) || !j[key].is_array()) {
        return out;
    }
    for (const auto& item : j[key]) {
        if (item.is_string()) {
            out.push_back(item.get<std::string>());
        }
    }
    return out;
}

nlohmann::json CaptureExtras(const nlohmann::json& json, std::initializer_list<const char*> known) {
    nlohmann::json extras = nlohmann::json::object();
    for (auto it = json.begin(); it != json.end(); ++it) {
        bool isKnown = false;
        for (const char* key : known) {
            if (it.key() == key) {
                isKnown = true;
                break;
            }
        }
        if (!isKnown) {
            extras[it.key()] = it.value();
        }
    }
    return extras;
}

nlohmann::json WithExtras(const nlohmann::json& extras, nlohmann::json known) {
    nlohmann::json out = extras.is_object() ? extras : nlohmann::json::object();
    for (auto it = known.begin(); it != known.end(); ++it) {
        out[it.key()] = it.value();
    }
    return out;
}

TraderCategoryRef ParseCategoryRef(const std::string& raw) {
    TraderCategoryRef ref;
    const auto colon = raw.rfind(':');
    if (colon != std::string::npos && colon + 1 < raw.size() && std::isdigit(static_cast<unsigned char>(raw[colon + 1]))) {
        ref.fileStem = raw.substr(0, colon);
        try {
            ref.mode = std::stoi(raw.substr(colon + 1));
        } catch (...) {
            ref.mode = 1;
        }
    } else {
        ref.fileStem = raw;
        ref.mode = 1;
    }
    return ref;
}

std::string Pretty(const nlohmann::json& j) {
    return j.dump(4);
}

}  // namespace

ParseResult ParseMarket(std::string_view filename, std::string_view raw) {
    ParseResult result;
    result.market.filename = std::string(filename);
    try {
        const auto json = nlohmann::json::parse(raw.begin(), raw.end());
        if (!json.is_object()) {
            result.error = "Market file root must be a JSON object";
            return result;
        }
        result.market.version = JsonInt(json, "m_Version", 12);
        result.market.displayName = JsonString(json, "DisplayName");
        result.market.icon = JsonString(json, "Icon", "Deliver");
        result.market.color = JsonString(json, "Color", "FBFCFEFF");
        result.market.isExchange = JsonInt(json, "IsExchange", 0);
        result.market.initStockPercent = JsonDouble(json, "InitStockPercent", 75.0);
        result.market.extras = CaptureExtras(json, {"m_Version", "DisplayName", "Icon", "Color", "IsExchange",
                                                    "InitStockPercent", "Items"});
        if (json.contains("Items") && json["Items"].is_array()) {
            for (const auto& itemJson : json["Items"]) {
                if (!itemJson.is_object()) {
                    continue;
                }
                MarketItem item;
                item.className = JsonString(itemJson, "ClassName");
                item.maxPriceThreshold = JsonInt(itemJson, "MaxPriceThreshold", 0);
                item.minPriceThreshold = JsonInt(itemJson, "MinPriceThreshold", 0);
                item.sellPricePercent = JsonDouble(itemJson, "SellPricePercent", -1.0);
                item.maxStockThreshold = JsonInt(itemJson, "MaxStockThreshold", 1);
                item.minStockThreshold = JsonInt(itemJson, "MinStockThreshold", 1);
                item.quantityPercent = JsonInt(itemJson, "QuantityPercent", -1);
                item.spawnAttachments = JsonStringArray(itemJson, "SpawnAttachments");
                item.variants = JsonStringArray(itemJson, "Variants");
                result.market.items.push_back(std::move(item));
            }
        }
        result.ok = true;
    } catch (const std::exception& ex) {
        result.error = ex.what();
    }
    return result;
}

ParseResult ParseTrader(std::string_view filename, std::string_view raw) {
    ParseResult result;
    result.trader.filename = std::string(filename);
    try {
        const auto json = nlohmann::json::parse(raw.begin(), raw.end());
        if (!json.is_object()) {
            result.error = "Trader file root must be a JSON object";
            return result;
        }
        result.trader.version = JsonInt(json, "m_Version", 13);
        result.trader.traderName = JsonString(json, "TraderName");
        result.trader.displayName = JsonString(json, "DisplayName");
        result.trader.traderIcon = JsonString(json, "TraderIcon", "Deliver");
        result.trader.currencies = JsonStringArray(json, "Currencies");
        result.trader.minRequiredReputation = JsonInt(json, "MinRequiredReputation", 0);
        result.trader.maxRequiredReputation = JsonInt(json, "MaxRequiredReputation", 2147483647);
        result.trader.requiredFaction = JsonString(json, "RequiredFaction");
        result.trader.requiredCompletedQuestId = JsonInt(json, "RequiredCompletedQuestID", -1);
        result.trader.displayCurrencyValue = JsonInt(json, "DisplayCurrencyValue", 1);
        result.trader.displayCurrencyName = JsonString(json, "DisplayCurrencyName");
        result.trader.useCategoryOrder = JsonInt(json, "UseCategoryOrder", 0);
        result.trader.extras = CaptureExtras(json, {"m_Version", "TraderName", "DisplayName", "TraderIcon", "Currencies",
                                                   "Categories", "Items", "MinRequiredReputation",
                                                   "MaxRequiredReputation", "RequiredFaction",
                                                   "RequiredCompletedQuestID", "DisplayCurrencyValue",
                                                   "DisplayCurrencyName", "UseCategoryOrder"});
        if (json.contains("Categories") && json["Categories"].is_array()) {
            for (const auto& cat : json["Categories"]) {
                if (cat.is_string()) {
                    result.trader.categories.push_back(ParseCategoryRef(cat.get<std::string>()));
                }
            }
        }
        if (json.contains("Items") && json["Items"].is_object()) {
            for (auto it = json["Items"].begin(); it != json["Items"].end(); ++it) {
                int mode = 1;
                if (it.value().is_number_integer()) {
                    mode = it.value().get<int>();
                } else if (it.value().is_number()) {
                    mode = static_cast<int>(it.value().get<double>());
                }
                result.trader.items.emplace_back(it.key(), mode);
            }
        }
        result.ok = true;
    } catch (const std::exception& ex) {
        result.error = ex.what();
    }
    return result;
}

ParseResult ParseZone(std::string_view filename, std::string_view raw) {
    ParseResult result;
    result.zone.filename = std::string(filename);
    try {
        const auto json = nlohmann::json::parse(raw.begin(), raw.end());
        if (!json.is_object()) {
            result.error = "TraderZone file root must be a JSON object";
            return result;
        }
        result.zone.version = JsonInt(json, "m_Version", 6);
        result.zone.displayName = JsonString(json, "m_DisplayName");
        if (json.contains("Position") && json["Position"].is_array() && json["Position"].size() >= 3) {
            result.zone.positionX = json["Position"][0].is_number() ? json["Position"][0].get<double>() : 0;
            result.zone.positionY = json["Position"][1].is_number() ? json["Position"][1].get<double>() : 0;
            result.zone.positionZ = json["Position"][2].is_number() ? json["Position"][2].get<double>() : 0;
        }
        result.zone.radius = JsonDouble(json, "Radius", 50);
        result.zone.buyPricePercent = JsonDouble(json, "BuyPricePercent", 100);
        result.zone.sellPricePercent = JsonDouble(json, "SellPricePercent", -1);
        result.zone.extras = CaptureExtras(json, {"m_Version", "m_DisplayName", "Position", "Radius",
                                                 "BuyPricePercent", "SellPricePercent", "Stock"});
        if (json.contains("Stock") && json["Stock"].is_object()) {
            for (auto it = json["Stock"].begin(); it != json["Stock"].end(); ++it) {
                int stock = 0;
                if (it.value().is_number_integer()) {
                    stock = it.value().get<int>();
                } else if (it.value().is_number()) {
                    stock = static_cast<int>(it.value().get<double>());
                }
                result.zone.stock.emplace_back(it.key(), stock);
            }
        }
        result.ok = true;
    } catch (const std::exception& ex) {
        result.error = ex.what();
    }
    return result;
}

nlohmann::json MarketToDisk(const MarketCategory& cat) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : cat.items) {
        items.push_back({
            {"ClassName", item.className},
            {"MaxPriceThreshold", item.maxPriceThreshold},
            {"MinPriceThreshold", item.minPriceThreshold},
            {"SellPricePercent", item.sellPricePercent},
            {"MaxStockThreshold", item.maxStockThreshold},
            {"MinStockThreshold", item.minStockThreshold},
            {"QuantityPercent", item.quantityPercent},
            {"SpawnAttachments", item.spawnAttachments},
            {"Variants", item.variants},
        });
    }
    return WithExtras(cat.extras, {
        {"m_Version", cat.version},
        {"DisplayName", cat.displayName},
        {"Icon", cat.icon},
        {"Color", cat.color},
        {"IsExchange", cat.isExchange},
        {"InitStockPercent", cat.initStockPercent},
        {"Items", items},
    });
}

nlohmann::json TraderToDisk(const TraderFile& trader) {
    nlohmann::json categories = nlohmann::json::array();
    for (const auto& cat : trader.categories) {
        categories.push_back(cat.fileStem + ":" + std::to_string(cat.mode));
    }
    nlohmann::json items = nlohmann::json::object();
    for (const auto& [name, mode] : trader.items) {
        items[name] = mode;
    }
    nlohmann::json known = {
        {"m_Version", trader.version},
        {"DisplayName", trader.displayName},
        {"MinRequiredReputation", trader.minRequiredReputation},
        {"MaxRequiredReputation", trader.maxRequiredReputation},
        {"RequiredFaction", trader.requiredFaction},
        {"RequiredCompletedQuestID", trader.requiredCompletedQuestId},
        {"TraderIcon", trader.traderIcon},
        {"Currencies", trader.currencies},
        {"DisplayCurrencyValue", trader.displayCurrencyValue},
        {"DisplayCurrencyName", trader.displayCurrencyName},
        {"UseCategoryOrder", trader.useCategoryOrder},
        {"Categories", categories},
        {"Items", items},
    };
    if (!trader.traderName.empty()) {
        known["TraderName"] = trader.traderName;
    }
    return WithExtras(trader.extras, std::move(known));
}

nlohmann::json ZoneToDisk(const TraderZone& zone) {
    nlohmann::json stock = nlohmann::json::object();
    for (const auto& [name, count] : zone.stock) {
        stock[name] = count;
    }
    return WithExtras(zone.extras, {
        {"m_Version", zone.version},
        {"m_DisplayName", zone.displayName},
        {"Position", nlohmann::json::array({zone.positionX, zone.positionY, zone.positionZ})},
        {"Radius", zone.radius},
        {"BuyPricePercent", zone.buyPricePercent},
        {"SellPricePercent", zone.sellPricePercent},
        {"Stock", stock},
    });
}

std::string SerializeMarket(const MarketCategory& cat) {
    return Pretty(MarketToDisk(cat));
}

std::string SerializeTrader(const TraderFile& trader) {
    return Pretty(TraderToDisk(trader));
}

std::string SerializeZone(const TraderZone& zone) {
    return Pretty(ZoneToDisk(zone));
}

std::string DisplayFromFile(std::string_view filename) {
    std::string name = FileStem(filename);
    for (char& c : name) {
        if (c == '_') {
            c = ' ';
        }
    }
    return name;
}

MarketCategory DefaultMarket(std::string_view filename, int version) {
    MarketCategory cat;
    cat.filename = std::string(filename);
    cat.version = version > 0 ? version : 12;
    cat.displayName = DisplayFromFile(filename);
    return cat;
}

TraderFile DefaultTrader(std::string_view filename, int version) {
    TraderFile trader;
    trader.filename = std::string(filename);
    trader.version = version > 0 ? version : 13;
    if (trader.version < 13) {
        trader.traderName = FileStem(filename);
    }
    trader.displayName = DisplayFromFile(filename);
    trader.currencies = {"ExpansionBanknoteEuro"};
    return trader;
}

TraderZone DefaultZone(std::string_view filename, int version) {
    TraderZone zone;
    zone.filename = std::string(filename);
    zone.version = version > 0 ? version : 6;
    zone.displayName = DisplayFromFile(filename);
    return zone;
}

void to_json(nlohmann::json& j, const ConnectionProfile& p) {
    j = {
        {"id", p.id},
        {"name", p.name},
        {"protocol", ProtocolName(p.protocol)},
        {"host", p.host},
        {"port", p.port},
        {"username", p.username},
        {"passive", p.passive},
        {"marketPath", p.marketPath},
        {"tradersPath", p.tradersPath},
        {"zonesPath", p.zonesPath},
        {"loadoutsPath", p.loadoutsPath},
    };
}

void from_json(const nlohmann::json& j, ConnectionProfile& p) {
    p.id = JsonString(j, "id");
    p.name = JsonString(j, "name");
    p.protocol = ProtocolFromName(JsonString(j, "protocol", "sftp"));
    p.host = JsonString(j, "host");
    p.port = JsonInt(j, "port", p.protocol == Protocol::Sftp ? 22 : 21);
    p.username = JsonString(j, "username");
    p.passive = !j.contains("passive") || j["passive"].is_null() ? true : j.value("passive", true);
    p.marketPath = JsonString(j, "marketPath");
    p.tradersPath = JsonString(j, "tradersPath");
    p.zonesPath = JsonString(j, "zonesPath");
    p.loadoutsPath = JsonString(j, "loadoutsPath");
}

void to_json(nlohmann::json& j, const MarketItem& item) {
    j = {
        {"className", item.className},
        {"maxPriceThreshold", item.maxPriceThreshold},
        {"minPriceThreshold", item.minPriceThreshold},
        {"sellPricePercent", item.sellPricePercent},
        {"maxStockThreshold", item.maxStockThreshold},
        {"minStockThreshold", item.minStockThreshold},
        {"quantityPercent", item.quantityPercent},
        {"spawnAttachments", item.spawnAttachments},
        {"variants", item.variants},
    };
}

void to_json(nlohmann::json& j, const MarketCategory& cat) {
    j = MarketToUi(cat);
}

void to_json(nlohmann::json& j, const TraderCategoryRef& ref) {
    j = {{"fileStem", ref.fileStem}, {"mode", ref.mode}};
}

void to_json(nlohmann::json& j, const TraderFile& trader) {
    j = TraderToUi(trader);
}

void to_json(nlohmann::json& j, const TraderZone& zone) {
    j = ZoneToUi(zone);
}

void to_json(nlohmann::json& j, const QuarantineFile& file) {
    j = {
        {"kind", KindName(file.kind)},
        {"filename", file.filename},
        {"error", file.error},
    };
}

void to_json(nlohmann::json& j, const ValidationIssue& issue) {
    j = {
        {"severity", issue.severity == ValidationIssue::Severity::Warning ? "warning" : "error"},
        {"kind", KindName(issue.kind)},
        {"filename", issue.filename},
        {"field", issue.field},
        {"message", issue.message},
        {"itemIndex", issue.itemIndex},
    };
}

nlohmann::json MarketToUi(const MarketCategory& cat) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : cat.items) {
        items.push_back(item);
    }
    return {
        {"filename", cat.filename},
        {"version", cat.version},
        {"displayName", cat.displayName},
        {"icon", cat.icon},
        {"color", cat.color},
        {"isExchange", cat.isExchange},
        {"initStockPercent", cat.initStockPercent},
        {"items", items},
        {"extras", cat.extras.is_object() ? cat.extras : nlohmann::json::object()},
    };
}

nlohmann::json TraderToUi(const TraderFile& trader) {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& [name, mode] : trader.items) {
        items.push_back({{"className", name}, {"mode", mode}});
    }
    return {
        {"filename", trader.filename},
        {"version", trader.version},
        {"traderName", trader.traderName},
        {"displayName", trader.displayName},
        {"traderIcon", trader.traderIcon},
        {"currencies", trader.currencies},
        {"minRequiredReputation", trader.minRequiredReputation},
        {"maxRequiredReputation", trader.maxRequiredReputation},
        {"requiredFaction", trader.requiredFaction},
        {"requiredCompletedQuestId", trader.requiredCompletedQuestId},
        {"displayCurrencyValue", trader.displayCurrencyValue},
        {"displayCurrencyName", trader.displayCurrencyName},
        {"useCategoryOrder", trader.useCategoryOrder},
        {"categories", trader.categories},
        {"items", items},
        {"extras", trader.extras.is_object() ? trader.extras : nlohmann::json::object()},
    };
}

nlohmann::json ZoneToUi(const TraderZone& zone) {
    nlohmann::json stock = nlohmann::json::array();
    for (const auto& [name, count] : zone.stock) {
        stock.push_back({{"className", name}, {"stock", count}});
    }
    return {
        {"filename", zone.filename},
        {"version", zone.version},
        {"displayName", zone.displayName},
        {"position", {{"x", zone.positionX}, {"y", zone.positionY}, {"z", zone.positionZ}}},
        {"radius", zone.radius},
        {"buyPricePercent", zone.buyPricePercent},
        {"sellPricePercent", zone.sellPricePercent},
        {"stock", stock},
        {"extras", zone.extras.is_object() ? zone.extras : nlohmann::json::object()},
    };
}

MarketCategory MarketFromUi(const nlohmann::json& j) {
    MarketCategory cat;
    cat.filename = JsonString(j, "filename");
    cat.version = JsonInt(j, "version", 12);
    cat.displayName = JsonString(j, "displayName");
    cat.icon = JsonString(j, "icon", "Deliver");
    cat.color = JsonString(j, "color", "FBFCFEFF");
    cat.isExchange = JsonInt(j, "isExchange", 0);
    cat.initStockPercent = JsonDouble(j, "initStockPercent", 75);
    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& itemJson : j["items"]) {
            MarketItem item;
            item.className = JsonString(itemJson, "className");
            item.maxPriceThreshold = JsonInt(itemJson, "maxPriceThreshold", 0);
            item.minPriceThreshold = JsonInt(itemJson, "minPriceThreshold", 0);
            item.sellPricePercent = JsonDouble(itemJson, "sellPricePercent", -1);
            item.maxStockThreshold = JsonInt(itemJson, "maxStockThreshold", 1);
            item.minStockThreshold = JsonInt(itemJson, "minStockThreshold", 1);
            item.quantityPercent = JsonInt(itemJson, "quantityPercent", -1);
            item.spawnAttachments = JsonStringArray(itemJson, "spawnAttachments");
            item.variants = JsonStringArray(itemJson, "variants");
            cat.items.push_back(std::move(item));
        }
    }
    if (j.contains("extras") && j["extras"].is_object()) {
        cat.extras = j["extras"];
    }
    return cat;
}

TraderFile TraderFromUi(const nlohmann::json& j) {
    TraderFile trader;
    trader.filename = JsonString(j, "filename");
    trader.version = JsonInt(j, "version", 13);
    trader.traderName = JsonString(j, "traderName");
    trader.displayName = JsonString(j, "displayName");
    trader.traderIcon = JsonString(j, "traderIcon", "Deliver");
    trader.currencies = JsonStringArray(j, "currencies");
    trader.minRequiredReputation = JsonInt(j, "minRequiredReputation", 0);
    trader.maxRequiredReputation = JsonInt(j, "maxRequiredReputation", 2147483647);
    trader.requiredFaction = JsonString(j, "requiredFaction");
    trader.requiredCompletedQuestId = JsonInt(j, "requiredCompletedQuestId", -1);
    trader.displayCurrencyValue = JsonInt(j, "displayCurrencyValue", 1);
    trader.displayCurrencyName = JsonString(j, "displayCurrencyName");
    trader.useCategoryOrder = JsonInt(j, "useCategoryOrder", 0);
    if (j.contains("categories") && j["categories"].is_array()) {
        for (const auto& cat : j["categories"]) {
            TraderCategoryRef ref;
            ref.fileStem = JsonString(cat, "fileStem");
            ref.mode = JsonInt(cat, "mode", 1);
            if (!ref.fileStem.empty()) {
                trader.categories.push_back(std::move(ref));
            }
        }
    }
    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& item : j["items"]) {
            const auto name = JsonString(item, "className");
            if (!name.empty()) {
                trader.items.emplace_back(name, JsonInt(item, "mode", 1));
            }
        }
    }
    if (j.contains("extras") && j["extras"].is_object()) {
        trader.extras = j["extras"];
    }
    return trader;
}

TraderZone ZoneFromUi(const nlohmann::json& j) {
    TraderZone zone;
    zone.filename = JsonString(j, "filename");
    zone.version = JsonInt(j, "version", 6);
    zone.displayName = JsonString(j, "displayName");
    if (j.contains("position") && j["position"].is_object()) {
        zone.positionX = JsonDouble(j["position"], "x", 0);
        zone.positionY = JsonDouble(j["position"], "y", 0);
        zone.positionZ = JsonDouble(j["position"], "z", 0);
    }
    zone.radius = JsonDouble(j, "radius", 50);
    zone.buyPricePercent = JsonDouble(j, "buyPricePercent", 100);
    zone.sellPricePercent = JsonDouble(j, "sellPricePercent", -1);
    if (j.contains("stock") && j["stock"].is_array()) {
        for (const auto& row : j["stock"]) {
            const auto name = JsonString(row, "className");
            if (!name.empty()) {
                zone.stock.emplace_back(name, JsonInt(row, "stock", 0));
            }
        }
    }
    if (j.contains("extras") && j["extras"].is_object()) {
        zone.extras = j["extras"];
    }
    return zone;
}

std::vector<std::string> DuplicateKeysInObject(std::string_view raw, std::string_view objectName) {
    std::vector<std::string> duplicates;
    const std::string needle = "\"" + std::string(objectName) + "\"";
    const auto pos = raw.find(needle);
    if (pos == std::string_view::npos) {
        return duplicates;
    }
    auto cursor = raw.find('{', pos + needle.size());
    if (cursor == std::string_view::npos) {
        return duplicates;
    }
    int depth = 0;
    std::unordered_set<std::string> seen;
    bool inString = false;
    bool escape = false;
    std::string key;
    bool collectingKey = false;
    enum class Expect { Key, Colon, Value };
    Expect expect = Expect::Key;

    for (std::size_t i = cursor; i < raw.size(); ++i) {
        const char c = raw[i];
        if (inString) {
            if (escape) {
                if (collectingKey) {
                    key.push_back(c);
                }
                escape = false;
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') {
                inString = false;
                if (collectingKey && expect == Expect::Key) {
                    const auto lowered = ToLowerAscii(key);
                    if (!seen.insert(lowered).second) {
                        duplicates.push_back(key);
                    }
                    collectingKey = false;
                    expect = Expect::Colon;
                }
                continue;
            }
            if (collectingKey) {
                key.push_back(c);
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            if (expect == Expect::Key && depth == 1) {
                collectingKey = true;
                key.clear();
            }
            continue;
        }
        if (c == '{') {
            ++depth;
            continue;
        }
        if (c == '}') {
            --depth;
            if (depth == 0) {
                break;
            }
            continue;
        }
        if (c == ':' && expect == Expect::Colon) {
            expect = Expect::Value;
            continue;
        }
        if (c == ',' && expect == Expect::Value && depth == 1) {
            expect = Expect::Key;
        }
    }
    return duplicates;
}

}  // namespace edity
