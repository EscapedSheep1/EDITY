#pragma once

#include "ftp/TransferClient.h"
#include "market/Models.h"
#include "market/Validator.h"
#include "types/TypesCatalog.h"
#include "types/TypesXmlIO.h"
#include "loadout/LoadoutIO.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace edity {

class Workspace {
public:
    Workspace(ConnectionProfile profile, TransferClient::Config transfer);

    void SetProgress(TransferClient::ProgressFn fn);
    void PullAll();
    void PullTypes();
    nlohmann::json ToUiJson() const;
    const TypesCatalog& Types() const { return typesCatalog_; }
    std::vector<ValidationIssue> Validate() const;
    WorkspaceSnapshot Snapshot() const;

    void SaveMarket(const nlohmann::json& body);
    void SaveTrader(const nlohmann::json& body);
    void SaveZone(const nlohmann::json& body);
    void SaveTypes(const nlohmann::json& body);
    void SaveLoadout(const nlohmann::json& body);
    nlohmann::json CreateWorkspaceFile(FileKind kind, const std::string& filename);
    void DeleteWorkspaceFile(FileKind kind, const std::string& filename);
    nlohmann::json ConfirmUpload();
    std::vector<std::string> ListBackups() const;

    const ConnectionProfile& Profile() const { return profile_; }
    bool IsLoaded() const { return loaded_; }

private:
    ConnectionProfile profile_;
    TransferClient transfer_;
    std::filesystem::path root_;
    bool loaded_ = false;

    std::unordered_map<std::string, MarketCategory> markets_;
    std::unordered_map<std::string, TraderFile> traders_;
    std::unordered_map<std::string, TraderZone> zones_;
    std::vector<QuarantineFile> quarantine_;
    std::unordered_set<std::string> dirty_;
    std::unordered_set<std::string> pendingDeletes_;
    std::vector<ValidationIssue> extraIssues_;
    TypesCatalog typesCatalog_;
    std::unordered_map<std::string, TypesDocument> typesFiles_;
    std::string economyCoreXml_;
    std::unordered_map<std::string, LoadoutFile> loadouts_;

    std::string RemotePath(FileKind kind) const;
    std::filesystem::path LocalPath(FileKind kind, const std::string& filename) const;
    void ClearLocal();
    void LoadLocal();
    void Ingest(FileKind kind, const std::string& filename, const std::string& raw);
    void Persist(FileKind kind, const std::string& filename, const std::string& contents);
    int SiblingVersion(FileKind kind) const;
    void MarkDirty(FileKind kind, const std::string& filename);
    void IngestTypesLabeled(const std::vector<std::pair<std::string, std::string>>& labeled);
    void RebuildTypesCatalog();
    std::string ResolveTypesName(const std::string& incoming) const;
};

}  // namespace edity
