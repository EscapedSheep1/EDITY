#include "bridge/AppBridge.h"

#include "app/Utf8.h"
#include "market/JsonIO.h"
#include "types/TypesCatalog.h"

#include <filesystem>
#include <stdexcept>
#include <thread>

namespace edity {

struct PostedJson {
    std::wstring json;
};

namespace {

nlohmann::json RequireWorkspace(Workspace* workspace) {
    if (!workspace || !workspace->IsLoaded()) {
        throw std::runtime_error("Connect to a server first");
    }
    return {};
}

}  // namespace

AppBridge::AppBridge(HWND hwnd, Microsoft::WRL::ComPtr<ICoreWebView2> webview)
    : hwnd_(hwnd), webview_(std::move(webview)) {}

void AppBridge::Reply(int id, bool ok, nlohmann::json data, const std::string& error) {
    nlohmann::json msg = {
        {"id", id},
        {"ok", ok},
        {"data", std::move(data)},
        {"error", error},
    };
    auto* posted = new PostedJson{Utf8ToWide(msg.dump())};
    PostMessageW(hwnd_, kPostToUi, 0, reinterpret_cast<LPARAM>(posted));
}

void AppBridge::Emit(const std::string& event, nlohmann::json data) {
    nlohmann::json msg = {{"event", event}, {"data", std::move(data)}};
    auto* posted = new PostedJson{Utf8ToWide(msg.dump())};
    PostMessageW(hwnd_, kPostToUi, 0, reinterpret_cast<LPARAM>(posted));
}

void AppBridge::HandleWebMessage(const std::string& json) {
    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(json);
    } catch (...) {
        return;
    }
    const int id = msg.value("id", 0);
    const auto cmd = msg.value("cmd", std::string());
    const auto payload = msg.contains("payload") && msg["payload"].is_object() ? msg["payload"] : nlohmann::json::object();

    if (cmd == "dragWindow") {
        DragWindow();
        Reply(id, true, nlohmann::json::object(), {});
        return;
    }
    if (cmd == "windowMin") {
        Minimize();
        Reply(id, true, nlohmann::json::object(), {});
        return;
    }
    if (cmd == "windowMax") {
        ToggleMaximize();
        Reply(id, true, nlohmann::json::object(), {});
        return;
    }
    if (cmd == "windowClose") {
        Close();
        Reply(id, true, nlohmann::json::object(), {});
        return;
    }

    std::thread([this, id, cmd, payload] {
        try {
            const auto result = Dispatch(cmd, payload);
            Reply(id, true, result, {});
        } catch (const std::exception& ex) {
            Reply(id, false, nlohmann::json::object(), ex.what());
        }
    }).detach();
}

nlohmann::json AppBridge::ProfilesJson() const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& profile : settings_.Profiles()) {
        nlohmann::json row = profile;
        row["hasPassword"] = credentials_.HasPassword(profile.id);
        list.push_back(std::move(row));
    }
    return {{"profiles", list}};
}

TransferClient::Config AppBridge::MakeConfig(const ConnectionProfile& profile, const std::string& password) const {
    TransferClient::Config config;
    config.protocol = profile.protocol;
    config.host = profile.host;
    config.port = profile.port;
    config.username = profile.username;
    config.password = password;
    config.passive = profile.passive;
    return config;
}

std::string AppBridge::ResolvePassword(const ConnectionProfile& profile, const nlohmann::json& payload) const {
    if (payload.contains("password") && payload["password"].is_string()) {
        const auto supplied = payload["password"].get<std::string>();
        if (!supplied.empty()) {
            return supplied;
        }
    }
    if (const auto stored = credentials_.LoadPassword(profile.id)) {
        return *stored;
    }
    throw std::runtime_error("Password is required");
}

nlohmann::json AppBridge::Dispatch(const std::string& cmd, const nlohmann::json& payload) {
    std::lock_guard lock(mutex_);

    if (cmd == "listProfiles") {
        return ProfilesJson();
    }
    if (cmd == "saveProfile") {
        ConnectionProfile profile;
        if (payload.contains("profile")) {
            profile = payload["profile"].get<ConnectionProfile>();
        } else {
            profile = payload.get<ConnectionProfile>();
        }
        if (Trim(profile.name).empty() || Trim(profile.host).empty()) {
            throw std::runtime_error("Profile name and host are required");
        }
        profile = settings_.Upsert(profile);
        if (payload.contains("password") && payload["password"].is_string()) {
            const auto password = payload["password"].get<std::string>();
            if (!password.empty()) {
                if (!credentials_.SavePassword(profile.id, password)) {
                    throw std::runtime_error("Could not save password to Windows Credential Manager");
                }
            }
        }
        auto json = ProfilesJson();
        json["profile"] = profile;
        json["profile"]["hasPassword"] = credentials_.HasPassword(profile.id);
        return json;
    }
    if (cmd == "deleteProfile") {
        const auto id = payload.value("id", std::string());
        settings_.Remove(id);
        credentials_.DeletePassword(id);
        if (workspace_ && workspace_->Profile().id == id) {
            workspace_.reset();
        }
        return ProfilesJson();
    }
    if (cmd == "testConnection") {
        ConnectionProfile profile;
        if (payload.contains("profile")) {
            profile = payload["profile"].get<ConnectionProfile>();
        } else if (payload.contains("id")) {
            auto found = settings_.Find(payload["id"].get<std::string>());
            if (!found) {
                throw std::runtime_error("Unknown profile");
            }
            profile = *found;
        } else {
            throw std::runtime_error("Profile is required");
        }
        const auto password = ResolvePassword(profile, payload);
        TransferClient client(MakeConfig(profile, password));
        client.SetProgress([this](const TransferProgress& p) {
            Emit("progress", {{"message", p.message}, {"percent", p.percent}});
        });
        client.TestConnection();
        return {{"ok", true}};
    }
    if (cmd == "browseRemote") {
        ConnectionProfile profile;
        if (payload.contains("profile")) {
            profile = payload["profile"].get<ConnectionProfile>();
        } else if (payload.contains("id")) {
            auto found = settings_.Find(payload["id"].get<std::string>());
            if (!found) {
                throw std::runtime_error("Unknown profile");
            }
            profile = *found;
        } else {
            throw std::runtime_error("Profile is required");
        }
        if (Trim(profile.host).empty()) {
            throw std::runtime_error("Host is required before browsing");
        }
        const auto password = ResolvePassword(profile, payload);
        TransferClient client(MakeConfig(profile, password));
        client.SetProgress([this](const TransferProgress& p) {
            Emit("progress", {{"message", p.message}, {"percent", p.percent}});
        });
        std::string path = payload.value("path", std::string());
        if (path == "/") {
            path.clear();
        }
        if (path.empty() && profile.protocol == Protocol::Sftp) {
            path = ".";
        }
        const auto entries = client.ListDirectory(path);
        nlohmann::json list = nlohmann::json::array();
        nlohmann::json suggestions = nlohmann::json::object();
        int jsonCount = 0;
        for (const auto& entry : entries) {
            list.push_back({
                {"name", entry.name},
                {"path", entry.path},
                {"isDir", entry.isDir},
                {"isJson", entry.isJson},
            });
            if (entry.isJson) {
                ++jsonCount;
            }
            if (entry.isDir) {
                const auto key = ToLowerAscii(entry.name);
                if (key == "market") {
                    suggestions["market"] = entry.path;
                } else if (key == "traders" || key == "trader") {
                    suggestions["traders"] = entry.path;
                } else if (key == "traderzones" || key == "traderzone" || key == "zones") {
                    suggestions["zones"] = entry.path;
                } else if (key == "expansionmod") {
                    suggestions["expansionMod"] = entry.path;
                }
            }
        }
        std::string parent;
        const auto slash = path.find_last_of('/');
        if (slash != std::string::npos) {
            parent = path.substr(0, slash);
        }
        return {
            {"path", path},
            {"parent", parent},
            {"entries", list},
            {"jsonCount", jsonCount},
            {"suggestions", suggestions},
        };
    }
    if (cmd == "connect") {
        const auto id = payload.value("id", payload.value("profileId", std::string()));
        auto found = settings_.Find(id);
        if (!found) {
            throw std::runtime_error("Unknown profile");
        }
        const auto password = ResolvePassword(*found, payload);
        workspace_ = std::make_unique<Workspace>(*found, MakeConfig(*found, password));
        workspace_->SetProgress([this](const TransferProgress& p) {
            Emit("progress", {{"message", p.message}, {"percent", p.percent}});
        });
        workspace_->PullAll();
        return workspace_->ToUiJson();
    }
    if (cmd == "getWorkspace") {
        RequireWorkspace(workspace_.get());
        return workspace_->ToUiJson();
    }
    if (cmd == "saveFile") {
        RequireWorkspace(workspace_.get());
        const auto kind = KindFromName(payload.value("kind", std::string()));
        const auto& body = payload.contains("file") ? payload["file"] : payload;
        if (kind == FileKind::Market) {
            workspace_->SaveMarket(body);
        } else if (kind == FileKind::Trader) {
            workspace_->SaveTrader(body);
        } else if (kind == FileKind::TraderZone) {
            workspace_->SaveZone(body);
        } else if (kind == FileKind::Types) {
            workspace_->SaveTypes(body);
        } else {
            workspace_->SaveZone(body);
        }
        return workspace_->ToUiJson();
    }
    if (cmd == "createFile") {
        RequireWorkspace(workspace_.get());
        const auto kind = KindFromName(payload.value("kind", std::string()));
        const auto filename = payload.value("filename", std::string());
        auto created = workspace_->CreateWorkspaceFile(kind, filename);
        auto json = workspace_->ToUiJson();
        json["created"] = created;
        return json;
    }
    if (cmd == "deleteFile") {
        RequireWorkspace(workspace_.get());
        workspace_->DeleteWorkspaceFile(KindFromName(payload.value("kind", std::string())), payload.value("filename", std::string()));
        return workspace_->ToUiJson();
    }
    if (cmd == "validateAll") {
        RequireWorkspace(workspace_.get());
        return {{"issues", workspace_->Validate()}};
    }
    if (cmd == "confirmUpload") {
        RequireWorkspace(workspace_.get());
        workspace_->SetProgress([this](const TransferProgress& p) {
            Emit("progress", {{"message", p.message}, {"percent", p.percent}});
        });
        return workspace_->ConfirmUpload();
    }
    if (cmd == "listBackups") {
        if (!workspace_) {
            return {{"backups", nlohmann::json::array()}};
        }
        return {{"backups", workspace_->ListBackups()}};
    }
    if (cmd == "disconnect") {
        workspace_.reset();
        return {{"disconnected", true}};
    }
    if (cmd == "getTypesCatalog") {
        if (workspace_ && workspace_->IsLoaded() && !workspace_->Types().types.empty()) {
            return workspace_->Types().ToUi();
        }
        return LoadTypesCatalog().ToUi();
    }
    if (cmd == "pullTypes") {
        RequireWorkspace(workspace_.get());
        workspace_->SetProgress([this](const TransferProgress& p) {
            Emit("progress", {{"message", p.message}, {"percent", p.percent}});
        });
        workspace_->PullTypes();
        return workspace_->ToUiJson();
    }

    throw std::runtime_error("Unknown command: " + cmd);
}

void AppBridge::DragWindow() const {
    ReleaseCapture();
    SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void AppBridge::Minimize() const {
    ShowWindow(hwnd_, SW_MINIMIZE);
}

void AppBridge::ToggleMaximize() const {
    WINDOWPLACEMENT place{};
    place.length = sizeof(place);
    GetWindowPlacement(hwnd_, &place);
    ShowWindow(hwnd_, place.showCmd == SW_MAXIMIZE ? SW_RESTORE : SW_MAXIMIZE);
}

void AppBridge::Close() const {
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void DeliverPostedJson(ICoreWebView2* webview, LPARAM lParam) {
    auto* posted = reinterpret_cast<PostedJson*>(lParam);
    if (webview && posted) {
        webview->PostWebMessageAsJson(posted->json.c_str());
    }
    delete posted;
}

}  // namespace edity
