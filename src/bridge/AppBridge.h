#pragma once

#include <windows.h>
#include <objbase.h>
#include <wrl.h>
#include <WebView2.h>

#include "settings/CredentialStore.h"
#include "settings/SettingsStore.h"
#include "workspace/Workspace.h"

#include <memory>
#include <mutex>
#include <string>

namespace edity {

constexpr UINT kPostToUi = WM_APP + 41;

void DeliverPostedJson(ICoreWebView2* webview, LPARAM lParam);

class AppBridge {
public:
    AppBridge(HWND hwnd, Microsoft::WRL::ComPtr<ICoreWebView2> webview);

    void HandleWebMessage(const std::string& json);
    void DragWindow() const;
    void Minimize() const;
    void ToggleMaximize() const;
    void Close() const;

private:
    HWND hwnd_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    SettingsStore settings_;
    CredentialStore credentials_;
    std::unique_ptr<Workspace> workspace_;
    std::mutex mutex_;

    void Reply(int id, bool ok, nlohmann::json data, const std::string& error);
    void Emit(const std::string& event, nlohmann::json data);
    nlohmann::json Dispatch(const std::string& cmd, const nlohmann::json& payload);
    nlohmann::json ProfilesJson() const;
    TransferClient::Config MakeConfig(const ConnectionProfile& profile, const std::string& password) const;
    std::string ResolvePassword(const ConnectionProfile& profile, const nlohmann::json& payload) const;
};

}  // namespace edity
