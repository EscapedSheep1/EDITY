#include "types/TypesCatalog.h"

#include "app/Paths.h"
#include "app/Utf8.h"
#include "app/Util.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace edity {
namespace {

std::size_t FindI(std::string_view hay, std::string_view needle, std::size_t from = 0) {
    if (needle.empty() || from >= hay.size()) {
        return std::string_view::npos;
    }
    for (std::size_t i = from; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t n = 0; n < needle.size(); ++n) {
            const auto a = static_cast<unsigned char>(hay[i + n]);
            const auto b = static_cast<unsigned char>(needle[n]);
            if (std::tolower(a) != std::tolower(b)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return i;
        }
    }
    return std::string_view::npos;
}

std::string XmlAttr(std::string_view tag, std::string_view key) {
    auto pos = FindI(tag, key);
    while (pos != std::string_view::npos) {
        std::size_t i = pos + key.size();
        while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\r' || tag[i] == '\n')) {
            ++i;
        }
        if (i >= tag.size() || tag[i] != '=') {
            pos = FindI(tag, key, pos + 1);
            continue;
        }
        ++i;
        while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\r' || tag[i] == '\n')) {
            ++i;
        }
        if (i >= tag.size()) {
            break;
        }
        const char quote = tag[i];
        if (quote != '"' && quote != '\'') {
            pos = FindI(tag, key, pos + 1);
            continue;
        }
        ++i;
        const auto end = tag.find(quote, i);
        if (end == std::string_view::npos) {
            break;
        }
        return std::string(tag.substr(i, end - i));
    }
    return {};
}

bool LooksLikeTypesRoot(std::string_view raw) {
    const auto probe = raw.substr(0, std::min<std::size_t>(raw.size(), 65536));
    return FindI(probe, "<types") != std::string_view::npos;
}

bool IsActiveTypesFile(const std::string& name, const std::string& type) {
    const auto kind = ToLowerAscii(Trim(type));
    if (kind == "spawnabletypes" || kind == "events" || kind == "economy" || kind == "messages") {
        return false;
    }
    if (kind == "types") {
        return !Trim(name).empty();
    }
    return kind.empty() && EndsWithI(Trim(name), "types.xml");
}

std::string StripBom(std::string raw) {
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    return raw;
}

}  // namespace

std::string TypesFileRef::Relative() const {
    return JoinRemotePath(folder, filename);
}

std::string NormalizeRemotePath(std::string path) {
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (!path.empty() && (path.back() == '/' || path.back() == ' ')) {
        path.pop_back();
    }
    while (path.size() > 1 && path.find("./") == 0) {
        path.erase(0, 2);
    }
    return path;
}

std::string ParentRemotePath(std::string path) {
    path = NormalizeRemotePath(std::move(path));
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash);
}

std::string JoinRemotePath(std::string dir, const std::string& name) {
    dir = NormalizeRemotePath(std::move(dir));
    auto file = Trim(name);
    for (char& c : file) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (!file.empty() && file.front() == '/') {
        file.erase(file.begin());
    }
    if (dir.empty() || dir == ".") {
        return file;
    }
    if (!dir.empty() && dir.back() != '/') {
        dir.push_back('/');
    }
    return dir + file;
}

std::pair<std::string, std::string> SplitRemoteFile(const std::string& path) {
    const auto norm = NormalizeRemotePath(path);
    const auto slash = norm.find_last_of('/');
    if (slash == std::string::npos) {
        return {{}, norm};
    }
    return {norm.substr(0, slash), norm.substr(slash + 1)};
}

std::string GuessMissionRoot(std::string_view zonesPath) {
    const auto norm = NormalizeRemotePath(std::string(zonesPath));
    const auto lower = ToLowerAscii(norm);
    auto mp = lower.find("mpmissions/");
    if (mp == std::string::npos) {
        mp = lower.find("mpmissions");
        if (mp != std::string::npos && mp + 10 < lower.size() && lower[mp + 10] == '/') {
            // already handled
        } else if (mp != std::string::npos && mp + 10 == lower.size()) {
            return norm;
        }
    }
    if (mp != std::string::npos) {
        const auto after = mp + 11;
        if (after >= norm.size()) {
            return norm;
        }
        const auto slash = norm.find('/', after);
        if (slash == std::string::npos) {
            return norm;
        }
        return norm.substr(0, slash);
    }
    const auto exp = lower.find("/expansion");
    if (exp != std::string::npos) {
        return norm.substr(0, exp);
    }
    return norm;
}

nlohmann::json TypesCatalog::ToUi() const {
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& entry : types) {
        rows.push_back({
            {"name", entry.name},
            {"category", entry.category},
            {"file", entry.file},
        });
    }
    return {
        {"folder", folder},
        {"importedAt", importedAt},
        {"error", error},
        {"fileCount", fileCount},
        {"skippedDuplicates", skippedDuplicates},
        {"typeCount", static_cast<int>(types.size())},
        {"files", files},
        {"types", rows},
    };
}

std::vector<TypesFileRef> ParseEconomyCore(std::string_view raw) {
    std::vector<TypesFileRef> out;
    std::size_t cursor = 0;
    while (cursor < raw.size()) {
        const auto ce = FindI(raw, "<ce", cursor);
        if (ce == std::string_view::npos) {
            break;
        }
        const auto after = ce + 3;
        if (after < raw.size() && std::isalnum(static_cast<unsigned char>(raw[after]))) {
            cursor = after;
            continue;
        }
        const auto tagEnd = raw.find('>', ce);
        if (tagEnd == std::string_view::npos) {
            break;
        }
        const auto openTag = raw.substr(ce, tagEnd - ce + 1);
        const auto folder = Trim(XmlAttr(openTag, "folder"));
        cursor = tagEnd + 1;
        if (!openTag.empty() && openTag[openTag.size() - 2] == '/') {
            continue;
        }
        const auto close = FindI(raw, "</ce>", tagEnd);
        const auto blockEnd = close == std::string_view::npos ? raw.size() : close;
        const auto block = raw.substr(tagEnd + 1, blockEnd - (tagEnd + 1));
        std::size_t filePos = 0;
        while (filePos < block.size()) {
            const auto fs = FindI(block, "<file", filePos);
            if (fs == std::string_view::npos) {
                break;
            }
            const auto fe = block.find('>', fs);
            if (fe == std::string_view::npos) {
                break;
            }
            const auto tag = block.substr(fs, fe - fs + 1);
            const auto name = Trim(XmlAttr(tag, "name"));
            const auto type = Trim(XmlAttr(tag, "type"));
            filePos = fe + 1;
            if (IsActiveTypesFile(name, type)) {
                TypesFileRef ref;
                ref.folder = folder;
                ref.filename = name;
                out.push_back(std::move(ref));
            }
        }
        cursor = close == std::string_view::npos ? blockEnd : close + 5;
    }
    return out;
}

bool IsVanillaTypesFile(const TypesFileRef& ref) {
    const auto rel = ToLowerAscii(NormalizeRemotePath(ref.Relative()));
    if (rel == "db/types.xml") {
        return true;
    }
    return IEquals(Trim(ref.folder), "db") && IEquals(Trim(ref.filename), "types.xml");
}

void EnsureVanillaTypesFile(std::vector<TypesFileRef>& refs) {
    for (const auto& ref : refs) {
        if (IsVanillaTypesFile(ref)) {
            return;
        }
    }
    TypesFileRef vanilla;
    vanilla.folder = "db";
    vanilla.filename = "types.xml";
    refs.insert(refs.begin(), std::move(vanilla));
}

std::vector<TypeEntry> ParseTypesXml(std::string_view raw, std::string_view fileLabel) {
    std::vector<TypeEntry> out;
    std::size_t cursor = 0;
    while (cursor < raw.size()) {
        const auto start = FindI(raw, "<type", cursor);
        if (start == std::string_view::npos) {
            break;
        }
        const auto after = start + 5;
        if (after < raw.size()) {
            const char next = raw[after];
            if (std::isalnum(static_cast<unsigned char>(next))) {
                cursor = after;
                continue;
            }
        }
        const auto tagEnd = raw.find('>', start);
        if (tagEnd == std::string_view::npos) {
            break;
        }
        const auto name = Trim(XmlAttr(raw.substr(start, tagEnd - start + 1), "name"));
        cursor = tagEnd + 1;
        if (name.empty()) {
            continue;
        }
        TypeEntry entry;
        entry.name = name;
        entry.file = std::string(fileLabel);
        const auto close = FindI(raw, "</type>", tagEnd);
        if (close != std::string_view::npos) {
            const auto block = raw.substr(tagEnd + 1, close - (tagEnd + 1));
            const auto catTag = FindI(block, "<category");
            if (catTag != std::string_view::npos) {
                const auto catEnd = block.find('>', catTag);
                if (catEnd != std::string_view::npos) {
                    entry.category = Trim(XmlAttr(block.substr(catTag, catEnd - catTag + 1), "name"));
                }
            }
            cursor = close + 7;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

TypesCatalog BuildTypesCatalog(const std::string& missionFolder,
                               const std::vector<std::pair<std::string, std::string>>& labeledXml) {
    TypesCatalog catalog;
    catalog.folder = missionFolder;
    catalog.importedAt = NowStamp();
    std::unordered_set<std::string> seen;
    for (const auto& [label, raw] : labeledXml) {
        const auto xml = StripBom(raw);
        if (!LooksLikeTypesRoot(xml)) {
            continue;
        }
        const auto parsed = ParseTypesXml(xml, label);
        if (parsed.empty()) {
            continue;
        }
        catalog.files.push_back(label);
        for (auto& entry : parsed) {
            const auto key = ToLowerAscii(entry.name);
            if (!seen.insert(key).second) {
                ++catalog.skippedDuplicates;
                continue;
            }
            catalog.types.push_back(std::move(entry));
        }
    }
    catalog.fileCount = static_cast<int>(catalog.files.size());
    std::sort(catalog.types.begin(), catalog.types.end(), [](const TypeEntry& a, const TypeEntry& b) {
        return ToLowerAscii(a.name) < ToLowerAscii(b.name);
    });
    std::sort(catalog.files.begin(), catalog.files.end());
    return catalog;
}

void SaveTypesCatalog(const TypesCatalog& catalog) {
    WriteFileAtomic(TypesCatalogPath(), catalog.ToUi().dump(2));
}

TypesCatalog LoadTypesCatalog() {
    TypesCatalog catalog;
    const auto path = TypesCatalogPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return catalog;
    }
    try {
        const auto json = nlohmann::json::parse(ReadFileUtf8(path));
        catalog.folder = json.value("folder", std::string());
        catalog.importedAt = json.value("importedAt", std::string());
        catalog.error = json.value("error", std::string());
        catalog.fileCount = json.value("fileCount", 0);
        catalog.skippedDuplicates = json.value("skippedDuplicates", 0);
        if (json.contains("files") && json["files"].is_array()) {
            catalog.files = json["files"].get<std::vector<std::string>>();
        }
        if (json.contains("types") && json["types"].is_array()) {
            for (const auto& row : json["types"]) {
                TypeEntry entry;
                entry.name = row.value("name", std::string());
                entry.category = row.value("category", std::string());
                entry.file = row.value("file", std::string());
                if (!entry.name.empty()) {
                    catalog.types.push_back(std::move(entry));
                }
            }
        }
    } catch (...) {
        return {};
    }
    return catalog;
}

}  // namespace edity
