#pragma once

#include <optional>
#include <string>

namespace edity {

class CredentialStore {
public:
    bool SavePassword(const std::string& profileId, const std::string& password) const;
    std::optional<std::string> LoadPassword(const std::string& profileId) const;
    void DeletePassword(const std::string& profileId) const;
    bool HasPassword(const std::string& profileId) const;
};

}  // namespace edity
