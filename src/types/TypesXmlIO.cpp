#include "types/TypesXmlIO.h"

#include "app/Utf8.h"
#include "types/TypesCatalog.h"

#include <cctype>
#include <sstream>

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

std::string StripBom(std::string raw) {
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    return raw;
}

std::string XmlEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

int ParseInt(std::string_view text, int fallback) {
    const auto trimmed = Trim(text);
    if (trimmed.empty()) {
        return fallback;
    }
    try {
        std::size_t idx = 0;
        const int value = std::stoi(std::string(trimmed), &idx, 10);
        if (idx == 0) {
            return fallback;
        }
        return value;
    } catch (...) {
        return fallback;
    }
}

int FlagAttr(std::string_view tag, std::string_view key, int fallback) {
    const auto raw = Trim(XmlAttr(tag, key));
    if (raw.empty()) {
        return fallback;
    }
    return ParseInt(raw, fallback) ? 1 : 0;
}

std::string InnerText(std::string_view block, std::string_view tag) {
    const auto open = std::string("<") + std::string(tag);
    const auto start = FindI(block, open);
    if (start == std::string_view::npos) {
        return {};
    }
    const auto tagEnd = block.find('>', start);
    if (tagEnd == std::string_view::npos) {
        return {};
    }
    const auto closeName = std::string("</") + std::string(tag) + ">";
    const auto close = FindI(block, closeName, tagEnd);
    if (close == std::string_view::npos) {
        return {};
    }
    return std::string(block.substr(tagEnd + 1, close - (tagEnd + 1)));
}

void CollectNamed(std::string_view block, std::string_view tag, std::vector<std::string>& out) {
    const auto open = std::string("<") + std::string(tag);
    std::size_t cursor = 0;
    while (cursor < block.size()) {
        const auto start = FindI(block, open, cursor);
        if (start == std::string_view::npos) {
            break;
        }
        const auto after = start + open.size();
        if (after < block.size() && std::isalnum(static_cast<unsigned char>(block[after]))) {
            cursor = after;
            continue;
        }
        const auto tagEnd = block.find('>', start);
        if (tagEnd == std::string_view::npos) {
            break;
        }
        const auto name = Trim(XmlAttr(block.substr(start, tagEnd - start + 1), "name"));
        if (!name.empty()) {
            out.push_back(name);
        }
        cursor = tagEnd + 1;
    }
}

TypeDefinition ParseTypeBlock(std::string_view openTag, std::string_view body) {
    TypeDefinition type = DefaultType(Trim(XmlAttr(openTag, "name")));
    type.nominal = ParseInt(InnerText(body, "nominal"), type.nominal);
    type.lifetime = ParseInt(InnerText(body, "lifetime"), type.lifetime);
    type.restock = ParseInt(InnerText(body, "restock"), type.restock);
    type.min = ParseInt(InnerText(body, "min"), type.min);
    type.quantMin = ParseInt(InnerText(body, "quantmin"), type.quantMin);
    type.quantMax = ParseInt(InnerText(body, "quantmax"), type.quantMax);
    type.cost = ParseInt(InnerText(body, "cost"), type.cost);

    const auto flagsAt = FindI(body, "<flags");
    if (flagsAt != std::string_view::npos) {
        const auto flagsEnd = body.find('>', flagsAt);
        if (flagsEnd != std::string_view::npos) {
            const auto tag = body.substr(flagsAt, flagsEnd - flagsAt + 1);
            type.flags.countInCargo = FlagAttr(tag, "count_in_cargo", type.flags.countInCargo);
            type.flags.countInHoarder = FlagAttr(tag, "count_in_hoarder", type.flags.countInHoarder);
            type.flags.countInMap = FlagAttr(tag, "count_in_map", type.flags.countInMap);
            type.flags.countInPlayer = FlagAttr(tag, "count_in_player", type.flags.countInPlayer);
            type.flags.crafted = FlagAttr(tag, "crafted", type.flags.crafted);
            type.flags.deloot = FlagAttr(tag, "deloot", type.flags.deloot);
        }
    }

    std::vector<std::string> categories;
    CollectNamed(body, "category", categories);
    if (!categories.empty()) {
        type.category = categories.front();
    }
    CollectNamed(body, "usage", type.usages);
    CollectNamed(body, "value", type.values);
    CollectNamed(body, "tag", type.tags);
    return type;
}

std::vector<std::string> StringArray(const nlohmann::json& json, const char* key) {
    std::vector<std::string> out;
    if (!json.contains(key) || !json[key].is_array()) {
        return out;
    }
    for (const auto& row : json[key]) {
        if (row.is_string()) {
            const auto value = Trim(row.get<std::string>());
            if (!value.empty()) {
                out.push_back(value);
            }
        }
    }
    return out;
}

void WriteNamed(std::ostringstream& out, const char* tag, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        if (Trim(name).empty()) {
            continue;
        }
        out << "        <" << tag << " name=\"" << XmlEscape(name) << "\"/>\n";
    }
}

TypeFlags FlagsFromUi(const nlohmann::json& json, TypeFlags fallback) {
    if (!json.is_object()) {
        return fallback;
    }
    fallback.countInCargo = json.value("countInCargo", fallback.countInCargo) ? 1 : 0;
    fallback.countInHoarder = json.value("countInHoarder", fallback.countInHoarder) ? 1 : 0;
    fallback.countInMap = json.value("countInMap", fallback.countInMap) ? 1 : 0;
    fallback.countInPlayer = json.value("countInPlayer", fallback.countInPlayer) ? 1 : 0;
    fallback.crafted = json.value("crafted", fallback.crafted) ? 1 : 0;
    fallback.deloot = json.value("deloot", fallback.deloot) ? 1 : 0;
    return fallback;
}

}  // namespace

TypeDefinition DefaultType(const std::string& name) {
    TypeDefinition type;
    type.name = name;
    return type;
}

ParseTypesResult ParseTypesDocument(std::string_view raw, std::string_view relPath) {
    ParseTypesResult result;
    result.doc.relPath = std::string(relPath);
    const auto xml = StripBom(std::string(raw));
    std::size_t cursor = 0;
    bool foundTypes = FindI(xml, "<types") != std::string_view::npos;
    if (!foundTypes && !Trim(xml).empty()) {
        result.error = "File is not a types.xml document";
        return result;
    }
    while (cursor < xml.size()) {
        const auto start = FindI(xml, "<type", cursor);
        if (start == std::string_view::npos) {
            break;
        }
        const auto after = start + 5;
        if (after < xml.size() && std::isalnum(static_cast<unsigned char>(xml[after]))) {
            cursor = after;
            continue;
        }
        const auto tagEnd = xml.find('>', start);
        if (tagEnd == std::string_view::npos) {
            break;
        }
        const auto openTag = xml.substr(start, tagEnd - start + 1);
        cursor = tagEnd + 1;
        std::string body;
        if (openTag.size() >= 2 && openTag[openTag.size() - 2] == '/') {
            body = {};
        } else {
            const auto close = FindI(xml, "</type>", tagEnd);
            if (close == std::string_view::npos) {
                break;
            }
            body = std::string(xml.substr(tagEnd + 1, close - (tagEnd + 1)));
            cursor = close + 7;
        }
        result.doc.types.push_back(ParseTypeBlock(openTag, body));
    }
    result.ok = true;
    return result;
}

std::string SerializeTypesDocument(const TypesDocument& doc) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    out << "<types>\n";
    for (const auto& type : doc.types) {
        out << "    <type name=\"" << XmlEscape(type.name) << "\">\n";
        out << "        <nominal>" << type.nominal << "</nominal>\n";
        out << "        <lifetime>" << type.lifetime << "</lifetime>\n";
        out << "        <restock>" << type.restock << "</restock>\n";
        out << "        <min>" << type.min << "</min>\n";
        out << "        <quantmin>" << type.quantMin << "</quantmin>\n";
        out << "        <quantmax>" << type.quantMax << "</quantmax>\n";
        out << "        <cost>" << type.cost << "</cost>\n";
        out << "        <flags count_in_cargo=\"" << (type.flags.countInCargo ? 1 : 0)
            << "\" count_in_hoarder=\"" << (type.flags.countInHoarder ? 1 : 0)
            << "\" count_in_map=\"" << (type.flags.countInMap ? 1 : 0)
            << "\" count_in_player=\"" << (type.flags.countInPlayer ? 1 : 0)
            << "\" crafted=\"" << (type.flags.crafted ? 1 : 0)
            << "\" deloot=\"" << (type.flags.deloot ? 1 : 0) << "\"/>\n";
        if (!Trim(type.category).empty()) {
            out << "        <category name=\"" << XmlEscape(type.category) << "\"/>\n";
        }
        WriteNamed(out, "usage", type.usages);
        WriteNamed(out, "value", type.values);
        WriteNamed(out, "tag", type.tags);
        out << "    </type>\n";
    }
    out << "</types>\n";
    return out.str();
}

nlohmann::json TypesFileToUi(const TypesDocument& doc) {
    nlohmann::json types = nlohmann::json::array();
    for (const auto& type : doc.types) {
        types.push_back({
            {"name", type.name},
            {"nominal", type.nominal},
            {"lifetime", type.lifetime},
            {"restock", type.restock},
            {"min", type.min},
            {"quantMin", type.quantMin},
            {"quantMax", type.quantMax},
            {"cost", type.cost},
            {"flags",
             {{"countInCargo", type.flags.countInCargo},
              {"countInHoarder", type.flags.countInHoarder},
              {"countInMap", type.flags.countInMap},
              {"countInPlayer", type.flags.countInPlayer},
              {"crafted", type.flags.crafted},
              {"deloot", type.flags.deloot}}},
            {"category", type.category},
            {"usages", type.usages},
            {"values", type.values},
            {"tags", type.tags},
        });
    }
    return {
        {"filename", doc.relPath},
        {"types", types},
    };
}

TypesDocument TypesFileFromUi(const nlohmann::json& body) {
    TypesDocument doc;
    doc.relPath = body.value("filename", std::string());
    if (!body.contains("types") || !body["types"].is_array()) {
        return doc;
    }
    for (const auto& row : body["types"]) {
        TypeDefinition type = DefaultType(row.value("name", std::string()));
        type.nominal = row.value("nominal", type.nominal);
        type.lifetime = row.value("lifetime", type.lifetime);
        type.restock = row.value("restock", type.restock);
        type.min = row.value("min", type.min);
        type.quantMin = row.value("quantMin", type.quantMin);
        type.quantMax = row.value("quantMax", type.quantMax);
        type.cost = row.value("cost", type.cost);
        type.category = row.value("category", std::string());
        type.usages = StringArray(row, "usages");
        type.values = StringArray(row, "values");
        type.tags = StringArray(row, "tags");
        if (row.contains("flags")) {
            type.flags = FlagsFromUi(row["flags"], type.flags);
        }
        doc.types.push_back(std::move(type));
    }
    return doc;
}

std::string TypesFileName(std::string_view name) {
    std::string raw = NormalizeRemotePath(std::string(name));
    while (!raw.empty() && raw.front() == '/') {
        raw.erase(raw.begin());
    }
    if (EndsWithI(raw, ".xml")) {
        raw.resize(raw.size() - 4);
    }
    std::string cleaned;
    cleaned.reserve(raw.size());
    for (const unsigned char c : raw) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
            c == '/') {
            cleaned.push_back(static_cast<char>(c));
        } else if (c == ' ' || c == '.' || c == '\\') {
            cleaned.push_back(c == '\\' ? '/' : '_');
        }
    }
    while (cleaned.find("//") != std::string::npos) {
        cleaned.replace(cleaned.find("//"), 2, "/");
    }
    while (cleaned.find("..") != std::string::npos) {
        cleaned.replace(cleaned.find(".."), 2, "");
    }
    while (!cleaned.empty() && cleaned.front() == '/') {
        cleaned.erase(cleaned.begin());
    }
    if (cleaned.empty()) {
        cleaned = "CustomTypes/New_types";
    }
    if (!EndsWithI(cleaned, "types")) {
        cleaned += "_types";
    }
    return cleaned + ".xml";
}

bool EconomyCoreHasFile(std::string_view xml, std::string_view relPath) {
    const auto [folder, filename] = SplitRemoteFile(NormalizeRemotePath(std::string(relPath)));
    const auto wantFile = ToLowerAscii(filename);
    const auto wantFolder = ToLowerAscii(folder);
    std::size_t cursor = 0;
    while (cursor < xml.size()) {
        const auto ce = FindI(xml, "<ce", cursor);
        if (ce == std::string_view::npos) {
            break;
        }
        const auto after = ce + 3;
        if (after < xml.size() && std::isalnum(static_cast<unsigned char>(xml[after]))) {
            cursor = after;
            continue;
        }
        const auto tagEnd = xml.find('>', ce);
        if (tagEnd == std::string_view::npos) {
            break;
        }
        const auto openTag = xml.substr(ce, tagEnd - ce + 1);
        const auto ceFolder = ToLowerAscii(Trim(XmlAttr(openTag, "folder")));
        cursor = tagEnd + 1;
        const auto close = FindI(xml, "</ce>", tagEnd);
        const auto blockEnd = close == std::string_view::npos ? xml.size() : close;
        if (ceFolder != wantFolder) {
            cursor = close == std::string_view::npos ? blockEnd : close + 5;
            continue;
        }
        const auto block = xml.substr(tagEnd + 1, blockEnd - (tagEnd + 1));
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
            const auto name = ToLowerAscii(Trim(XmlAttr(block.substr(fs, fe - fs + 1), "name")));
            if (name == wantFile) {
                return true;
            }
            filePos = fe + 1;
        }
        cursor = close == std::string_view::npos ? blockEnd : close + 5;
    }
    return false;
}

void EconomyCoreAddFile(std::string& xml, const std::string& relPath) {
    if (EconomyCoreHasFile(xml, relPath)) {
        return;
    }
    const auto [folder, filename] = SplitRemoteFile(NormalizeRemotePath(relPath));
    const auto entry = "        <file name=\"" + XmlEscape(filename) + "\" type=\"types\" />\n";
    const auto wantFolder = ToLowerAscii(folder);
    std::size_t cursor = 0;
    while (cursor < xml.size()) {
        const auto ce = FindI(xml, "<ce", cursor);
        if (ce == std::string_view::npos) {
            break;
        }
        const auto after = ce + 3;
        if (after < xml.size() && std::isalnum(static_cast<unsigned char>(xml[after]))) {
            cursor = after;
            continue;
        }
        const auto tagEnd = xml.find('>', ce);
        if (tagEnd == std::string_view::npos) {
            break;
        }
        const auto openTag = xml.substr(ce, tagEnd - ce + 1);
        const auto ceFolder = ToLowerAscii(Trim(XmlAttr(openTag, "folder")));
        const auto close = FindI(xml, "</ce>", tagEnd);
        if (ceFolder == wantFolder && close != std::string_view::npos) {
            xml.insert(close, entry);
            return;
        }
        cursor = close == std::string_view::npos ? tagEnd + 1 : close + 5;
    }
    std::string block = "    <ce folder=\"" + XmlEscape(folder) + "\">\n" + entry + "    </ce>\n";
    const auto rootClose = FindI(xml, "</economycore>");
    if (rootClose != std::string_view::npos) {
        xml.insert(rootClose, block);
        return;
    }
    if (Trim(xml).empty()) {
        xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<economycore>\n" + block +
              "</economycore>\n";
        return;
    }
    xml += block;
}

void EconomyCoreRemoveFile(std::string& xml, const std::string& relPath) {
    const auto [folder, filename] = SplitRemoteFile(NormalizeRemotePath(relPath));
    const auto wantFile = ToLowerAscii(filename);
    const auto wantFolder = ToLowerAscii(folder);
    std::size_t cursor = 0;
    while (cursor < xml.size()) {
        const auto ce = FindI(xml, "<ce", cursor);
        if (ce == std::string_view::npos) {
            break;
        }
        const auto after = ce + 3;
        if (after < xml.size() && std::isalnum(static_cast<unsigned char>(xml[after]))) {
            cursor = after;
            continue;
        }
        const auto tagEnd = xml.find('>', ce);
        if (tagEnd == std::string_view::npos) {
            break;
        }
        const auto openTag = xml.substr(ce, tagEnd - ce + 1);
        const auto ceFolder = ToLowerAscii(Trim(XmlAttr(openTag, "folder")));
        const auto close = FindI(xml, "</ce>", tagEnd);
        cursor = close == std::string_view::npos ? tagEnd + 1 : close + 5;
        if (ceFolder != wantFolder || close == std::string_view::npos) {
            continue;
        }
        auto block = xml.substr(tagEnd + 1, close - (tagEnd + 1));
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
            const auto name = ToLowerAscii(Trim(XmlAttr(block.substr(fs, fe - fs + 1), "name")));
            filePos = fe + 1;
            if (name != wantFile) {
                continue;
            }
            std::size_t lineStart = fs;
            while (lineStart > 0 && block[lineStart - 1] != '\n') {
                --lineStart;
            }
            std::size_t lineEnd = fe + 1;
            if (lineEnd < block.size() && block[lineEnd] == '\r') {
                ++lineEnd;
            }
            if (lineEnd < block.size() && block[lineEnd] == '\n') {
                ++lineEnd;
            }
            block.erase(lineStart, lineEnd - lineStart);
            xml.replace(tagEnd + 1, close - (tagEnd + 1), block);
            return;
        }
    }
}

}  // namespace edity
