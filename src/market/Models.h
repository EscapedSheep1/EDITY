#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace edity {

enum class Protocol { Ftp, Ftps, Sftp };
enum class FileKind { Market, Trader, TraderZone, Types, Loadout };

inline const char* KindName(FileKind kind) {
    switch (kind) {
        case FileKind::Market:
            return "Market";
        case FileKind::Trader:
            return "Traders";
        case FileKind::TraderZone:
            return "TraderZones";
        case FileKind::Types:
            return "Types";
        case FileKind::Loadout:
            return "Loadouts";
    }
    return "Market";
}

inline FileKind KindFromName(std::string_view name) {
    if (name == "Traders" || name == "Trader") {
        return FileKind::Trader;
    }
    if (name == "TraderZones" || name == "Zones") {
        return FileKind::TraderZone;
    }
    if (name == "Types" || name == "Type") {
        return FileKind::Types;
    }
    if (name == "Loadouts" || name == "Loadout") {
        return FileKind::Loadout;
    }
    return FileKind::Market;
}

inline const char* ProtocolName(Protocol protocol) {
    switch (protocol) {
        case Protocol::Ftps:
            return "ftps";
        case Protocol::Sftp:
            return "sftp";
        case Protocol::Ftp:
        default:
            return "ftp";
    }
}

inline Protocol ProtocolFromName(std::string_view name) {
    if (name == "ftps" || name == "FTPS") {
        return Protocol::Ftps;
    }
    if (name == "sftp" || name == "SFTP") {
        return Protocol::Sftp;
    }
    return Protocol::Ftp;
}

struct ConnectionProfile {
    std::string id;
    std::string name;
    Protocol protocol = Protocol::Sftp;
    std::string host;
    int port = 22;
    std::string username;
    bool passive = true;
    std::string marketPath;
    std::string tradersPath;
    std::string zonesPath;
    std::string loadoutsPath;
};

struct MarketItem {
    std::string className;
    int maxPriceThreshold = 0;
    int minPriceThreshold = 0;
    double sellPricePercent = -1.0;
    int maxStockThreshold = 1;
    int minStockThreshold = 1;
    int quantityPercent = -1;
    std::vector<std::string> spawnAttachments;
    std::vector<std::string> variants;
};

struct MarketCategory {
    std::string filename;
    int version = 12;
    std::string displayName;
    std::string icon = "Deliver";
    std::string color = "FBFCFEFF";
    int isExchange = 0;
    double initStockPercent = 75.0;
    std::vector<MarketItem> items;
    nlohmann::json extras = nlohmann::json::object();
};

struct TraderCategoryRef {
    std::string fileStem;
    int mode = 1;
};

struct TraderFile {
    std::string filename;
    int version = 13;
    std::string traderName;
    std::string displayName;
    std::string traderIcon = "Deliver";
    std::vector<std::string> currencies;
    std::vector<TraderCategoryRef> categories;
    std::vector<std::pair<std::string, int>> items;
    int minRequiredReputation = 0;
    int maxRequiredReputation = 2147483647;
    std::string requiredFaction;
    int requiredCompletedQuestId = -1;
    int displayCurrencyValue = 1;
    std::string displayCurrencyName;
    int useCategoryOrder = 0;
    nlohmann::json extras = nlohmann::json::object();
};

struct TraderZone {
    std::string filename;
    int version = 6;
    std::string displayName;
    double positionX = 0;
    double positionY = 0;
    double positionZ = 0;
    double radius = 50;
    double buyPricePercent = 100;
    double sellPricePercent = -1;
    std::vector<std::pair<std::string, int>> stock;
    nlohmann::json extras = nlohmann::json::object();
};

struct LoadoutHealth {
    double min = 1.0;
    double max = 1.0;
    std::string zone;
};

struct LoadoutNode {
    std::string className;
    std::string includeFile;
    double chance = 1.0;
    double quantityMin = 0.0;
    double quantityMax = 0.0;
    std::vector<LoadoutHealth> health;
    std::vector<std::pair<std::string, std::vector<LoadoutNode>>> attachments;
    std::vector<LoadoutNode> cargo;
    std::vector<LoadoutNode> sets;
    std::vector<std::string> constructionParts;
};

struct LoadoutFile {
    std::string filename;
    LoadoutNode root;
};

struct QuarantineFile {
    FileKind kind = FileKind::Market;
    std::string filename;
    std::string error;
};

struct ValidationIssue {
    enum class Severity { Error, Warning };
    Severity severity = Severity::Error;
    FileKind kind = FileKind::Market;
    std::string filename;
    std::string field;
    std::string message;
    int itemIndex = -1;
};

}  // namespace edity
