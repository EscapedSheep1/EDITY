#include "ftp/TransferClient.h"

#include "app/Utf8.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace edity {
namespace {

std::once_flag g_curlOnce;

void EnsureCurl() {
    std::call_once(g_curlOnce, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

struct CurlCleanup {
    void operator()(CURL* curl) const {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }
};

using CurlPtr = std::unique_ptr<CURL, CurlCleanup>;

std::size_t WriteString(char* ptr, std::size_t size, std::size_t nmemb, void* user) {
    auto* out = static_cast<std::string*>(user);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct UploadState {
    const char* data = nullptr;
    std::size_t remaining = 0;
};

std::size_t ReadUpload(char* buffer, std::size_t size, std::size_t nitems, void* user) {
    auto* state = static_cast<UploadState*>(user);
    const std::size_t room = size * nitems;
    const std::size_t take = room < state->remaining ? room : state->remaining;
    if (take > 0) {
        memcpy(buffer, state->data, take);
        state->data += take;
        state->remaining -= take;
    }
    return take;
}

std::string LastError(CURL*, CURLcode code) {
    const char* extra = curl_easy_strerror(code);
    return extra ? extra : "Unknown transfer error";
}

std::string EncodePath(CURL* curl, const std::string& path) {
    std::string out;
    if (path.empty()) {
        return out;
    }
    std::string current;
    const bool leading = path.front() == '/';
    auto flush = [&] {
        if (current.empty()) {
            return;
        }
        char* escaped = curl_easy_escape(curl, current.c_str(), static_cast<int>(current.size()));
        if (escaped) {
            if (!out.empty() && out.back() != '/') {
                out.push_back('/');
            }
            out += escaped;
            curl_free(escaped);
        }
        current.clear();
    };
    if (leading) {
        out.push_back('/');
    }
    for (char c : path) {
        if (c == '/') {
            flush();
            if (out.empty() || out.back() != '/') {
                out.push_back('/');
            }
        } else {
            current.push_back(c);
        }
    }
    flush();
    return out;
}

std::string NormalizeDir(std::string path) {
    while (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
        path.pop_back();
    }
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    return path;
}

std::string TrimCopy(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
    const auto start = line.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return {};
    }
    return line.substr(start);
}

std::string BasenameOnly(std::string name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    return name;
}

bool LooksJson(const std::string& name) {
    return name.size() > 5 && IEquals(name.substr(name.size() - 5), ".json");
}

bool LooksLikeListMetadata(const std::string& name) {
    if (name.find("rwx") != std::string::npos || name.find("<DIR>") != std::string::npos ||
        name.find("<dir>") != std::string::npos) {
        return true;
    }
    if (name.size() >= 8 && std::isdigit(static_cast<unsigned char>(name[0])) &&
        (name[2] == '-' || name[2] == '/')) {
        return true;
    }
    return name.size() > 3 && (name[0] == '-' || name[0] == 'd') && name[1] == 'r';
}

bool IsWindowsListLine(const std::string& line) {
    if (line.size() < 8 || !std::isdigit(static_cast<unsigned char>(line[0]))) {
        return false;
    }
    return line[2] == '-' || line[2] == '/';
}

std::size_t SkipToken(const std::string& line, std::size_t i) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
        ++i;
    }
    return i;
}

std::string JoinRemote(std::string dir, const std::string& name) {
    dir = NormalizeDir(std::move(dir));
    if (dir.empty() || dir == ".") {
        return name;
    }
    if (dir.back() != '/') {
        dir.push_back('/');
    }
    return dir + name;
}

struct ParsedEntry {
    std::string name;
    bool isDir = false;
    bool ok = false;
};

ParsedEntry ParseListLine(std::string line) {
    line = TrimCopy(std::move(line));
    ParsedEntry out;
    if (line.empty() || line == "." || line == "..") {
        return out;
    }
    if (line.back() == ':') {
        return out;
    }

    const auto lower = ToLowerAscii(line);
    if (lower.find("type=") != std::string::npos && line.find(';') != std::string::npos) {
        if (lower.find("type=cdir") != std::string::npos || lower.find("type=pdir") != std::string::npos) {
            return out;
        }
        out.isDir = lower.find("type=dir") != std::string::npos;
        auto sep = line.find("; ");
        std::string name;
        if (sep != std::string::npos) {
            name = TrimCopy(line.substr(sep + 2));
        } else {
            const auto last = line.find_last_of(';');
            name = last == std::string::npos ? line : TrimCopy(line.substr(last + 1));
        }
        out.name = BasenameOnly(name);
        out.ok = !out.name.empty() && out.name != "." && out.name != "..";
        return out;
    }

    if (lower.find("<dir>") != std::string::npos) {
        auto pos = lower.find("<dir>");
        out.name = BasenameOnly(TrimCopy(line.substr(pos + 5)));
        out.isDir = true;
        out.ok = !out.name.empty();
        return out;
    }

    if (IsWindowsListLine(line)) {
        std::size_t i = SkipToken(line, 0);
        i = SkipToken(line, i);
        if (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            const auto afterTime = TrimCopy(line.substr(i));
            const auto afterLower = ToLowerAscii(afterTime);
            if (afterLower.rfind("am", 0) == 0 || afterLower.rfind("pm", 0) == 0) {
                i = SkipToken(line, i);
            }
        }
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
            i = SkipToken(line, i);
        }
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        out.name = BasenameOnly(TrimCopy(i < line.size() ? line.substr(i) : std::string()));
        out.isDir = false;
        out.ok = !out.name.empty() && !LooksLikeListMetadata(out.name);
        return out;
    }

    if (!line.empty() && (line[0] == 'd' || line[0] == '-' || line[0] == 'l')) {
        out.isDir = line[0] == 'd';
        std::size_t i = 0;
        int fields = 0;
        while (i < line.size() && fields < 8) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
                ++i;
            }
            if (i >= line.size()) {
                break;
            }
            while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
                ++i;
            }
            ++fields;
        }
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i < line.size()) {
            auto name = line.substr(i);
            const auto arrow = name.find(" -> ");
            if (arrow != std::string::npos) {
                name = name.substr(0, arrow);
                out.isDir = true;
            }
            out.name = BasenameOnly(TrimCopy(name));
            out.ok = !out.name.empty();
        }
        return out;
    }

    out.name = BasenameOnly(line);
    out.isDir = !LooksJson(out.name);
    out.ok = !out.name.empty() && !LooksLikeListMetadata(out.name);
    return out;
}

}  // namespace

TransferClient::TransferClient(Config config) : config_(std::move(config)) {
    EnsureCurl();
    if (config_.port <= 0) {
        config_.port = config_.protocol == Protocol::Sftp ? 22 : 21;
    }
}

void TransferClient::SetProgress(ProgressFn fn) {
    progress_ = std::move(fn);
}

void TransferClient::Report(const std::string& message, int percent) const {
    std::lock_guard lock(progressMutex_);
    if (progress_) {
        progress_({message, percent});
    }
}

std::string TransferClient::Scheme() const {
    switch (config_.protocol) {
        case Protocol::Ftps:
            return "ftp";
        case Protocol::Sftp:
            return "sftp";
        case Protocol::Ftp:
        default:
            return "ftp";
    }
}

std::string TransferClient::FileUrl(const std::string& remoteDir, const std::string& filename) const {
    CurlPtr curl(curl_easy_init());
    const auto dir = EncodePath(curl.get(), NormalizeDir(remoteDir));
    const auto file = EncodePath(curl.get(), filename);
    std::ostringstream oss;
    oss << Scheme() << "://" << config_.host << ":" << config_.port;
    if (!dir.empty() && dir.front() != '/') {
        oss << '/';
    }
    oss << dir;
    if (!dir.empty() && dir.back() != '/') {
        oss << '/';
    }
    oss << file;
    return oss.str();
}

std::string TransferClient::DirUrl(const std::string& remoteDir) const {
    return FileUrl(remoteDir, "");
}

void ApplyAuth(CURL* curl, const TransferClient::Config& config) {
    curl_easy_setopt(curl, CURLOPT_USERNAME, config.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, config.password.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 256L * 1024L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);
    if (config.protocol == Protocol::Ftps) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_DEFAULT);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (config.protocol != Protocol::Sftp) {
        // Windows FTP hosts (common for DayZ) often fail EPSV, then retry PASV.
        curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);
        curl_easy_setopt(curl, CURLOPT_FTP_SKIP_PASV_IP, 1L);
        curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, (long)CURLFTPMETHOD_SINGLECWD);
        if (!config.passive) {
            curl_easy_setopt(curl, CURLOPT_FTPPORT, "-");
        }
    }
}

std::string TransferClient::TestConnection() {
    Report("Testing connection...", 5);
    CurlPtr curl(curl_easy_init());
    if (!curl) {
        throw std::runtime_error("Could not start transfer engine");
    }
    const auto url = DirUrl(config_.protocol == Protocol::Sftp ? "." : "/");
    std::string body;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    ApplyAuth(curl.get(), config_);
    curl_easy_setopt(curl.get(), CURLOPT_DIRLISTONLY, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteString);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        throw std::runtime_error(LastError(curl.get(), code));
    }
    Report("Connection OK", 100);
    return "ok";
}

std::vector<TransferClient::RemoteEntry> TransferClient::ListDirectory(const std::string& remoteDir) {
    Report("Listing " + (remoteDir.empty() ? "/" : remoteDir), 10);
    CurlPtr curl(curl_easy_init());
    if (!curl) {
        throw std::runtime_error("Could not start transfer engine");
    }
    std::string body;
    const auto url = DirUrl(remoteDir);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    ApplyAuth(curl.get(), config_);
    curl_easy_setopt(curl.get(), CURLOPT_DIRLISTONLY, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteString);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        body.clear();
        curl_easy_setopt(curl.get(), CURLOPT_DIRLISTONLY, 1L);
        code = curl_easy_perform(curl.get());
        if (code != CURLE_OK) {
            throw std::runtime_error("Could not list " + remoteDir + ": " + LastError(curl.get(), code));
        }
    }

    std::vector<RemoteEntry> entries;
    std::string line;
    std::istringstream stream(body);
    while (std::getline(stream, line)) {
        const auto parsed = ParseListLine(line);
        if (!parsed.ok || parsed.name.empty()) {
            continue;
        }
        RemoteEntry entry;
        entry.name = TrimCopy(parsed.name);
        if (LooksLikeListMetadata(entry.name)) {
            continue;
        }
        entry.path = JoinRemote(remoteDir, entry.name);
        entry.isDir = parsed.isDir && !LooksJson(parsed.name);
        entry.isJson = LooksJson(parsed.name) && !LooksLikeListMetadata(parsed.name);
        if (entry.isJson) {
            entry.isDir = false;
        }
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const RemoteEntry& a, const RemoteEntry& b) {
        if (a.isDir != b.isDir) {
            return a.isDir && !b.isDir;
        }
        return ToLowerAscii(a.name) < ToLowerAscii(b.name);
    });
    return entries;
}

std::vector<std::string> TransferClient::ListJsonFiles(const std::string& remoteDir) {
    std::vector<std::string> files;
    for (const auto& entry : ListDirectory(remoteDir)) {
        if (entry.isJson && !entry.isDir) {
            files.push_back(entry.name);
        }
    }
    return files;
}

namespace {

bool RetryableDownload(CURLcode code) {
    switch (code) {
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
        case CURLE_FTP_ACCEPT_FAILED:
        case CURLE_FTP_ACCEPT_TIMEOUT:
        case CURLE_FTP_WEIRD_PASV_REPLY:
        case CURLE_FTP_CANT_GET_HOST:
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_QUOTE_ERROR:
        case CURLE_REMOTE_ACCESS_DENIED:
            return true;
        default:
            return false;
    }
}

struct MultiCleanup {
    void operator()(CURLM* multi) const {
        if (multi) {
            curl_multi_cleanup(multi);
        }
    }
};

using MultiPtr = std::unique_ptr<CURLM, MultiCleanup>;

struct DownloadJob {
    std::string remoteDir;
    std::string filename;
    std::string url;
    std::string body;
    CURLcode code = CURLE_OK;
    std::string error;
};

struct DownloadSlot {
    CurlPtr easy;
    DownloadJob* job = nullptr;
};

void ArmDownload(CURL* easy, const TransferClient::Config& config, DownloadJob& job) {
    job.body.clear();
    job.code = CURLE_OK;
    job.error.clear();
    curl_easy_reset(easy);
    curl_easy_setopt(easy, CURLOPT_URL, job.url.c_str());
    ApplyAuth(easy, config);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteString);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &job.body);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, &job);
}

}  // namespace

std::string TransferClient::DownloadFile(const std::string& remoteDir, const std::string& filename) {
    const auto results = DownloadFiles({{remoteDir, filename}});
    if (results.empty()) {
        throw std::runtime_error("Download failed for " + filename);
    }
    if (!results.front().error.empty()) {
        throw std::runtime_error(results.front().error);
    }
    return results.front().contents;
}

std::vector<TransferClient::DownloadResult> TransferClient::DownloadFiles(
    const std::vector<DownloadRequest>& requests) {
    std::vector<DownloadJob> jobs;
    jobs.reserve(requests.size());
    for (const auto& request : requests) {
        if (request.filename.empty() || LooksLikeListMetadata(request.filename) ||
            request.filename.find("  ") != std::string::npos) {
            DownloadJob job;
            job.remoteDir = request.remoteDir;
            job.filename = request.filename;
            job.error = "Skipped invalid remote name: " + request.filename;
            jobs.push_back(std::move(job));
            continue;
        }
        DownloadJob job;
        job.remoteDir = request.remoteDir;
        job.filename = request.filename;
        job.url = FileUrl(request.remoteDir, request.filename);
        jobs.push_back(std::move(job));
    }

    const auto runBatch = [&](int maxParallel) {
        std::vector<DownloadJob*> pending;
        pending.reserve(jobs.size());
        for (auto& job : jobs) {
            if (job.error.empty() && job.body.empty()) {
                pending.push_back(&job);
            }
        }
        if (pending.empty()) {
            return;
        }

        MultiPtr multi(curl_multi_init());
        if (!multi) {
            throw std::runtime_error("Could not start transfer pool");
        }
        const long cap = static_cast<long>(std::max(1, maxParallel));
        curl_multi_setopt(multi.get(), CURLMOPT_MAX_TOTAL_CONNECTIONS, cap);
        curl_multi_setopt(multi.get(), CURLMOPT_MAX_HOST_CONNECTIONS, cap);

        const int slotCount = static_cast<int>(std::min(pending.size(), static_cast<std::size_t>(maxParallel)));
        std::vector<DownloadSlot> slots(static_cast<std::size_t>(slotCount));
        for (auto& slot : slots) {
            slot.easy.reset(curl_easy_init());
            if (!slot.easy) {
                throw std::runtime_error("Could not start transfer engine");
            }
        }

        std::size_t next = 0;
        std::size_t finished = 0;
        const std::size_t total = pending.size();

        auto startNext = [&](DownloadSlot& slot) {
            if (next >= pending.size()) {
                slot.job = nullptr;
                return false;
            }
            slot.job = pending[next++];
            ArmDownload(slot.easy.get(), config_, *slot.job);
            curl_multi_add_handle(multi.get(), slot.easy.get());
            return true;
        };

        for (auto& slot : slots) {
            startNext(slot);
        }

        int running = 0;
        curl_multi_perform(multi.get(), &running);
        while (running > 0 || next < pending.size()) {
            int waitFds = 0;
            curl_multi_wait(multi.get(), nullptr, 0, 1000, &waitFds);
            curl_multi_perform(multi.get(), &running);

            int queued = 0;
            while (CURLMsg* msg = curl_multi_info_read(multi.get(), &queued)) {
                if (msg->msg != CURLMSG_DONE) {
                    continue;
                }
                char* priv = nullptr;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &priv);
                auto* job = reinterpret_cast<DownloadJob*>(priv);
                if (job) {
                    job->code = msg->data.result;
                    if (job->code != CURLE_OK) {
                        job->error = "Download failed for " + job->filename + ": " +
                                     LastError(msg->easy_handle, job->code);
                        job->body.clear();
                    } else if (job->body.empty()) {
                        job->error = "Download of " + job->filename + " was empty";
                    }
                }
                curl_multi_remove_handle(multi.get(), msg->easy_handle);
                ++finished;
                Report("Pulled " + (job ? job->filename : std::string("file")),
                       std::min(99, 12 + static_cast<int>(finished * 86 / std::max<std::size_t>(1, total))));

                for (auto& slot : slots) {
                    if (slot.easy.get() == msg->easy_handle) {
                        startNext(slot);
                        break;
                    }
                }
            }
            curl_multi_perform(multi.get(), &running);
        }
    };

    const int parallel = config_.protocol == Protocol::Sftp ? 8 : 6;
    Report("Downloading " + std::to_string(jobs.size()) + " files...", 12);
    runBatch(parallel);

    std::vector<DownloadJob*> retry;
    int okCount = 0;
    for (auto& job : jobs) {
        if (job.error.empty() && !job.body.empty()) {
            ++okCount;
        } else if (!job.url.empty() &&
                   (RetryableDownload(job.code) || (okCount > 0 && !job.error.empty()))) {
            job.error.clear();
            job.body.clear();
            retry.push_back(&job);
        }
    }
    if (!retry.empty()) {
        Report("Retrying " + std::to_string(retry.size()) + " files...", 90);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        runBatch(2);
        retry.clear();
        for (auto& job : jobs) {
            if (!job.url.empty() && job.error.empty() && !job.body.empty()) {
                continue;
            }
            if (!job.url.empty() && !job.error.empty()) {
                job.error.clear();
                job.body.clear();
                retry.push_back(&job);
            }
        }
        if (!retry.empty()) {
            runBatch(1);
        }
    }

    std::vector<DownloadResult> results;
    results.reserve(jobs.size());
    for (auto& job : jobs) {
        DownloadResult row;
        row.remoteDir = std::move(job.remoteDir);
        row.filename = std::move(job.filename);
        row.contents = std::move(job.body);
        row.error = std::move(job.error);
        results.push_back(std::move(row));
    }
    return results;
}

void TransferClient::UploadFile(const std::string& remoteDir, const std::string& filename, const std::string& contents) {
    Report("Uploading " + filename);
    CurlPtr curl(curl_easy_init());
    if (!curl) {
        throw std::runtime_error("Could not start transfer engine");
    }
    const auto url = FileUrl(remoteDir, filename);
    UploadState state{contents.data(), contents.size()};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    ApplyAuth(curl.get(), config_);
    curl_easy_setopt(curl.get(), CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_READFUNCTION, ReadUpload);
    curl_easy_setopt(curl.get(), CURLOPT_READDATA, &state);
    curl_easy_setopt(curl.get(), CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(contents.size()));
    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        throw std::runtime_error("Upload failed for " + filename + ": " + LastError(curl.get(), code));
    }
}

void TransferClient::DeleteRemoteFile(const std::string& remoteDir, const std::string& filename) {
    Report("Deleting remote " + filename);
    CurlPtr curl(curl_easy_init());
    if (!curl) {
        throw std::runtime_error("Could not start transfer engine");
    }
    const auto url = FileUrl(remoteDir, filename);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    ApplyAuth(curl.get(), config_);
    if (config_.protocol == Protocol::Sftp) {
        curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "rm");
    } else {
        const std::string quote = "DELE " + filename;
        curl_slist* cmds = curl_slist_append(nullptr, quote.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_QUOTE, cmds);
        curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L);
        const CURLcode code = curl_easy_perform(curl.get());
        curl_slist_free_all(cmds);
        if (code != CURLE_OK) {
            throw std::runtime_error("Delete failed for " + filename + ": " + LastError(curl.get(), code));
        }
        return;
    }
    const CURLcode code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        throw std::runtime_error("Delete failed for " + filename + ": " + LastError(curl.get(), code));
    }
}

}  // namespace edity
