// Created by yangb on 2026/11/3.
#include "file/UploadReactor.h"
#include "file/UploadHttpParser.h"
#include "common/Logger.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <limits>
#include <sodium.h>
#include <stdexcept>
#include <unistd.h>

using backend::logging::Level;
using backend::logging::Logger;

namespace {

void deleteTemporaryUpload(const std::string& path) noexcept {
    int result;
    do { result = ::unlink(path.c_str()); } while (result == -1 && errno == EINTR);
    if (result == -1 && errno != ENOENT) {
        Logger::write(Level::Warn, "UploadReactor", "upload_temp_cleanup_failed", {{"errno", errno}});
    }
}

struct TemporaryUpload {
    DiskPool& pool;
    std::string path;
    std::shared_ptr<UniqueFd> file;

    ~TemporaryUpload() {
        // 只有 OpenTempFile 独占创建成功后才取得清理责任。
        if (!file) return;
        file.reset();
        try {
            if (pool.receiveTask([path = path] { deleteTemporaryUpload(path); })) return;
        } catch (const std::exception&) {
            // 分配或入队失败时仍尝试清理。
        }
        deleteTemporaryUpload(path);
    }
};

}

UploadReactor::UploadReactor(int port, DiskPool& disk_pool, AuthMiddleWare& auth_middle_ware, FileService& file_service,
    std::function<void(Task)> submit_business)
    : disk_pool_(disk_pool), port_(port), auth_middle_ware_(auth_middle_ware),
      file_service_(file_service), submit_business_(std::move(submit_business)) {
    if (port_ <= 0 || port_ > 65535) throw std::invalid_argument("invalid upload port");
    init();
}

UploadReactor::~UploadReactor() {
    // 所有者必须先让 run() 返回；不能与仍在运行的 Reactor 线程并发销毁对象。
    close();
    while (!sessionMap.empty()) removeSession(sessionMap.begin()->first);
    incoming_connection_listen_fd_.reset();
    if (disk_task_completion_event_fd_ != -1) ::close(disk_task_completion_event_fd_);
    if (epoll_fd_ != -1) ::close(epoll_fd_);
}

bool UploadReactor::init() {
    if (epoll_fd_ != -1) return true;
    // 先创建 epoll fd，才能注册监听 socket fd；临时 RAII 对象负责初始化失败时关闭。
    UniqueFd epoll{::epoll_create1(EPOLL_CLOEXEC)};
    if (!epoll.valid()) throw std::system_error(errno, std::generic_category(), "create upload epoll");
    UniqueFd notification{::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)};
    if (!notification.valid()) throw std::system_error(errno, std::generic_category(), "create upload eventfd");
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = notification.get();
    if (::epoll_ctl(epoll.get(), EPOLL_CTL_ADD, notification.get(), &event) == -1) throw std::system_error(errno, std::generic_category(), "register upload eventfd");
    epoll_fd_ = epoll.get();
    try {
        createListenSocket();
    } catch (...) {
        epoll_fd_ = -1;
        throw;
    }
    epoll_fd_ = epoll.release();
    disk_task_completion_event_fd_ = notification.release();
    running_.store(true);
    return true;
}

void UploadReactor::close() {
    running_.store(false);
    // eventfd 唤醒 epoll_wait，不在这里跨线程删除 Session 或关闭连接 socket fd。
    if (disk_task_completion_event_fd_ == -1) return;
    const std::uint64_t value = 1;
    ssize_t written;
    do { written = ::write(disk_task_completion_event_fd_, &value, sizeof(value)); } while (written == -1 && errno == EINTR);
    // EAGAIN 表示 eventfd 已有待处理通知，不需要继续写入。
}

void UploadReactor::createListenSocket() {
    UniqueFd listener{::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    if (!listener.valid()) throw std::system_error(errno, std::generic_category(), "create upload listener");
    int reuse = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) throw std::system_error(errno, std::generic_category(), "upload SO_REUSEADDR");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port_));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) throw std::system_error(errno, std::generic_category(), "bind upload 127.0.0.1:" + std::to_string(port_));
    if (::listen(listener.get(), 128) == -1) throw std::system_error(errno, std::generic_category(), "listen upload");
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listener.get();
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listener.get(), &event) == -1) throw std::system_error(errno, std::generic_category(), "register upload listener");
    incoming_connection_listen_fd_ = std::move(listener);
}

void UploadReactor::listen_fd_connection() {
    // 监听 socket fd 可读表示有连接等待 accept，不是客户端文件正文到达。
    // 每轮限制 accept 数量，避免连接持续涌入时饿死已有连接的读写事件。
    for (int accepted = 0; accepted < 32 && running_.load(); ++accepted) {
        UniqueFd client{::accept4(incoming_connection_listen_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)};
        if (!client.valid()) {
            const int error = errno;
            if (error == EINTR) continue;
            if (error == EAGAIN || error == EWOULDBLOCK) return;
            throw std::system_error(error, std::generic_category(), "accept upload connection");
        }
        if (sessionMap.size() >= 128) continue; // 限制未完成连接数量，client 自动关闭被拒绝的连接 socket fd。
        const int connection_socket_fd = client.get();
        UploadSession session;
        session.generation = next_generation_++;
        session.socket = std::move(client);
        auto [iterator, inserted] = sessionMap.try_emplace(connection_socket_fd, std::move(session));
        if (!inserted) throw std::logic_error("upload connection socket fd already exists");
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.fd = connection_socket_fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, connection_socket_fd, &event) == -1) {
            const int error = errno;
            sessionMap.erase(iterator); // UniqueFd 关闭连接 socket fd，不能再次手动 close。
            throw std::system_error(error, std::generic_category(), "register upload connection");
        }
        Logger::write(Level::Info, "UploadReactor", "connection_accepted",
            {{"fd", connection_socket_fd}, {"generation", iterator->second.generation}});
    }
}

void UploadReactor::readRequestHeaders(int connection_socket_fd) {
    auto iterator = sessionMap.find(connection_socket_fd);
    if (iterator == sessionMap.end() || iterator->second.http_state != UploadHttpState::ReadingHeaders) return;
    auto& session = iterator->second;
    std::array<char, 4096> buffer{};
    while (true) {
        // 只读连接 socket fd；此处没有打开文件 fd，也不会向磁盘写入。
        const ssize_t received = ::recv(connection_socket_fd, buffer.data(), buffer.size(), 0);
        if (received == -1) {
            const int error = errno;
            if (error == EINTR) continue;
            if (error == EAGAIN || error == EWOULDBLOCK) return;
            Logger::write(Level::Warn, "UploadReactor", "header_read_failed",
                {{"fd", connection_socket_fd}, {"generation", session.generation}, {"errno", error}});
            removeSession(connection_socket_fd);
            return;
        }
        if (received == 0) {
            queueResponse(connection_socket_fd, 400, "incomplete_upload_headers");
            return;
        }
        session.header_buffer.append(buffer.data(), static_cast<std::size_t>(received));
        auto parsed = UploadHttpParser::parse(session.header_buffer);
        if (parsed.status == UploadHeaderStatus::NeedMoreData) continue;
        if (parsed.status == UploadHeaderStatus::Error) {
            queueResponse(connection_socket_fd, parsed.http_status, parsed.error_code);
            return;
        }

        // 请求头和正文可能同次到达。只把请求头交给解析器，保留尾部正文，不把正文当成 HTTP 头。
        const auto body_bytes = session.header_buffer.size() - parsed.header_bytes;
        const char* body_begin = session.header_buffer.data() + parsed.header_bytes;
        session.pending_body.assign(body_begin, body_begin + body_bytes);
        session.received_bytes = body_bytes;
        session.declared_bytes = parsed.content_length;
        session.upload_request = std::move(parsed.request);
        session.header_buffer.clear();
        Logger::write(Level::Info, "UploadReactor", "headers_parsed",
            {{"fd", connection_socket_fd}, {"generation", session.generation},
             {"declared_bytes", session.declared_bytes}, {"buffered_body_bytes", body_bytes}});
        dispatchRequest(connection_socket_fd, session);
        return;
    }
}

void UploadReactor::dispatchRequest(int connection_socket_fd, UploadSession& session) {
    const auto& request = session.upload_request;

    // 先按路径和方法分派，再检查这条路由要求的请求头。
    if (request.path != "/upload_file") {
        queueResponse(connection_socket_fd, 404, "route_not_found");
        return;
    }
    if (request.method != HttpMethod::PUT) {
        queueResponse(connection_socket_fd, 405, "upload_method_not_allowed");
        return;
    }

    const auto content_type = request.headers.find("content-type");
    if (content_type == request.headers.end() || content_type->second != "application/octet-stream") {
        queueResponse(connection_socket_fd, 415, "invalid_upload_content_type");
        return;
    }
    if (!request.headers.contains("content-length")) {
        queueResponse(connection_socket_fd, 411, "missing_upload_content_length");
        return;
    }
    if (session.received_bytes > session.declared_bytes) {
        queueResponse(connection_socket_fd, 400, "upload_body_exceeds_content_length");
        return;
    }

    //在这里的时候才进入解析
    session.http_state = UploadHttpState::WaitingAuthorization;

   handleVerificationResult(connection_socket_fd, requestUploadAuthorization(connection_socket_fd));
}

void UploadReactor::handleVerificationResult(int connection_socket_fd, VerifyPermissionResult result) {
    switch (result) {
        case VerifyPermissionResult::Allowed: {
            const auto iterator = sessionMap.find(connection_socket_fd);
            if (iterator == sessionMap.end()) return;
            Logger::write(Level::Info, "UploadReactor", "permission_allowed",
                {{"fd", connection_socket_fd}, {"generation", iterator->second.generation}});
            auto& session = iterator->second;
            session.expected_bytes = session.declared_bytes;
            if (!createFile(session)) queueResponse(connection_socket_fd, 503, "upload_disk_busy");
            return;
        }
        case VerifyPermissionResult::Pending:
            // 验证任务已入队，保留连接以接收最终结果。
            Logger::write(Level::Info, "UploadReactor", "verification_queued", {{"fd", connection_socket_fd}});
            return;
        case VerifyPermissionResult::ConnectionClosed:
            if (sessionMap.contains(connection_socket_fd)) removeSession(connection_socket_fd);
            return;
        case VerifyPermissionResult::AccessTokenExpired:
            return queueResponse(connection_socket_fd, 401, "access_token_expired");
        case VerifyPermissionResult::InvalidAccessToken:
            return queueResponse(connection_socket_fd, 401, "invalid_access_token");
        case VerifyPermissionResult::MissingUploadId:
            return queueResponse(connection_socket_fd, 400, "missing_upload_id");
        case VerifyPermissionResult::InvalidUploadId:
            return queueResponse(connection_socket_fd, 400, "invalid_upload_id");
        case VerifyPermissionResult::MissingUploadToken:
            return queueResponse(connection_socket_fd, 400, "missing_upload_token");
        case VerifyPermissionResult::InvalidUploadTokenFormat:
            return queueResponse(connection_socket_fd, 400, "invalid_upload_token_format");
        case VerifyPermissionResult::InvalidContentLength:
            return queueResponse(connection_socket_fd, 400, "invalid_upload_content_length");
        case VerifyPermissionResult::ContentLengthMismatch:
            return queueResponse(connection_socket_fd, 400, "upload_content_length_mismatch");
        case VerifyPermissionResult::NotFound:
        case VerifyPermissionResult::Unauthorized:
            return queueResponse(connection_socket_fd, 403, "upload_permission_denied");
        case VerifyPermissionResult::NotImplemented:
            return queueResponse(connection_socket_fd, 503, "upload_authorization_not_implemented");
        case VerifyPermissionResult::QueueUnavailable:
            return queueResponse(connection_socket_fd, 503, "upload_verification_unavailable");
        case VerifyPermissionResult::VerificationFailed:
            return queueResponse(connection_socket_fd, 500, "upload_verification_failed");
        case VerifyPermissionResult::DigestMismatch:
            return queueResponse(connection_socket_fd, 400, "upload_digest_mismatch");
    }
    removeSession(connection_socket_fd);
}

VerifyPermissionResult UploadReactor::requestUploadAuthorization(int connection_socket_fd) {
    const auto it = sessionMap.find(connection_socket_fd);
    if (it == sessionMap.end()) {
        return VerifyPermissionResult::ConnectionClosed;
    }

    const auto auth = auth_middle_ware_.authenticate(it->second.upload_request);
    if (const auto* error = std::get_if<AuthError>(&auth.result)) {
        return *error == AuthError::Expired ? VerifyPermissionResult::AccessTokenExpired : VerifyPermissionResult::InvalidAccessToken;
    }

    const auto& headers = it->second.upload_request.headers;
    const auto upload_id_it = headers.find("x-upload-id");
    if (upload_id_it == headers.end() || upload_id_it->second.empty()) {
        return VerifyPermissionResult::MissingUploadId;
    }

    boost::uuids::uuid upload_id{};
    try {
        upload_id = boost::uuids::string_generator{}(upload_id_it->second);
    } catch (const std::runtime_error&) {
        return VerifyPermissionResult::InvalidUploadId;
    }
    if (upload_id.is_nil()) {
        return VerifyPermissionResult::InvalidUploadId;
    }

    const auto upload_token_it = headers.find("x-upload-token");
    if (upload_token_it == headers.end() || upload_token_it->second.empty()) {
        return VerifyPermissionResult::MissingUploadToken;
    }
    const auto& upload_token = upload_token_it->second;
    if (upload_token.size() != 64 || upload_token.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return VerifyPermissionResult::InvalidUploadTokenFormat;
    }

    const auto content_length = it->second.declared_bytes;
    if (content_length == 0 || content_length > std::numeric_limits<std::int64_t>::max()) {
        return VerifyPermissionResult::InvalidContentLength;
    }



    // 暂停连接 socket fd 的读事件，等待验证结果通过 eventfd 回投。
    epoll_event event{};
    event.data.fd = connection_socket_fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection_socket_fd, &event) == -1) {
        Logger::write(Level::Warn, "UploadReactor", "pause_read_failed",
            {{"fd", connection_socket_fd}, {"generation", it->second.generation}, {"errno", errno}});
        return VerifyPermissionResult::QueueUnavailable;
    }
    const auto& user_id = std::get<boost::uuids::uuid>(auth.result);
    const WriteTaskIdentity identity{connection_socket_fd, it->second.generation};
    it->second.upload_id = upload_id;
    return submitVerificationRequest(identity, user_id, upload_id, upload_token, content_length)
        ? VerifyPermissionResult::Pending : VerifyPermissionResult::QueueUnavailable;
}

void UploadReactor::queueResponse(int connection_socket_fd, int status, const std::string& code) {
    auto iterator = sessionMap.find(connection_socket_fd);
    if (iterator == sessionMap.end()) return;
    auto& session = iterator->second;
    if (status >= 400) {
        if (session.claim) failClaim(session.claim->upload_id, code);
        session.claim.reset();
        session.file.reset();
    }
    nlohmann::json message;
    if (status == 200) {
        message = {{"success", true}, {"code", code}};
        if (session.claim) {
            message["upload_id"] = boost::uuids::to_string(session.claim->upload_id);
            message["attachment_id"] = boost::uuids::to_string(session.claim->attachment_id);
            message["size_bytes"] = session.written_bytes;
            message["upload_state"] = "completed";
            message["attachment_state"] = "ready";
        }
    } else {
        message = {{"error", {{"code", code}}}};
    }
    const auto body = message.dump();
    session.response_buffer = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK" : " Upload Error") +
        "\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    if (status == 405) session.response_buffer += "Allow: PUT\r\n";
    session.response_buffer += "\r\n" + body;
    session.response_sent = 0;
    session.http_state = UploadHttpState::SendingResponse;
    Logger::write(Level::Info, "UploadReactor", "response_queued",
        {{"fd", connection_socket_fd}, {"generation", session.generation}, {"status", status}, {"response_code", std::string_view{code}}});
    epoll_event event{};
    event.events = EPOLLOUT; // 停止读取正文，只等待错误响应发完。
    event.data.fd = connection_socket_fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection_socket_fd, &event) == -1) {
        Logger::write(Level::Warn, "UploadReactor", "response_epoll_failed",
            {{"fd", connection_socket_fd}, {"generation", session.generation}, {"errno", errno}});
        removeSession(connection_socket_fd);
        return;
    }
    flushResponse(connection_socket_fd);
}

void UploadReactor::flushResponse(int connection_socket_fd) {
    auto iterator = sessionMap.find(connection_socket_fd);
    if (iterator == sessionMap.end() || iterator->second.http_state != UploadHttpState::SendingResponse) return;
    auto& session = iterator->second;
    while (session.response_sent < session.response_buffer.size()) {
        const ssize_t sent = ::send(connection_socket_fd, session.response_buffer.data() + session.response_sent, session.response_buffer.size() - session.response_sent, MSG_NOSIGNAL);
        if (sent == -1 && errno == EINTR) continue;
        if (sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (sent <= 0) {
            Logger::write(Level::Warn, "UploadReactor", "response_send_failed",
                {{"fd", connection_socket_fd}, {"generation", session.generation}, {"errno", sent < 0 ? errno : 0}});
            break;
        }
        session.response_sent += static_cast<std::size_t>(sent);
    }
    if (session.response_sent == session.response_buffer.size()) {
        Logger::write(Level::Info, "UploadReactor", "response_sent",
            {{"fd", connection_socket_fd}, {"generation", session.generation}, {"bytes", session.response_sent}});
    }
    removeSession(connection_socket_fd);
}

void UploadReactor::removeSession(int connection_socket_fd) {
    const auto it = sessionMap.find(connection_socket_fd);
    if (it == sessionMap.end()) return;
    if (it->second.claim && it->second.http_state != UploadHttpState::Finalizing) {
        failClaim(it->second.claim->upload_id, "upload_connection_closed");
    }
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, connection_socket_fd, nullptr);
    sessionMap.erase(it);
}

void UploadReactor::run() {
    try {
        work();
    } catch (...) {
        while (!sessionMap.empty()) removeSession(sessionMap.begin()->first);
        throw;
    }
    while (!sessionMap.empty()) removeSession(sessionMap.begin()->first);
}

void UploadReactor::settleStoppedUploads() {
    // Reactor 和线程池均已停止，处理尚未回到 Session 的认领。
    while (!verification_queue_->completions.empty()) {
        auto completion = std::move(verification_queue_->completions.front());
        verification_queue_->completions.pop();
        if (!completion.claim || (completion.finalized && completion.result == VerifyPermissionResult::Allowed)) continue;
        try {
            file_service_.failUpload(completion.claim->upload_id, "upload_connection_closed");
        } catch (const std::exception&) {
            Logger::write(Level::Warn, "UploadReactor", "upload_shutdown_cleanup_failed");
        }
    }
}

bool UploadReactor::watchBody(int fd, bool enabled) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = enabled ? EPOLLIN | EPOLLRDHUP : 0;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) == 0) return true;
    removeSession(fd);
    return false;
}

void UploadReactor::verifyReceivedBody(int fd, UploadSession& session, bool timed_out) {
    if (session.received_bytes != session.expected_bytes) {
        queueResponse(fd, timed_out ? 408 : 400, timed_out ? "upload_body_timeout" : "incomplete_upload_body");
        return;
    }
    if (session.written_bytes != session.expected_bytes || session.write_in_progress) {
        queueResponse(fd, 500, "upload_write_incomplete");
        return;
    }
    if (!session.claim || !session.file) {
        queueResponse(fd, 500, "upload_write_incomplete");
        return;
    }
    if (!watchBody(fd, false)) return;
    const WriteTaskIdentity identity{fd, session.generation};
    const auto claim = *session.claim;
    if (!disk_pool_.receiveTask([this, identity, claim, size = session.written_bytes, file = session.file,
        temp = session.temp_path, final = session.final_path] {
        auto failure = VerifyPermissionResult::VerificationFailed;
        bool linked = false;
        bool commit_submitted = false;
        try {
            UniqueFd input{::open(temp.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
            if (!input.valid()) throw std::system_error(errno, std::generic_category(), "open upload for hash");
            crypto_hash_sha256_state hash;
            crypto_hash_sha256_init(&hash);
            std::array<unsigned char, 65536> buffer;
            std::uint64_t total = 0;
            while (true) {
                const auto count = ::read(input.get(), buffer.data(), buffer.size());
                if (count == -1 && errno == EINTR) continue;
                if (count == -1) throw std::system_error(errno, std::generic_category(), "read upload for hash");
                if (count == 0) break;
                total += static_cast<std::uint64_t>(count);
                if (total > size) throw std::runtime_error("Upload file exceeds expected size");
                crypto_hash_sha256_update(&hash, buffer.data(), static_cast<unsigned long long>(count));
            }
            if (total != size) throw std::runtime_error("Upload file length changed");
            unsigned char digest[crypto_hash_sha256_BYTES];
            crypto_hash_sha256_final(&hash, digest);
            char hex[crypto_hash_sha256_BYTES * 2 + 1];
            sodium_bin2hex(hex, sizeof hex, digest, sizeof digest);
            if (!claim.expected_sha256_hex.empty() && claim.expected_sha256_hex != hex) {
                failure = VerifyPermissionResult::DigestMismatch;
                throw std::runtime_error("Upload digest mismatch");
            }
            const auto sync = [](int target_fd) {
                int result;
                do { result = ::fsync(target_fd); } while (result == -1 && errno == EINTR);
                if (result == -1) throw std::system_error(errno, std::generic_category(), "sync uploaded file");
            };
            sync(file->get());
            const auto directory = std::filesystem::path{final}.parent_path();
            std::filesystem::create_directories(directory);
            if (::link(temp.c_str(), final.c_str()) == -1) {
                throw std::system_error(errno, std::generic_category(), "publish uploaded file");
            }
            linked = true;
            auto current = directory;
            for (int depth = 0; depth < 5; ++depth, current = current.parent_path()) {
                UniqueFd dir{::open(current.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
                if (!dir.valid()) throw std::system_error(errno, std::generic_category(), "open upload directory");
                sync(dir.get());
            }
            submit_business_([this, identity, claim, size, digest = std::string{hex}, final] {
                auto result = VerifyPermissionResult::VerificationFailed;
                try {
                    if (file_service_.finishUpload(claim.upload_id, size, digest)) {
                        result = VerifyPermissionResult::Allowed;
                    } else {
                        deleteTemporaryUpload(final);
                        file_service_.failUpload(claim.upload_id, "upload_publish_rejected");
                    }
                } catch (const std::exception&) {
                    // 提交结果不明确时保留正式文件供恢复流程核对。
                    Logger::write(Level::Error, "UploadReactor", "upload_commit_failed");
                }
                notifyVerification({identity, result, claim, true});
            });
            commit_submitted = true;
        } catch (const std::exception&) {
            Logger::write(Level::Warn, "UploadReactor", "upload_finalization_failed");
        }
        if (!commit_submitted) {
            if (linked) deleteTemporaryUpload(final);
            failClaim(claim.upload_id, failure == VerifyPermissionResult::DigestMismatch ? "upload_digest_mismatch" : "upload_finalization_failed");
            notifyVerification({identity, failure, claim, true});
        }
    })) {
        queueResponse(fd, 503, "upload_disk_busy");
        return;
    }
    session.http_state = UploadHttpState::Finalizing;
}

void UploadReactor::work() {
    //核心的流程
    std::array<epoll_event, 64> events{};
    while (running_.load()) {
        const int event_count = ::epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), 1000);
        if (event_count == -1) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "wait upload events");
        }
        if (!running_.load()) break;
        for (int i = 0; i < event_count; ++i) {
            const int event_fd = events[i].data.fd;
            const auto flags = events[i].events;
            try {
                if (event_fd == incoming_connection_listen_fd_.get()) {
                    // 接收待验证的连接 socket fd，等请求头可读后再取得上传凭据。
                    listen_fd_connection();
                } else if (event_fd == disk_task_completion_event_fd_) {
                    std::uint64_t value;
                    while (true) {
                        const auto count = ::read(disk_task_completion_event_fd_, &value, sizeof(value));
                        if (count > 0 || (count == -1 && errno == EINTR)) continue;
                        break;
                    }
                    drainVerificationResults();
                    std::queue<DiskWriteResult> ready;
                    {
                        std::lock_guard lock(mutex_);
                        ready.swap(disk_pool_task_response_queue_);
                    }
                    while (!ready.empty()) {
                        auto result = std::move(ready.front());
                        ready.pop();
                        const int fd = result.identity.connection_socket_fd;
                        const auto it = sessionMap.find(fd);
                        if (it == sessionMap.end() || it->second.generation != result.identity.generation) continue;
                        auto& session = it->second;

                        if (result.work_type == DiskWorkType::OpenFile) {
                            if (session.http_state != UploadHttpState::WaitingAuthorization) continue;
                            if (result.error || !result.opened_file || !result.opened_file->valid()) {
                                queueResponse(fd, 500, "upload_open_failed");
                                continue;
                            }
                            session.file = std::move(result.opened_file);
                            session.http_state = UploadHttpState::ReceivingBody;
                            if (!session.pending_body.empty()) {
                                if (!submitWrite(session)) queueResponse(fd, 503, "upload_disk_busy");
                                continue;
                            }
                        } else {
                            if (session.http_state != UploadHttpState::ReceivingBody || !session.write_in_progress) continue;
                            if (result.error || result.offset != session.written_bytes ||
                                result.written_bytes != session.pending_write_bytes) {
                                queueResponse(fd, 500, "upload_write_failed");
                                continue;
                            }
                            session.written_bytes += result.written_bytes;
                            session.pending_write_bytes = 0;
                            session.write_in_progress = false;
                        }

                        session.body_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
                        watchBody(fd, true);
                    }
                } else {
                    // 此分支的 event_fd 是连接 socket fd，不是监听 socket fd 或文件 fd。
                    if (flags & (EPOLLERR | EPOLLHUP)) {
                        //发生错误或者对方关闭连接
                        removeSession(event_fd);
                        continue;
                    }
                    if (flags & (EPOLLIN | EPOLLRDHUP)) {
                        // 请求头解析完成后进入 requestUploadAuthorization() 做首次业务校验。
                        const auto it = sessionMap.find(event_fd);
                        if (it == sessionMap.end()) continue;
                        const auto state = it->second.http_state;
                        if (state == UploadHttpState::ReadingHeaders) {
                            readRequestHeaders(event_fd);
                        }
                        if (state == UploadHttpState::ReceivingBody) {
                            auto& session = it->second;
                            if (session.write_in_progress) continue;

                            std::array<char, 8192> buffer;
                            const auto remaining = session.expected_bytes - session.received_bytes;
                            // 正文已收齐时只探测一个字节，等待 EOF 或发现额外数据。
                            const auto count = remaining == 0 ? 1 : std::min<std::uint64_t>(remaining, buffer.size());
                            ssize_t received;
                            do {
                                received = ::recv(event_fd, buffer.data(), count, 0);
                            } while (received == -1 && errno == EINTR);

                            if (received == 0) {
                                verifyReceivedBody(event_fd, session, false);
                                continue;
                            }
                            if (received == -1) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                                removeSession(event_fd);
                                continue;
                            }
                            if (remaining == 0) {
                                queueResponse(event_fd, 400, "upload_body_exceeds_content_length");
                                continue;
                            }

                            session.pending_body.assign(buffer.data(), buffer.data() + received);
                            session.received_bytes += static_cast<std::uint64_t>(received);
                            if (!submitWrite(session)) {
                                queueResponse(event_fd, 503, "upload_disk_busy");
                            } else {
                                watchBody(event_fd, false);
                            }
                        }
                    }
                    if (flags & EPOLLOUT) flushResponse(event_fd);
                }
            } catch (...) {
                if (event_fd != incoming_connection_listen_fd_.get() && event_fd != disk_task_completion_event_fd_) removeSession(event_fd);
                backend::logging::Logger::write(backend::logging::Level::Error, "UploadReactor", "event_failed");
            }
        }

        const auto now = std::chrono::steady_clock::now();
        for (auto it = sessionMap.begin(); it != sessionMap.end();) {
            const auto current = it++;
            auto& session = current->second;
            if (session.http_state == UploadHttpState::ReceivingBody && !session.write_in_progress &&
                now >= session.body_deadline) {
                verifyReceivedBody(current->first, session, true);
            }
        }
    }
}
bool UploadReactor::createFile(UploadSession& session) {
    if (!session.claim || session.upload_id.is_nil()) return false;
    const char* configured_root = std::getenv("FILE_STORAGE_ROOT");
    const std::filesystem::path root{configured_root && *configured_root ? configured_root : "/srv/chat-files"};
    const auto id = boost::uuids::to_string(session.upload_id);
    const auto attachment_id = boost::uuids::to_string(session.claim->attachment_id);
    const auto key = std::filesystem::path{"objects"} / "attachments" / attachment_id.substr(0, 2) /
        attachment_id.substr(2, 2) / (attachment_id + ".bin");
    if (session.claim->storage_key != key.string()) return false;
    const auto path = root / "staging" / "attachments" / id.substr(0, 2) / id.substr(2, 2) / (id + ".part");
    session.temp_path = path.string();
    session.final_path = (root / key).string();
    auto task = std::make_shared<DiskWriteTask>();
    task->identity.connection_socket_fd = session.socket.get();
    task->identity.generation = session.generation;
    task->offset = 0;
    task->temp_path = path.string();
    auto temporary = std::make_shared<TemporaryUpload>(disk_pool_, task->temp_path, nullptr);
    return disk_pool_.receiveTask([this, task, temporary, root, directory = path.parent_path()] {
        DiskWriteResult result{};
        result.work_type = DiskWorkType::OpenFile;
        result.identity = task->identity;
        if (!root.is_absolute() || !std::filesystem::is_directory(root, result.error)) {
            if (!result.error) result.error = std::make_error_code(std::errc::not_a_directory);
        } else {
            std::filesystem::create_directories(directory, result.error);
            if (!result.error) result = DiskOperation::OpenTempFile(*task);
        }
        if (result.opened_file) {
            temporary->file = std::move(result.opened_file);
            // Session 和在途写入共用清理所有权，最后一个持有者释放后才删除文件。
            result.opened_file = std::shared_ptr<UniqueFd>(temporary, temporary->file.get());
        }
        postWriteResult(std::move(result));
    });
}
bool UploadReactor::submitWrite(UploadSession& session) {
    if (session.write_in_progress || !session.file || !session.file->valid() || session.pending_body.empty()) {
        return false;
    }
    auto task = std::make_shared<DiskWriteTask>();
    task->identity.connection_socket_fd = session.socket.get();
    task->identity.generation = session.generation;
    task->offset = session.written_bytes;
    task->file = session.file;
    task->data.swap(session.pending_body);
    const auto block_size = task->data.size();

    bool accepted;
    try {
        accepted = disk_pool_.receiveTask([this, task] {
            auto result = DiskOperation::writeBlock(*task);
            postWriteResult(std::move(result));
        });
    } catch (...) {
        session.pending_body.swap(task->data);
        throw;
    }
    if (!accepted) {
        session.pending_body.swap(task->data);
        return false;
    }

    //注意，这里只是任务提交成功了，但是还没开始执行
    session.pending_write_bytes = block_size;
    session.write_in_progress = true;
    return true;
}
bool UploadReactor::submitVerificationRequest(
    WriteTaskIdentity identity, const boost::uuids::uuid& user_id, const boost::uuids::uuid& upload_id,
    const std::string& upload_token, std::uint64_t content_length) {
    try {
        submit_business_([this, identity, user_id, upload_id, upload_token, content_length] {
            Logger::write(Level::Info, "UploadReactor", "verification_started",
                {{"fd", identity.connection_socket_fd}, {"generation", identity.generation}});
            VerificationCompletion completion{identity, VerifyPermissionResult::VerificationFailed, std::nullopt};
            try {
                auto result = file_service_.claimUpload(user_id, upload_id, upload_token, content_length);
                if (auto* claim = std::get_if<ClaimedUpload>(&result)) {
                    completion.result = VerifyPermissionResult::Allowed;
                    completion.claim = std::move(*claim);
                } else {
                    completion.result = std::get<VerifyPermissionResult>(result);
                }
            } catch (const std::exception&) {
                Logger::write(Level::Error, "UploadReactor", "upload_claim_failed");
            }
            notifyVerification(std::move(completion));
        });
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void UploadReactor::failClaim(const boost::uuids::uuid& id, const std::string& code) noexcept {
    try {
        submit_business_([service = &file_service_, id, code] {
            service->failUpload(id, code);
        });
    } catch (const std::exception&) {
        Logger::write(Level::Warn, "UploadReactor", "upload_failure_record_not_queued");
    }
}

void UploadReactor::notifyVerification(VerificationCompletion completion) {
    {
        std::lock_guard lock(verification_queue_->mutex);
        verification_queue_->completions.push(std::move(completion));
    }
    const std::uint64_t value = 1;
    ssize_t written;
    do { written = ::write(disk_task_completion_event_fd_, &value, sizeof(value)); }
    while (written == -1 && errno == EINTR);
    if (written == -1 && errno != EAGAIN) {
        Logger::write(Level::Error, "UploadReactor", "verification_notify_failed", {{"errno", errno}});
    }
}

void UploadReactor::drainVerificationResults() {
    std::queue<VerificationCompletion> ready;
    {
        std::lock_guard lock(verification_queue_->mutex);
        ready.swap(verification_queue_->completions);
    }
    while (!ready.empty()) {
        auto completion = std::move(ready.front());
        ready.pop();
        const int fd = completion.identity.connection_socket_fd;
        const auto it = sessionMap.find(fd);
        if (it == sessionMap.end() || it->second.generation != completion.identity.generation) {
            if (completion.claim && (!completion.finalized || completion.result != VerifyPermissionResult::Allowed)) {
                failClaim(completion.claim->upload_id, "upload_connection_closed");
            }
            continue;
        }
        auto& session = it->second;
        if (completion.finalized) {
            if (session.http_state != UploadHttpState::Finalizing) continue;
            if (completion.result == VerifyPermissionResult::Allowed) {
                queueResponse(fd, 200, "upload_completed");
            } else if (completion.result == VerifyPermissionResult::DigestMismatch) {
                queueResponse(fd, 400, "upload_digest_mismatch");
            } else {
                queueResponse(fd, 500, "upload_finalization_failed");
            }
            continue;
        }
        if (session.http_state != UploadHttpState::WaitingAuthorization) {
            if (completion.claim) failClaim(completion.claim->upload_id, "upload_connection_closed");
            continue;
        }
        session.claim = std::move(completion.claim);
        Logger::write(Level::Info, "UploadReactor", "verification_result_received",
            {{"fd", fd}, {"generation", completion.identity.generation}});
        handleVerificationResult(fd, completion.result);
    }
}
void UploadReactor::postWriteResult(DiskWriteResult disk_write_result) {

    {
        std::lock_guard<std::mutex> lock(mutex_);
        disk_pool_task_response_queue_.push(std::move(disk_write_result));

    }
    const std::uint64_t notification=1;
    ssize_t written;
    do {
        written=::write(disk_task_completion_event_fd_, &notification, sizeof(notification));

    }while (written==-1 && errno == EINTR);
    if (written==static_cast<ssize_t>(sizeof(notification))) return;
    if (written==-1&&errno == EAGAIN) return;
    const int error = written == -1 ? errno : EIO;
    backend::logging::Logger::write(
        backend::logging::Level::Error,
        "UploadReactor",
        "disk_completion_notify_failed",
        {{"errno", error}}
    );
}
