#include "settings/SettingsStore.h"

#include "app/Paths.h"
#include "app/Utf8.h"
#include "app/Util.h"
#include "market/JsonIO.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace edity {

SettingsStore::SettingsStore() {
    Reload();
}

void SettingsStore::Reload() {
    profiles_.clear();
    const auto path = SettingsPath();
    if (!std::filesystem::exists(path)) {
        return;
    }
    try {
        const auto raw = ReadFileUtf8(path);
        const auto json = nlohmann::json::parse(raw);
        if (json.contains("profiles") && json["profiles"].is_array()) {
            for (const auto& item : json["profiles"]) {
                ConnectionProfile profile = item.get<ConnectionProfile>();
                if (!profile.id.empty()) {
                    profiles_.push_back(std::move(profile));
                }
            }
        }
    } catch (...) {
        profiles_.clear();
    }
}

void SettingsStore::Save() const {
    nlohmann::json json;
    json["profiles"] = nlohmann::json::array();
    for (const auto& profile : profiles_) {
        nlohmann::json row = profile;
        json["profiles"].push_back(row);
    }
    WriteFileAtomic(SettingsPath(), json.dump(4));
}

std::optional<ConnectionProfile> SettingsStore::Find(const std::string& id) const {
    for (const auto& profile : profiles_) {
        if (profile.id == id) {
            return profile;
        }
    }
    return std::nullopt;
}

ConnectionProfile SettingsStore::Upsert(ConnectionProfile profile) {
    if (profile.id.empty()) {
        profile.id = NewUuid();
    }
    if (profile.port <= 0) {
        profile.port = profile.protocol == Protocol::Sftp ? 22 : 21;
    }
    for (auto& existing : profiles_) {
        if (existing.id == profile.id) {
            existing = profile;
            Save();
            return existing;
        }
    }
    profiles_.push_back(profile);
    Save();
    return profile;
}

bool SettingsStore::Remove(const std::string& id) {
    const auto before = profiles_.size();
    profiles_.erase(std::remove_if(profiles_.begin(), profiles_.end(),
                                   [&](const ConnectionProfile& p) { return p.id == id; }),
                    profiles_.end());
    if (profiles_.size() != before) {
        Save();
        return true;
    }
    return false;
}

}  // namespace edity
