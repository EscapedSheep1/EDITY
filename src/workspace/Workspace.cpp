#include "workspace/Workspace.h"

#include "app/Paths.h"
#include "app/Utf8.h"
#include "app/Util.h"
#include "backup/ZipArchive.h"
#include "market/JsonIO.h"
#include "types/TypesCatalog.h"
#include "types/TypesXmlIO.h"

#include <algorithm>
#include <future>
#include <stdexcept>
#include <string_view>

namespace edity {
namespace {

std::string FileKey(const std::string& filename) {
    return ToLowerAscii(filename);
}

std::filesystem::path TypesLocalPath(const std::filesystem::path& root, const std::string& rel) {
    auto out = KindDir(root, "Types");
    std::string part;
    const auto norm = NormalizeRemotePath(rel);
    for (char c : norm) {
        if (c == '/') {
            if (!part.empty() && part != "." && part != "..") {
                out /= Utf8ToWide(part);
            }
            part.clear();
        } else {
            part.push_back(c);
        }
    }
    if (part.empty() || part == "." || part == "..") {
        throw std::runtime_error("Invalid types path: " + rel);
    }
    return out / Utf8ToWide(part);
}

void PersistTypesFile(const std::filesystem::path& root, const std::string& rel, const std::string& contents) {
    const auto path = TypesLocalPath(root, rel);
    EnsureDirectory(path.parent_path());
    WriteFileAtomic(path, contents);
}

struct FoundEconomyCore {
    std::string mission;
    std::string xml;
};

FoundEconomyCore FindEconomyCore(TransferClient& transfer, const std::string& zonesPath) {
    if (Trim(zonesPath).empty()) {
        throw std::runtime_error(
            "Set the TraderZones path so EDITY can find the mission folder and cfgeconomycore.xml.");
    }
    std::vector<std::string> candidates;
    const auto guessed = GuessMissionRoot(zonesPath);
    if (!Trim(guessed).empty()) {
        candidates.push_back(guessed);
    }
    auto dir = NormalizeRemotePath(zonesPath);
    for (int i = 0; i < 8; ++i) {
        if (std::find(candidates.begin(), candidates.end(), dir) == candidates.end()) {
            candidates.push_back(dir);
        }
        const auto parent = ParentRemotePath(dir);
        if (parent == dir) {
            break;
        }
        dir = parent;
    }

    std::string lastError;
    for (const auto& cand : candidates) {
        try {
            auto raw = transfer.DownloadFile(cand, "cfgeconomycore.xml");
            if (raw.find("<ce") != std::string::npos || raw.find("economycore") != std::string::npos) {
                FoundEconomyCore found;
                found.mission = cand;
                found.xml = std::move(raw);
                return found;
            }
        } catch (const std::exception& ex) {
            lastError = ex.what();
        }
        try {
            for (const auto& entry : transfer.ListDirectory(cand)) {
                if (!entry.isDir && IEquals(entry.name, "cfgeconomycore.xml")) {
                    FoundEconomyCore found;
                    found.mission = cand;
                    found.xml = transfer.DownloadFile(cand, entry.name);
                    return found;
                }
            }
        } catch (const std::exception& ex) {
            lastError = ex.what();
        }
    }
    throw std::runtime_error(
        "Could not find cfgeconomycore.xml in the mission folder. Check the TraderZones path." +
        (lastError.empty() ? std::string() : " " + lastError));
}

template <class Map>
std::string ResolveExistingName(const Map& files, const std::string& incoming) {
    const auto rawKey = FileKey(incoming);
    if (auto it = files.find(rawKey); it != files.end()) {
        return it->second.filename;
    }
    const auto cleaned = JsonFileName(incoming);
    if (auto it = files.find(FileKey(cleaned)); it != files.end()) {
        return it->second.filename;
    }
    return cleaned;
}

}  // namespace

Workspace::Workspace(ConnectionProfile profile, TransferClient::Config transfer)
    : profile_(std::move(profile)), transfer_(std::move(transfer)), root_(WorkspaceDir(profile_.id)) {
    EnsureDirectory(KindDir(root_, "Market"));
    EnsureDirectory(KindDir(root_, "Traders"));
    EnsureDirectory(KindDir(root_, "TraderZones"));
    EnsureDirectory(KindDir(root_, "Types"));
}

void Workspace::SetProgress(TransferClient::ProgressFn fn) {
    transfer_.SetProgress(std::move(fn));
}

std::string Workspace::RemotePath(FileKind kind) const {
    switch (kind) {
        case FileKind::Trader:
            return profile_.tradersPath;
        case FileKind::TraderZone:
            return profile_.zonesPath;
        case FileKind::Market:
        default:
            return profile_.marketPath;
    }
}

std::filesystem::path Workspace::LocalPath(FileKind kind, const std::string& filename) const {
    return KindDir(root_, KindName(kind)) / Utf8ToWide(filename);
}

void Workspace::MarkDirty(FileKind kind, const std::string& filename) {
    dirty_.insert(RelKey(KindName(kind), filename));
}

void Workspace::ClearLocal() {
    for (const char* kind : {"Market", "Traders", "TraderZones", "Types"}) {
        const auto dir = KindDir(root_, kind);
        std::error_code ec;
        for (const auto& item : std::filesystem::directory_iterator(dir, ec)) {
            std::filesystem::remove_all(item.path(), ec);
        }
    }
    markets_.clear();
    traders_.clear();
    zones_.clear();
    typesCatalog_ = {};
    typesFiles_.clear();
    economyCoreXml_.clear();
    quarantine_.clear();
    extraIssues_.clear();
    dirty_.clear();
    pendingDeletes_.clear();
}

std::string StripBom(std::string raw) {
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    return raw;
}

bool LooksLikeJsonObject(std::string_view raw) {
    const auto start = raw.find_first_not_of(" \t\r\n");
    return start != std::string_view::npos && raw[start] == '{';
}

void Workspace::Ingest(FileKind kind, const std::string& filename, const std::string& incoming) {
    const auto raw = StripBom(incoming);
    if (!LooksLikeJsonObject(raw)) {
        quarantine_.push_back({kind, filename,
                               "Downloaded data is not a JSON object. The remote path is probably a parent folder "
                               "or a directory listing was treated as a file. Browse the server and select the "
                               "Market, Traders, and TraderZones directories."});
        return;
    }
    const auto objectName = kind == FileKind::TraderZone ? "Stock" : "Items";
    if (kind != FileKind::Market) {
        for (const auto& key : DuplicateKeysInObject(raw, objectName)) {
            ValidationIssue issue;
            issue.kind = kind;
            issue.filename = filename;
            issue.field = key;
            issue.message = "Duplicate classname in " + std::string(objectName) + " map";
            extraIssues_.push_back(std::move(issue));
        }
    }
    if (kind == FileKind::Market) {
        auto parsed = ParseMarket(filename, raw);
        if (!parsed.ok) {
            quarantine_.push_back({kind, filename, parsed.error});
            return;
        }
        markets_[FileKey(filename)] = std::move(parsed.market);
        return;
    }
    if (kind == FileKind::Trader) {
        auto parsed = ParseTrader(filename, raw);
        if (!parsed.ok) {
            quarantine_.push_back({kind, filename, parsed.error});
            return;
        }
        traders_[FileKey(filename)] = std::move(parsed.trader);
        return;
    }
    auto parsed = ParseZone(filename, raw);
    if (!parsed.ok) {
        quarantine_.push_back({kind, filename, parsed.error});
        return;
    }
    zones_[FileKey(filename)] = std::move(parsed.zone);
}

void Workspace::Persist(FileKind kind, const std::string& filename, const std::string& contents) {
    WriteFileAtomic(LocalPath(kind, filename), contents);
}

void Workspace::LoadLocal() {
    markets_.clear();
    traders_.clear();
    zones_.clear();
    quarantine_.clear();
    extraIssues_.clear();

    const std::pair<FileKind, const char*> kinds[] = {
        {FileKind::Market, "Market"},
        {FileKind::Trader, "Traders"},
        {FileKind::TraderZone, "TraderZones"},
    };
    for (const auto& [kind, name] : kinds) {
        const auto dir = KindDir(root_, name);
        std::error_code ec;
        for (const auto& item : std::filesystem::directory_iterator(dir, ec)) {
            if (!item.is_regular_file()) {
                continue;
            }
            const auto filename = WideToUtf8(item.path().filename().wstring());
            if (!EndsWithI(filename, ".json")) {
                continue;
            }
            try {
                Ingest(kind, filename, ReadFileUtf8(item.path()));
            } catch (const std::exception& ex) {
                quarantine_.push_back({kind, filename, ex.what()});
            }
        }
    }
}

void Workspace::PullAll() {
    transfer_.Report("Pulling remote files...", 1);
    ClearLocal();

    struct Folder {
        FileKind kind;
        std::string remote;
    };
    const Folder folders[] = {
        {FileKind::Market, profile_.marketPath},
        {FileKind::Trader, profile_.tradersPath},
        {FileKind::TraderZone, profile_.zonesPath},
    };

    struct Listed {
        FileKind kind = FileKind::Market;
        std::string remote;
        std::vector<std::string> files;
        std::string error;
    };

    std::vector<std::future<Listed>> listings;
    for (const auto& folder : folders) {
        if (Trim(folder.remote).empty()) {
            continue;
        }
        listings.push_back(std::async(std::launch::async, [this, folder] {
            Listed listed;
            listed.kind = folder.kind;
            listed.remote = folder.remote;
            try {
                listed.files = transfer_.ListJsonFiles(folder.remote);
            } catch (const std::exception& ex) {
                listed.error = ex.what();
            }
            return listed;
        }));
    }

    std::vector<Listed> listedFolders;
    listedFolders.reserve(listings.size());
    for (auto& listing : listings) {
        listedFolders.push_back(listing.get());
    }

    std::vector<TransferClient::DownloadRequest> requests;
    std::vector<FileKind> requestKinds;
    for (auto& listed : listedFolders) {
        if (!listed.error.empty()) {
            quarantine_.push_back(
                {listed.kind, KindName(listed.kind), "Could not list remote folder: " + listed.error});
            continue;
        }
        for (const auto& filename : listed.files) {
            requests.push_back({listed.remote, filename});
            requestKinds.push_back(listed.kind);
        }
    }

    transfer_.Report("Downloading " + std::to_string(requests.size()) + " JSON files...", 10);
    const auto results = transfer_.DownloadFiles(requests);
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& row = results[i];
        const auto kind = i < requestKinds.size() ? requestKinds[i] : FileKind::Market;
        if (!row.error.empty()) {
            quarantine_.push_back({kind, row.filename, row.error});
            continue;
        }
        try {
            Persist(kind, row.filename, row.contents);
            Ingest(kind, row.filename, row.contents);
        } catch (const std::exception& ex) {
            quarantine_.push_back({kind, row.filename, ex.what()});
        }
    }
    loaded_ = true;
    try {
        PullTypes();
    } catch (const std::exception& ex) {
        typesCatalog_ = {};
        typesCatalog_.error = ex.what();
        SaveTypesCatalog(typesCatalog_);
    }
    transfer_.Report("Workspace ready", 100);
}

void Workspace::PullTypes() {
    transfer_.Report("Finding cfgeconomycore.xml...", 4);
    const auto found = FindEconomyCore(transfer_, profile_.zonesPath);
    PersistTypesFile(root_, "cfgeconomycore.xml", found.xml);
    auto refs = ParseEconomyCore(found.xml);
    EnsureVanillaTypesFile(refs);
    if (refs.empty()) {
        throw std::runtime_error(
            "cfgeconomycore.xml was found, but no types files could be resolved (including db/types.xml).");
    }

    std::vector<TransferClient::DownloadRequest> requests;
    std::vector<std::string> labels;
    std::unordered_set<std::string> seen;
    for (const auto& ref : refs) {
        const auto rel = ref.Relative();
        if (rel.empty() || !seen.insert(ToLowerAscii(rel)).second) {
            continue;
        }
        const auto [dir, file] = SplitRemoteFile(JoinRemotePath(found.mission, rel));
        requests.push_back({dir, file});
        labels.push_back(rel);
    }

    transfer_.Report("Downloading " + std::to_string(requests.size()) + " types files...", 12);
    const auto results = transfer_.DownloadFiles(requests);
    std::vector<std::pair<std::string, std::string>> labeled;
    std::vector<std::string> failed;
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (!results[i].error.empty()) {
            failed.push_back(labels[i] + ": " + results[i].error);
            continue;
        }
        PersistTypesFile(root_, labels[i], results[i].contents);
        labeled.push_back({labels[i], results[i].contents});
    }

    economyCoreXml_ = found.xml;
    IngestTypesLabeled(labeled);
    typesCatalog_ = BuildTypesCatalog(found.mission, labeled);
    if (!failed.empty()) {
        typesCatalog_.error = failed.size() == 1
                                  ? failed.front()
                                  : std::to_string(failed.size()) + " types files failed to download";
    }
    if (typesCatalog_.types.empty()) {
        throw std::runtime_error(typesCatalog_.error.empty()
                                     ? "Types files downloaded but no classnames were parsed."
                                     : typesCatalog_.error);
    }
    SaveTypesCatalog(typesCatalog_);
    transfer_.Report("Types ready (" + std::to_string(typesCatalog_.types.size()) + " classnames)", 100);
}

WorkspaceSnapshot Workspace::Snapshot() const {
    WorkspaceSnapshot snap;
    snap.quarantine = quarantine_;
    for (const auto& [_, cat] : markets_) {
        snap.markets.push_back(cat);
    }
    for (const auto& [_, trader] : traders_) {
        snap.traders.push_back(trader);
    }
    for (const auto& [_, zone] : zones_) {
        snap.zones.push_back(zone);
    }
    for (const auto& [_, file] : typesFiles_) {
        snap.typesFiles.push_back(file);
    }
    return snap;
}

std::vector<ValidationIssue> Workspace::Validate() const {
    auto issues = ValidateWorkspace(Snapshot());
    issues.insert(issues.end(), extraIssues_.begin(), extraIssues_.end());
    return issues;
}

nlohmann::json Workspace::ToUiJson() const {
    nlohmann::json markets = nlohmann::json::array();
    nlohmann::json traders = nlohmann::json::array();
    nlohmann::json zones = nlohmann::json::array();
    nlohmann::json typesFiles = nlohmann::json::array();
    for (const auto& [_, cat] : markets_) {
        markets.push_back(MarketToUi(cat));
    }
    for (const auto& [_, trader] : traders_) {
        traders.push_back(TraderToUi(trader));
    }
    for (const auto& [_, zone] : zones_) {
        zones.push_back(ZoneToUi(zone));
    }
    for (const auto& [_, file] : typesFiles_) {
        typesFiles.push_back(TypesFileToUi(file));
    }
    nlohmann::json dirty = nlohmann::json::array();
    for (const auto& key : dirty_) {
        dirty.push_back(key);
    }
    nlohmann::json deletes = nlohmann::json::array();
    for (const auto& key : pendingDeletes_) {
        deletes.push_back(key);
    }
    return {
        {"profileId", profile_.id},
        {"profileName", profile_.name},
        {"markets", markets},
        {"traders", traders},
        {"zones", zones},
        {"typesFiles", typesFiles},
        {"quarantine", quarantine_},
        {"dirty", dirty},
        {"pendingDeletes", deletes},
        {"issues", Validate()},
        {"types", typesCatalog_.ToUi()},
    };
}

int Workspace::SiblingVersion(FileKind kind) const {
    if (kind == FileKind::Market && !markets_.empty()) {
        return markets_.begin()->second.version;
    }
    if (kind == FileKind::Trader && !traders_.empty()) {
        return traders_.begin()->second.version;
    }
    if (kind == FileKind::TraderZone && !zones_.empty()) {
        return zones_.begin()->second.version;
    }
    return 0;
}

void Workspace::IngestTypesLabeled(const std::vector<std::pair<std::string, std::string>>& labeled) {
    typesFiles_.clear();
    for (const auto& [rel, raw] : labeled) {
        auto parsed = ParseTypesDocument(raw, rel);
        if (!parsed.ok) {
            quarantine_.push_back({FileKind::Types, rel, parsed.error});
            continue;
        }
        parsed.doc.relPath = rel;
        typesFiles_[FileKey(rel)] = std::move(parsed.doc);
    }
}

void Workspace::RebuildTypesCatalog() {
    std::vector<std::pair<std::string, std::string>> labeled;
    labeled.reserve(typesFiles_.size());
    for (const auto& [_, doc] : typesFiles_) {
        labeled.push_back({doc.relPath, SerializeTypesDocument(doc)});
    }
    const auto folder = typesCatalog_.folder;
    const auto error = typesCatalog_.error;
    typesCatalog_ = BuildTypesCatalog(folder, labeled);
    if (typesCatalog_.error.empty()) {
        typesCatalog_.error = error;
    }
    SaveTypesCatalog(typesCatalog_);
}

std::string Workspace::ResolveTypesName(const std::string& incoming) const {
    const auto rawKey = FileKey(incoming);
    if (auto it = typesFiles_.find(rawKey); it != typesFiles_.end()) {
        return it->second.relPath;
    }
    const auto cleaned = TypesFileName(incoming);
    if (auto it = typesFiles_.find(FileKey(cleaned)); it != typesFiles_.end()) {
        return it->second.relPath;
    }
    return incoming;
}

void Workspace::SaveTypes(const nlohmann::json& body) {
    auto doc = TypesFileFromUi(body);
    doc.relPath = ResolveTypesName(doc.relPath);
    if (Trim(doc.relPath).empty()) {
        throw std::runtime_error("Types file path is empty");
    }
    PersistTypesFile(root_, doc.relPath, SerializeTypesDocument(doc));
    typesFiles_[FileKey(doc.relPath)] = doc;
    MarkDirty(FileKind::Types, doc.relPath);
    RebuildTypesCatalog();
}

void Workspace::SaveMarket(const nlohmann::json& body) {
    auto cat = MarketFromUi(body);
    cat.filename = ResolveExistingName(markets_, cat.filename);
    const auto filename = cat.filename;
    Persist(FileKind::Market, filename, SerializeMarket(cat));
    markets_[FileKey(filename)] = std::move(cat);
    MarkDirty(FileKind::Market, filename);
}

void Workspace::SaveTrader(const nlohmann::json& body) {
    auto trader = TraderFromUi(body);
    trader.filename = ResolveExistingName(traders_, trader.filename);
    const auto filename = trader.filename;
    Persist(FileKind::Trader, filename, SerializeTrader(trader));
    traders_[FileKey(filename)] = std::move(trader);
    MarkDirty(FileKind::Trader, filename);
}

void Workspace::SaveZone(const nlohmann::json& body) {
    auto zone = ZoneFromUi(body);
    zone.filename = ResolveExistingName(zones_, zone.filename);
    const auto filename = zone.filename;
    Persist(FileKind::TraderZone, filename, SerializeZone(zone));
    zones_[FileKey(filename)] = std::move(zone);
    MarkDirty(FileKind::TraderZone, filename);
}

nlohmann::json Workspace::CreateWorkspaceFile(FileKind kind, const std::string& filename) {
    if (kind == FileKind::Types) {
        const auto clean = TypesFileName(filename);
        if (typesFiles_.contains(FileKey(clean))) {
            throw std::runtime_error("A types file with that path already exists");
        }
        TypesDocument doc;
        doc.relPath = clean;
        typesFiles_[FileKey(clean)] = doc;
        PersistTypesFile(root_, clean, SerializeTypesDocument(doc));
        MarkDirty(FileKind::Types, clean);
        if (!economyCoreXml_.empty()) {
            EconomyCoreAddFile(economyCoreXml_, clean);
            PersistTypesFile(root_, "cfgeconomycore.xml", economyCoreXml_);
            MarkDirty(FileKind::Types, "cfgeconomycore.xml");
        }
        RebuildTypesCatalog();
        return TypesFileToUi(doc);
    }
    const auto clean = JsonFileName(filename);
    const int version = SiblingVersion(kind);
    if (kind == FileKind::Market) {
        if (markets_.contains(FileKey(clean))) {
            throw std::runtime_error("A Market file with that name already exists");
        }
        auto cat = DefaultMarket(clean, version);
        markets_[FileKey(clean)] = cat;
        Persist(kind, clean, SerializeMarket(cat));
        MarkDirty(kind, clean);
        return MarketToUi(cat);
    }
    if (kind == FileKind::Trader) {
        if (traders_.contains(FileKey(clean))) {
            throw std::runtime_error("A Trader file with that name already exists");
        }
        auto trader = DefaultTrader(clean, version);
        traders_[FileKey(clean)] = trader;
        Persist(kind, clean, SerializeTrader(trader));
        MarkDirty(kind, clean);
        return TraderToUi(trader);
    }
    if (zones_.contains(FileKey(clean))) {
        throw std::runtime_error("A TraderZone file with that name already exists");
    }
    auto zone = DefaultZone(clean, version);
    zones_[FileKey(clean)] = zone;
    Persist(kind, clean, SerializeZone(zone));
    MarkDirty(kind, clean);
    return ZoneToUi(zone);
}

void Workspace::DeleteWorkspaceFile(FileKind kind, const std::string& filename) {
    if (kind == FileKind::Types) {
        const auto clean = ResolveTypesName(filename);
        const auto key = FileKey(clean);
        typesFiles_.erase(key);
        std::error_code ec;
        std::filesystem::remove(TypesLocalPath(root_, clean), ec);
        pendingDeletes_.insert(RelKey(KindName(kind), clean));
        dirty_.erase(RelKey(KindName(kind), clean));
        if (!economyCoreXml_.empty()) {
            EconomyCoreRemoveFile(economyCoreXml_, clean);
            PersistTypesFile(root_, "cfgeconomycore.xml", economyCoreXml_);
            MarkDirty(FileKind::Types, "cfgeconomycore.xml");
        }
        RebuildTypesCatalog();
        return;
    }
    const auto clean = JsonFileName(filename);
    const auto key = FileKey(clean);
    if (kind == FileKind::Market) {
        markets_.erase(key);
    } else if (kind == FileKind::Trader) {
        traders_.erase(key);
    } else {
        zones_.erase(key);
    }
    std::error_code ec;
    std::filesystem::remove(LocalPath(kind, clean), ec);
    pendingDeletes_.insert(RelKey(KindName(kind), clean));
    dirty_.erase(RelKey(KindName(kind), clean));
}

nlohmann::json Workspace::ConfirmUpload() {
    const auto issues = Validate();
    for (const auto& issue : issues) {
        if (issue.severity == ValidationIssue::Severity::Error) {
            nlohmann::json fail;
            fail["uploaded"] = false;
            fail["issues"] = issues;
            fail["error"] = "Validation failed. Fix errors before uploading.";
            return fail;
        }
    }

    transfer_.Report("Creating backup zip...", 5);
    const auto zipPath = BackupsDir(profile_.id) / Utf8ToWide(NowStamp() + ".zip");
    const auto backup = ZipWorkspaceFolder(root_, zipPath);

    try {
        int done = 0;
        for (const auto& key : dirty_) {
            const auto slash = key.find('/');
            if (slash == std::string::npos) {
                continue;
            }
            const auto kind = KindFromName(key.substr(0, slash));
            const auto filename = key.substr(slash + 1);
            std::string contents;
            std::string remote;
            std::string remoteName = filename;
            if (kind == FileKind::Types) {
                if (Trim(typesCatalog_.folder).empty()) {
                    throw std::runtime_error("Types mission folder is unknown. Re-pull types, then upload.");
                }
                if (IEquals(filename, "cfgeconomycore.xml")) {
                    contents = economyCoreXml_;
                } else {
                    contents = SerializeTypesDocument(typesFiles_.at(FileKey(filename)));
                }
                const auto split = SplitRemoteFile(JoinRemotePath(typesCatalog_.folder, filename));
                remote = split.first;
                remoteName = split.second;
            } else {
                remote = RemotePath(kind);
                if (kind == FileKind::Market) {
                    contents = SerializeMarket(markets_.at(FileKey(filename)));
                } else if (kind == FileKind::Trader) {
                    contents = SerializeTrader(traders_.at(FileKey(filename)));
                } else {
                    contents = SerializeZone(zones_.at(FileKey(filename)));
                }
            }
            if (Trim(remote).empty()) {
                throw std::runtime_error(std::string(KindName(kind)) + " remote path is empty");
            }
            transfer_.UploadFile(remote, remoteName, contents);
            ++done;
            transfer_.Report("Uploaded " + filename, std::min(80, 20 + done * 5));
        }
        for (const auto& key : pendingDeletes_) {
            const auto slash = key.find('/');
            if (slash == std::string::npos) {
                continue;
            }
            const auto kind = KindFromName(key.substr(0, slash));
            const auto filename = key.substr(slash + 1);
            std::string remote;
            std::string remoteName = filename;
            if (kind == FileKind::Types) {
                if (Trim(typesCatalog_.folder).empty()) {
                    throw std::runtime_error("Types mission folder is unknown. Re-pull types, then upload.");
                }
                const auto split = SplitRemoteFile(JoinRemotePath(typesCatalog_.folder, filename));
                remote = split.first;
                remoteName = split.second;
            } else {
                remote = RemotePath(kind);
            }
            transfer_.DeleteRemoteFile(remote, remoteName);
        }
    } catch (const std::exception& ex) {
        nlohmann::json fail;
        fail["uploaded"] = false;
        fail["backup"] = backup;
        fail["error"] = std::string("Upload failed. Local edits and backup were kept. ") + ex.what();
        fail["issues"] = issues;
        return fail;
    }

    PullAll();
    nlohmann::json ok;
    ok["uploaded"] = true;
    ok["backup"] = backup;
    ok["workspace"] = ToUiJson();
    return ok;
}

std::vector<std::string> Workspace::ListBackups() const {
    std::vector<std::string> names;
    const auto dir = BackupsDir(profile_.id);
    std::error_code ec;
    for (const auto& item : std::filesystem::directory_iterator(dir, ec)) {
        if (item.is_regular_file()) {
            names.push_back(WideToUtf8(item.path().filename().wstring()));
        }
    }
    std::sort(names.begin(), names.end());
    std::reverse(names.begin(), names.end());
    return names;
}

}  // namespace edity
