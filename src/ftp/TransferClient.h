#pragma once

#include "market/Models.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace edity {

struct TransferProgress {
    std::string message;
    int percent = -1;
};

class TransferClient {
public:
    using ProgressFn = std::function<void(const TransferProgress&)>;

    struct Config {
        Protocol protocol = Protocol::Sftp;
        std::string host;
        int port = 22;
        std::string username;
        std::string password;
        bool passive = true;
    };

    explicit TransferClient(Config config);

    struct RemoteEntry {
        std::string name;
        std::string path;
        bool isDir = false;
        bool isJson = false;
    };

    void SetProgress(ProgressFn fn);
    void Report(const std::string& message, int percent = -1) const;
    std::string TestConnection();
    struct DownloadRequest {
        std::string remoteDir;
        std::string filename;
    };

    struct DownloadResult {
        std::string remoteDir;
        std::string filename;
        std::string contents;
        std::string error;
    };

    std::vector<RemoteEntry> ListDirectory(const std::string& remoteDir);
    std::vector<std::string> ListJsonFiles(const std::string& remoteDir);
    std::string DownloadFile(const std::string& remoteDir, const std::string& filename);
    std::vector<DownloadResult> DownloadFiles(const std::vector<DownloadRequest>& requests);
    void UploadFile(const std::string& remoteDir, const std::string& filename, const std::string& contents);
    void DeleteRemoteFile(const std::string& remoteDir, const std::string& filename);

private:
    Config config_;
    ProgressFn progress_;
    mutable std::mutex progressMutex_;

    std::string Scheme() const;
    std::string FileUrl(const std::string& remoteDir, const std::string& filename) const;
    std::string DirUrl(const std::string& remoteDir) const;
};

}  // namespace edity
