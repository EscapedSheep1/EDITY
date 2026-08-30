#include "backup/ZipArchive.h"

#include "app/Utf8.h"
#include "app/Util.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace edity {
namespace {

#pragma pack(push, 1)
struct LocalHeader {
    std::uint32_t signature = 0x04034b50;
    std::uint16_t version = 20;
    std::uint16_t flags = 0;
    std::uint16_t method = 0;
    std::uint16_t time = 0;
    std::uint16_t date = 0;
    std::uint32_t crc = 0;
    std::uint32_t compressed = 0;
    std::uint32_t uncompressed = 0;
    std::uint16_t nameLength = 0;
    std::uint16_t extraLength = 0;
};

struct CentralHeader {
    std::uint32_t signature = 0x02014b50;
    std::uint16_t madeBy = 20;
    std::uint16_t version = 20;
    std::uint16_t flags = 0;
    std::uint16_t method = 0;
    std::uint16_t time = 0;
    std::uint16_t date = 0;
    std::uint32_t crc = 0;
    std::uint32_t compressed = 0;
    std::uint32_t uncompressed = 0;
    std::uint16_t nameLength = 0;
    std::uint16_t extraLength = 0;
    std::uint16_t commentLength = 0;
    std::uint16_t disk = 0;
    std::uint16_t internalAttr = 0;
    std::uint32_t externalAttr = 0;
    std::uint32_t localOffset = 0;
};

struct EndRecord {
    std::uint32_t signature = 0x06054b50;
    std::uint16_t disk = 0;
    std::uint16_t startDisk = 0;
    std::uint16_t entriesOnDisk = 0;
    std::uint16_t entries = 0;
    std::uint32_t centralSize = 0;
    std::uint32_t centralOffset = 0;
    std::uint16_t commentLength = 0;
};
#pragma pack(pop)

void WriteRaw(std::ofstream& out, const void* data, std::size_t size) {
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) {
        throw std::runtime_error("Failed writing backup zip");
    }
}

}  // namespace

void WriteZip(const std::filesystem::path& zipPath, const std::vector<ZipEntry>& entries) {
    std::ofstream out(zipPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Could not create backup zip");
    }

    struct Written {
        std::string name;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
    };
    std::vector<Written> written;

    for (const auto& entry : entries) {
        Written row;
        row.name = entry.name;
        row.crc = Crc32(entry.data.data(), entry.data.size());
        row.size = static_cast<std::uint32_t>(entry.data.size());
        row.offset = static_cast<std::uint32_t>(out.tellp());

        LocalHeader local;
        local.crc = row.crc;
        local.compressed = row.size;
        local.uncompressed = row.size;
        local.nameLength = static_cast<std::uint16_t>(row.name.size());
        WriteRaw(out, &local, sizeof(local));
        WriteRaw(out, row.name.data(), row.name.size());
        if (!entry.data.empty()) {
            WriteRaw(out, entry.data.data(), entry.data.size());
        }
        written.push_back(std::move(row));
    }

    const auto centralOffset = static_cast<std::uint32_t>(out.tellp());
    for (const auto& row : written) {
        CentralHeader central;
        central.crc = row.crc;
        central.compressed = row.size;
        central.uncompressed = row.size;
        central.nameLength = static_cast<std::uint16_t>(row.name.size());
        central.localOffset = row.offset;
        WriteRaw(out, &central, sizeof(central));
        WriteRaw(out, row.name.data(), row.name.size());
    }
    const auto centralSize = static_cast<std::uint32_t>(out.tellp()) - centralOffset;

    EndRecord end;
    end.entriesOnDisk = static_cast<std::uint16_t>(written.size());
    end.entries = end.entriesOnDisk;
    end.centralSize = centralSize;
    end.centralOffset = centralOffset;
    WriteRaw(out, &end, sizeof(end));
}

std::string ZipWorkspaceFolder(const std::filesystem::path& workspaceRoot, const std::filesystem::path& zipPath) {
    std::vector<ZipEntry> entries;
    if (std::filesystem::exists(workspaceRoot)) {
        for (const auto& item : std::filesystem::recursive_directory_iterator(workspaceRoot)) {
            if (!item.is_regular_file()) {
                continue;
            }
            auto rel = std::filesystem::relative(item.path(), workspaceRoot).generic_string();
            if (rel.empty() || rel[0] == '.') {
                continue;
            }
            ZipEntry entry;
            entry.name = rel;
            entry.data = ReadFileUtf8(item.path());
            entries.push_back(std::move(entry));
        }
    }
    WriteZip(zipPath, entries);
    return WideToUtf8(zipPath.wstring());
}

}  // namespace edity
