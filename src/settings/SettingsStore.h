#pragma once

#include "market/Models.h"

#include <optional>
#include <vector>

namespace edity {

class SettingsStore {
public:
    SettingsStore();

    const std::vector<ConnectionProfile>& Profiles() const { return profiles_; }
    std::vector<ConnectionProfile> ProfilesForUi() const { return profiles_; }
    std::optional<ConnectionProfile> Find(const std::string& id) const;
    ConnectionProfile Upsert(ConnectionProfile profile);
    bool Remove(const std::string& id);
    void Reload();
    void Save() const;

private:
    std::vector<ConnectionProfile> profiles_;
};

}  // namespace edity
