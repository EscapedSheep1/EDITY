#include "market/Validator.h"

#include "app/Utf8.h"
#include "app/Util.h"
#include "market/JsonIO.h"

#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace edity {
namespace {

bool IsHexColor(std::string_view color) {
    static const std::regex hex("^[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$");
    return std::regex_match(color.begin(), color.end(), hex);
}

void Add(std::vector<ValidationIssue>& issues, ValidationIssue::Severity severity, FileKind kind,
         std::string filename, std::string field, std::string message, int itemIndex = -1) {
    ValidationIssue issue;
    issue.severity = severity;
    issue.kind = kind;
    issue.filename = std::move(filename);
    issue.field = std::move(field);
    issue.message = std::move(message);
    issue.itemIndex = itemIndex;
    issues.push_back(std::move(issue));
}

struct ClassLoc {
    std::string filename;
    bool isPrimary = true;
};

}  // namespace

std::vector<ValidationIssue> ValidateWorkspace(const WorkspaceSnapshot& snap) {
    std::vector<ValidationIssue> issues;

    for (const auto& bad : snap.quarantine) {
        Add(issues, ValidationIssue::Severity::Error, bad.kind, bad.filename, "json",
            "File failed to parse and was quarantined: " + bad.error);
    }

    std::unordered_map<std::string, std::string> marketStems;
    std::unordered_map<std::string, ClassLoc> classIndex;
    std::unordered_set<std::string> allClassNames;

    for (const auto& cat : snap.markets) {
        marketStems.emplace(ToLowerAscii(FileStem(cat.filename)), cat.filename);
        if (Trim(cat.displayName).empty()) {
            Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, "DisplayName",
                "DisplayName is required");
        }
        if (!IsHexColor(cat.color)) {
            Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, "Color",
                "Color must be 6 or 8 hex characters without #");
        }
        if (cat.isExchange != 0 && cat.isExchange != 1) {
            Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, "IsExchange",
                "IsExchange must be 0 or 1");
        }
        if (cat.initStockPercent < 0.0 || cat.initStockPercent > 100.0) {
            Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, "InitStockPercent",
                "InitStockPercent must be between 0 and 100");
        }

        std::unordered_set<std::string> inFile;
        for (int i = 0; i < static_cast<int>(cat.items.size()); ++i) {
            const auto& item = cat.items[static_cast<std::size_t>(i)];
            const auto className = Trim(item.className);
            if (className.empty()) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, "ClassName",
                    "Item ClassName cannot be empty", i);
                continue;
            }
            const auto key = ToLowerAscii(className);
            if (!inFile.insert(key).second) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, className,
                    "ClassName is duplicated inside this file", i);
            }
            auto [it, inserted] = classIndex.emplace(key, ClassLoc{cat.filename, true});
            if (!inserted && !IEquals(it->second.filename, cat.filename)) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, className,
                    "ClassName already exists in " + it->second.filename, i);
            } else if (!inserted && IEquals(it->second.filename, cat.filename) && it->second.isPrimary) {
                // already reported as in-file duplicate
            }
            allClassNames.insert(key);
            if (item.minPriceThreshold > item.maxPriceThreshold) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, className,
                    "MinPriceThreshold cannot exceed MaxPriceThreshold", i);
            }
            if (item.minStockThreshold > item.maxStockThreshold) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, className,
                    "MinStockThreshold cannot exceed MaxStockThreshold", i);
            }
            if (!(item.quantityPercent == -1 || (item.quantityPercent >= 0 && item.quantityPercent <= 100))) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, className,
                    "QuantityPercent must be -1 or 0 to 100", i);
            }
            if (item.sellPricePercent < -1.0) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, className,
                    "SellPricePercent must be -1 or greater than or equal to 0", i);
            }
        }
    }

    for (const auto& cat : snap.markets) {
        for (int i = 0; i < static_cast<int>(cat.items.size()); ++i) {
            const auto& item = cat.items[static_cast<std::size_t>(i)];
            for (const auto& variant : item.variants) {
                const auto key = ToLowerAscii(Trim(variant));
                if (key.empty()) {
                    Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, item.className,
                        "Variant classname cannot be empty", i);
                    continue;
                }
                const auto found = classIndex.find(key);
                if (found != classIndex.end() && !IEquals(found->second.filename, cat.filename)) {
                    Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, variant,
                        "Variant is a ClassName in a different file: " + found->second.filename, i);
                }
                if (!key.empty()) {
                    allClassNames.insert(key);
                }
            }
        }
    }

    for (const auto& cat : snap.markets) {
        for (int i = 0; i < static_cast<int>(cat.items.size()); ++i) {
            const auto& item = cat.items[static_cast<std::size_t>(i)];
            for (const auto& att : item.spawnAttachments) {
                const auto key = ToLowerAscii(Trim(att));
                if (key.empty()) {
                    Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, item.className,
                        "Attachment classname cannot be empty", i);
                    continue;
                }
                if (!allClassNames.contains(key)) {
                    Add(issues, ValidationIssue::Severity::Error, FileKind::Market, cat.filename, att,
                        "Attachment is not a ClassName or Variant in any Market file", i);
                }
            }
        }
    }

    for (const auto& trader : snap.traders) {
        if (Trim(trader.displayName).empty()) {
            Add(issues, ValidationIssue::Severity::Error, FileKind::Trader, trader.filename, "DisplayName",
                "DisplayName is required");
        }
        if (trader.minRequiredReputation > trader.maxRequiredReputation) {
            Add(issues, ValidationIssue::Severity::Error, FileKind::Trader, trader.filename, "MinRequiredReputation",
                "MinRequiredReputation cannot exceed MaxRequiredReputation");
        }
        for (int i = 0; i < static_cast<int>(trader.categories.size()); ++i) {
            const auto& cat = trader.categories[static_cast<std::size_t>(i)];
            if (Trim(cat.fileStem).empty()) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Trader, trader.filename, "Categories",
                    "Category reference cannot be empty", i);
                continue;
            }
            if (cat.mode < 0 || cat.mode > 3) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Trader, trader.filename, cat.fileStem,
                    "Category mode must be 0, 1, 2, or 3", i);
            }
            if (!marketStems.contains(ToLowerAscii(cat.fileStem))) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Trader, trader.filename, cat.fileStem,
                    "Category does not match any Market filename", i);
            }
        }
        std::unordered_set<std::string> itemKeys;
        for (int i = 0; i < static_cast<int>(trader.items.size()); ++i) {
            const auto& [name, mode] = trader.items[static_cast<std::size_t>(i)];
            const auto key = ToLowerAscii(Trim(name));
            if (key.empty()) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Trader, trader.filename, "Items",
                    "Trader item classname cannot be empty", i);
                continue;
            }
            if (!itemKeys.insert(key).second) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Trader, trader.filename, name,
                    "Duplicate classname in trader Items map", i);
            }
            if (mode < 0 || mode > 3) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Trader, trader.filename, name,
                    "Item mode must be 0, 1, 2, or 3", i);
            }
        }
    }

    for (const auto& zone : snap.zones) {
        if (Trim(zone.displayName).empty()) {
            Add(issues, ValidationIssue::Severity::Error, FileKind::TraderZone, zone.filename, "m_DisplayName",
                "m_DisplayName is required");
        }
        if (zone.radius <= 0) {
            Add(issues, ValidationIssue::Severity::Error, FileKind::TraderZone, zone.filename, "Radius",
                "Radius must be greater than 0");
        }
        std::unordered_set<std::string> stockKeys;
        for (int i = 0; i < static_cast<int>(zone.stock.size()); ++i) {
            const auto& [name, count] = zone.stock[static_cast<std::size_t>(i)];
            const auto key = ToLowerAscii(Trim(name));
            if (key.empty()) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::TraderZone, zone.filename, "Stock",
                    "Stock classname cannot be empty", i);
                continue;
            }
            if (!stockKeys.insert(key).second) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::TraderZone, zone.filename, name,
                    "Duplicate classname in zone Stock map", i);
            }
            if (count < 0) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::TraderZone, zone.filename, name,
                    "Stock cannot be negative", i);
            }
            if (!allClassNames.contains(key)) {
                Add(issues, ValidationIssue::Severity::Warning, FileKind::TraderZone, zone.filename, name,
                    "Stock classname is not present in any Market file", i);
            }
        }
    }

    std::unordered_map<std::string, std::string> typeOwners;
    for (const auto& file : snap.typesFiles) {
        std::unordered_set<std::string> inFile;
        for (int i = 0; i < static_cast<int>(file.types.size()); ++i) {
            const auto& type = file.types[static_cast<std::size_t>(i)];
            const auto name = Trim(type.name);
            const auto key = ToLowerAscii(name);
            if (key.empty()) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Types, file.relPath, "Name",
                    "Type classname cannot be empty", i);
                continue;
            }
            if (key.rfind("zmbf", 0) == 0 || key.rfind("zmbm", 0) == 0) {
                continue;
            }
            if (!inFile.insert(key).second) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Types, file.relPath, name,
                    "Duplicate classname in this types file", i);
            }
            if (auto it = typeOwners.find(key); it != typeOwners.end()) {
                Add(issues, ValidationIssue::Severity::Warning, FileKind::Types, file.relPath, name,
                    "Classname also appears in " + it->second, i);
            } else {
                typeOwners.emplace(key, file.relPath);
            }
            if (type.nominal < 0 || type.min < 0 || type.lifetime < 0 || type.restock < 0 || type.cost < 0) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Types, file.relPath, name,
                    "Counts cannot be negative", i);
            }
            if (type.min > type.nominal) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Types, file.relPath, name,
                    "Min must be less than or equal to nominal", i);
            }
            const auto quantOk = [](int value) { return value == -1 || (value >= 0 && value <= 100); };
            if (!quantOk(type.quantMin) || !quantOk(type.quantMax)) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Types, file.relPath, name,
                    "Quant min/max must be -1 or 0-100", i);
            }
            if (type.quantMin > type.quantMax && type.quantMin != -1 && type.quantMax != -1) {
                Add(issues, ValidationIssue::Severity::Error, FileKind::Types, file.relPath, name,
                    "Quant min must be less than or equal to quant max", i);
            }
            if (type.nominal > 0 && type.lifetime == 0) {
                Add(issues, ValidationIssue::Severity::Warning, FileKind::Types, file.relPath, name,
                    "Lifetime is 0; items will despawn immediately", i);
            }
        }
    }

    return issues;
}

}  // namespace edity
