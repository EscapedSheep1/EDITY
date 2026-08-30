#include "loadout/LoadoutIO.h"

namespace edity {
namespace {

std::string JsonString(const nlohmann::json& j, const char* key, const std::string& fallback = {}) {
    if (!j.contains(key) || j[key].is_null()) {
        return fallback;
    }
    if (j[key].is_string()) {
        return j[key].get<std::string>();
    }
    if (j[key].is_number()) {
        return std::to_string(j[key].get<double>());
    }
    return fallback;
}

double JsonDouble(const nlohmann::json& j, const char* key, double fallback) {
    if (!j.contains(key) || j[key].is_null()) {
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

LoadoutNode ParseNode(const nlohmann::json& j);

std::vector<LoadoutHealth> ParseHealth(const nlohmann::json& j) {
    std::vector<LoadoutHealth> out;
    if (!j.contains("Health") || !j["Health"].is_array() || j["Health"].empty()) {
        out.push_back({});
        return out;
    }
    for (const auto& row : j["Health"]) {
        if (!row.is_object()) {
            continue;
        }
        LoadoutHealth health;
        health.min = JsonDouble(row, "Min", 1.0);
        health.max = JsonDouble(row, "Max", 1.0);
        health.zone = JsonString(row, "Zone");
        out.push_back(health);
    }
    if (out.empty()) {
        out.push_back({});
    }
    return out;
}

void ParseQuantity(const nlohmann::json& j, LoadoutNode& node) {
    if (j.contains("Quantity") && j["Quantity"].is_object()) {
        node.quantityMin = JsonDouble(j["Quantity"], "Min", 0.0);
        node.quantityMax = JsonDouble(j["Quantity"], "Max", 0.0);
        return;
    }
    if (j.contains("Quantity") && j["Quantity"].is_number()) {
        const auto qty = j["Quantity"].get<double>();
        node.quantityMin = qty;
        node.quantityMax = qty;
    }
}

void AddAttachmentItems(LoadoutNode& node, const std::string& slot, const nlohmann::json& items) {
    if (!items.is_array()) {
        return;
    }
    std::vector<LoadoutNode> parsed;
    for (const auto& item : items) {
        if (item.is_string()) {
            parsed.push_back(DefaultLoadoutNode(item.get<std::string>()));
        } else if (item.is_object()) {
            parsed.push_back(ParseNode(item));
        }
    }
    if (parsed.empty()) {
        node.attachments.push_back({slot, {}});
        return;
    }
    node.attachments.push_back({slot, std::move(parsed)});
}

LoadoutNode ParseNode(const nlohmann::json& j) {
    LoadoutNode node;
    if (!j.is_object()) {
        return node;
    }
    node.className = JsonString(j, "ClassName");
    node.includeFile = JsonString(j, "Include");
    node.chance = JsonDouble(j, "Chance", 1.0);
    ParseQuantity(j, node);
    node.health = ParseHealth(j);

    if (j.contains("InventoryAttachments") && j["InventoryAttachments"].is_array()) {
        for (const auto& slot : j["InventoryAttachments"]) {
            if (!slot.is_object()) {
                continue;
            }
            AddAttachmentItems(node, JsonString(slot, "SlotName"),
                               slot.contains("Items") ? slot["Items"] : nlohmann::json::array());
        }
    } else if (j.contains("Attachments") && j["Attachments"].is_array()) {
        AddAttachmentItems(node, "", j["Attachments"]);
    }

    const auto* cargo = j.contains("InventoryCargo") && j["InventoryCargo"].is_array() ? &j["InventoryCargo"]
                        : j.contains("Inventory") && j["Inventory"].is_array()         ? &j["Inventory"]
                                                                                      : nullptr;
    if (cargo) {
        for (const auto& item : *cargo) {
            if (item.is_string()) {
                node.cargo.push_back(DefaultLoadoutNode(item.get<std::string>()));
            } else if (item.is_object()) {
                node.cargo.push_back(ParseNode(item));
            }
        }
    }

    if (j.contains("Sets") && j["Sets"].is_array()) {
        for (const auto& set : j["Sets"]) {
            if (set.is_object()) {
                node.sets.push_back(ParseNode(set));
            }
        }
    }

    if (j.contains("ConstructionPartsBuilt") && j["ConstructionPartsBuilt"].is_array()) {
        for (const auto& part : j["ConstructionPartsBuilt"]) {
            if (part.is_string()) {
                node.constructionParts.push_back(part.get<std::string>());
            }
        }
    }
    return node;
}

nlohmann::json NodeToJson(const LoadoutNode& node) {
    nlohmann::json health = nlohmann::json::array();
    for (const auto& row : node.health) {
        health.push_back({{"Min", row.min}, {"Max", row.max}, {"Zone", row.zone}});
    }
    if (health.empty()) {
        health.push_back({{"Min", 1.0}, {"Max", 1.0}, {"Zone", ""}});
    }

    nlohmann::json attachments = nlohmann::json::array();
    for (const auto& [slot, items] : node.attachments) {
        nlohmann::json list = nlohmann::json::array();
        for (const auto& item : items) {
            list.push_back(NodeToJson(item));
        }
        attachments.push_back({{"SlotName", slot}, {"Items", list}});
    }

    nlohmann::json cargo = nlohmann::json::array();
    for (const auto& item : node.cargo) {
        cargo.push_back(NodeToJson(item));
    }
    nlohmann::json sets = nlohmann::json::array();
    for (const auto& item : node.sets) {
        sets.push_back(NodeToJson(item));
    }

    return {
        {"ClassName", node.className},
        {"Include", node.includeFile},
        {"Chance", node.chance},
        {"Quantity", {{"Min", node.quantityMin}, {"Max", node.quantityMax}}},
        {"Health", health},
        {"InventoryAttachments", attachments},
        {"InventoryCargo", cargo},
        {"ConstructionPartsBuilt", node.constructionParts},
        {"Sets", sets},
    };
}

nlohmann::json NodeToUi(const LoadoutNode& node) {
    nlohmann::json attachments = nlohmann::json::array();
    for (const auto& [slot, items] : node.attachments) {
        nlohmann::json list = nlohmann::json::array();
        for (const auto& item : items) {
            list.push_back(NodeToUi(item));
        }
        attachments.push_back({{"slotName", slot}, {"items", list}});
    }
    nlohmann::json cargo = nlohmann::json::array();
    for (const auto& item : node.cargo) {
        cargo.push_back(NodeToUi(item));
    }
    nlohmann::json sets = nlohmann::json::array();
    for (const auto& item : node.sets) {
        sets.push_back(NodeToUi(item));
    }
    nlohmann::json health = nlohmann::json::array();
    for (const auto& row : node.health) {
        health.push_back({{"min", row.min}, {"max", row.max}, {"zone", row.zone}});
    }
    return {
        {"className", node.className},
        {"includeFile", node.includeFile},
        {"chance", node.chance},
        {"quantityMin", node.quantityMin},
        {"quantityMax", node.quantityMax},
        {"health", health},
        {"attachments", attachments},
        {"cargo", cargo},
        {"sets", sets},
        {"constructionParts", node.constructionParts},
    };
}

LoadoutNode NodeFromUi(const nlohmann::json& j) {
    LoadoutNode node = DefaultLoadoutNode(j.value("className", std::string()));
    if (!j.is_object()) {
        return node;
    }
    node.includeFile = j.value("includeFile", std::string());
    node.chance = j.value("chance", 1.0);
    node.quantityMin = j.value("quantityMin", 0.0);
    node.quantityMax = j.value("quantityMax", 0.0);
    node.health.clear();
    if (j.contains("health") && j["health"].is_array()) {
        for (const auto& row : j["health"]) {
            LoadoutHealth health;
            health.min = row.value("min", 1.0);
            health.max = row.value("max", 1.0);
            health.zone = row.value("zone", std::string());
            node.health.push_back(health);
        }
    }
    if (node.health.empty()) {
        node.health.push_back({});
    }
    node.attachments.clear();
    if (j.contains("attachments") && j["attachments"].is_array()) {
        for (const auto& slot : j["attachments"]) {
            std::vector<LoadoutNode> items;
            if (slot.contains("items") && slot["items"].is_array()) {
                for (const auto& item : slot["items"]) {
                    items.push_back(NodeFromUi(item));
                }
            }
            node.attachments.push_back({slot.value("slotName", std::string()), std::move(items)});
        }
    }
    node.cargo.clear();
    if (j.contains("cargo") && j["cargo"].is_array()) {
        for (const auto& item : j["cargo"]) {
            node.cargo.push_back(NodeFromUi(item));
        }
    }
    node.sets.clear();
    if (j.contains("sets") && j["sets"].is_array()) {
        for (const auto& item : j["sets"]) {
            node.sets.push_back(NodeFromUi(item));
        }
    }
    node.constructionParts.clear();
    if (j.contains("constructionParts") && j["constructionParts"].is_array()) {
        for (const auto& part : j["constructionParts"]) {
            if (part.is_string()) {
                node.constructionParts.push_back(part.get<std::string>());
            }
        }
    }
    return node;
}

}  // namespace

LoadoutNode DefaultLoadoutNode(const std::string& className) {
    LoadoutNode node;
    node.className = className;
    node.health.push_back({});
    return node;
}

LoadoutFile DefaultLoadout(std::string_view filename) {
    LoadoutFile file;
    file.filename = std::string(filename);
    file.root = DefaultLoadoutNode("SurvivorM_Mirek");
    return file;
}

ParseLoadoutResult ParseLoadout(std::string_view filename, std::string_view raw) {
    ParseLoadoutResult result;
    result.file.filename = std::string(filename);
    try {
        auto json = nlohmann::json::parse(raw.begin(), raw.end());
        if (json.is_array() && !json.empty()) {
            json = json.front();
        }
        if (!json.is_object()) {
            result.error = "Loadout file must be a JSON object";
            return result;
        }
        if (json.contains("StartingGear") || json.contains("StartingClothing")) {
            result.error = "This looks like SpawnSettings.json, not an Expansion Loadouts file";
            return result;
        }
        result.file.root = ParseNode(json);
        result.ok = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    }
}

std::string SerializeLoadout(const LoadoutFile& file) {
    return NodeToJson(file.root).dump(4);
}

nlohmann::json LoadoutToUi(const LoadoutFile& file) {
    return {
        {"filename", file.filename},
        {"root", NodeToUi(file.root)},
    };
}

LoadoutFile LoadoutFromUi(const nlohmann::json& body) {
    LoadoutFile file;
    file.filename = body.value("filename", std::string());
    if (body.contains("root")) {
        file.root = NodeFromUi(body["root"]);
    } else {
        file.root = DefaultLoadoutNode("SurvivorM_Mirek");
    }
    return file;
}

}  // namespace edity
