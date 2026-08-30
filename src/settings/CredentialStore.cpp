#include "settings/CredentialStore.h"

#include "app/Utf8.h"

#include <windows.h>
#include <wincred.h>

namespace edity {
namespace {

std::wstring Target(const std::string& profileId) {
    return L"EDITY/ftp/" + Utf8ToWide(profileId);
}

}  // namespace

bool CredentialStore::SavePassword(const std::string& profileId, const std::string& password) const {
    if (profileId.empty()) {
        return false;
    }
    if (password.empty()) {
        return true;
    }
    const auto target = Target(profileId);
    const auto blob = Utf8ToWide(password);
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size() * sizeof(wchar_t));
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(blob.c_str()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR>(L"EDITY");
    return CredWriteW(&cred, 0) == TRUE;
}

std::optional<std::string> CredentialStore::LoadPassword(const std::string& profileId) const {
    PCREDENTIALW cred = nullptr;
    const auto target = Target(profileId);
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred) {
        return std::nullopt;
    }
    std::wstring secret;
    if (cred->CredentialBlob && cred->CredentialBlobSize > 0) {
        secret.assign(reinterpret_cast<wchar_t*>(cred->CredentialBlob), cred->CredentialBlobSize / sizeof(wchar_t));
    }
    CredFree(cred);
    if (secret.empty()) {
        return std::nullopt;
    }
    return WideToUtf8(secret);
}

void CredentialStore::DeletePassword(const std::string& profileId) const {
    const auto target = Target(profileId);
    CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
}

bool CredentialStore::HasPassword(const std::string& profileId) const {
    return LoadPassword(profileId).has_value();
}

}  // namespace edity
