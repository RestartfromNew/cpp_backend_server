//
// Created by yangb on 2026/11/16.
//

#include "file/DownloadReactor.h"

#include "common/Logger.h"
#include "http/HttpRequestHeadParser.h"
#include <array>
#include <algorithm>
#include <unistd.h>
#include <boost/uuid/string_generator.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>

DownloadReactor::DownloadReactor(int port, DiskPool& disk_pool, AuthMiddleWare& auth_middle_ware,
    FileService& file_service,DiskOperation &disk_operation):disk_pool_(disk_pool),port_(port),auth_middle_ware_(auth_middle_ware),
file_service_(file_service),disk_operation_(disk_operation) {
    if (port_ <= 0 || port_ > 65535) throw std::invalid_argument("invalid download port");
    init();
}
bool DownloadReactor::init() {
    //注册epoll
    if (epoll_fd_ != -1) return true;
    UniqueFd epoll{::epoll_create1(EPOLL_CLOEXEC)};
    if (!epoll.valid()) throw std::system_error(errno, std::generic_category(), "create download epoll");
    UniqueFd notification{::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)};
    if (!notification.valid()) throw std::system_error(errno, std::generic_category(), "create download eventfd");
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = notification.get();
    if (::epoll_ctl(epoll.get(), EPOLL_CTL_ADD, notification.get(), &event) == -1) throw std::system_error(errno, std::generic_category(), "register download eventfd");
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
void DownloadReactor::createListenSocket() {
    UniqueFd listener{::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    if (!listener.valid()) throw std::system_error(errno, std::generic_category(), "create download listener");
    int reuse = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) throw std::system_error(errno, std::generic_category(), "download SO_REUSEADDR");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port_));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) throw std::system_error(errno, std::generic_category(), "bind download 127.0.0.1:" + std::to_string(port_));
    if (::listen(listener.get(), 128) == -1) throw std::system_error(errno, std::generic_category(), "listen download");
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listener.get();
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listener.get(), &event) == -1) throw std::system_error(errno, std::generic_category(), "register download listener");
    incoming_connection_listen_fd_ = std::move(listener);
}
void DownloadReactor::listen_fd_connection() {
    // 监听 socket fd 可读表示有连接等待 accept，不是客户端文件正文到达。
    // 每轮限制 accept 数量，避免连接持续涌入时饿死已有连接的读写事件。
    for (int accepted = 0; accepted < 32 && running_.load(); ++accepted) {
        UniqueFd client{::accept4(incoming_connection_listen_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)};
        if (!client.valid()) {
            const int error = errno;
            if (error == EINTR) continue;
            if (error == EAGAIN || error == EWOULDBLOCK) return;
            throw std::system_error(error, std::generic_category(), "accept download connection");
        }
        if (sessionMap.size() >= 128) continue; // 限制未完成连接数量，client 自动关闭被拒绝的连接 socket fd。
        const int connection_socket_fd = client.get();
        DownloadSession session;
        session.generation = next_generation_++;
        session.socket = std::move(client);
        auto [iterator, inserted] = sessionMap.try_emplace(connection_socket_fd, std::move(session));
        if (!inserted) throw std::logic_error("download connection socket fd already exists");
        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.fd = connection_socket_fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, connection_socket_fd, &event) == -1) {
            const int error = errno;
            sessionMap.erase(iterator); // UniqueFd 关闭连接 socket fd，不能再次手动 close。
            throw std::system_error(error, std::generic_category(), "register download connection");
        }
        backend::logging::Logger::write(backend::logging::Level::Info, "DownloadReactor", "connection_accepted",
                                        {{"fd", connection_socket_fd}, {"generation", iterator->second.generation}});
    }
}
void DownloadReactor::work() {
    std::array<epoll_event, 64> events{};
    while (running_.load()) {
        const int event_count = ::epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), 1000);
        if (event_count == -1) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "wait download events");
        }
        if (!running_.load()) break;
        for (int i = 0; i < event_count; ++i) {
            const int event_fd = events[i].data.fd;
            const auto flags = events[i].events;
            try {
                if (event_fd == incoming_connection_listen_fd_.get()) {
                    // 接收待验证的连接 socket fd，等请求头可读后再取得下载凭据。
                    listen_fd_connection();
                } else if (event_fd == disk_task_completion_event_fd_) {
                    std::uint64_t value;
                    while (true) {
                        const auto count = ::read(disk_task_completion_event_fd_, &value, sizeof(value));
                        if (count > 0 || (count == -1 && errno == EINTR)) continue;
                        break;
                    }
                    std::queue<DownloadDiskReadResult> ready;
                    {
                        std::lock_guard lock(mutex_);
                        ready.swap(disk_pool_task_response_queue_);
                    }
                    while (!ready.empty()) {
                        auto result = std::move(ready.front());
                        ready.pop();
                        const int fd = result.identity.download_request_fd;
                        const auto it = sessionMap.find(fd);
                        if (it == sessionMap.end() || it->second.generation != result.identity.generation) continue;
                        auto& session = it->second;
                        if (result.work_type==DownloadWorkType::OpenFile) {
                            if (session.http_state != DownloadHttpState::WaitingAuthorization) continue;
                            if (result.error || !result.opened_file || !result.opened_file->valid() || result.file_size_bytes == 0) {
                                int status = 500;
                                std::string code = "download_open_failed";
                                if (result.file_error) {
                                    switch (*result.file_error) {
                                        case FileError::PermissionDenied: status = 403; code = "download_permission_denied"; break;
                                        case FileError::NoSuchFile: status = 404; code = "file_not_found"; break;
                                        case FileError::FileDeleted: status = 410; code = "file_deleted"; break;
                                        case FileError::FileExpired: status = 410; code = "file_expired"; break;
                                        case FileError::FileNotReady: status = 409; code = "file_not_ready"; break;
                                        case FileError::LookupFailed: break;
                                    }
                                }
                                queueResponse(fd, status, code);
                                continue;
                            }
                            session.file = std::move(result.opened_file);
                            session.expected_file_bytes = result.file_size_bytes;
                            // 成功响应头先发送，response_sent 只记录响应头的发送位置。
                            session.response_buffer = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                                std::to_string(session.expected_file_bytes) + "\r\n\r\n";
                            session.response_sent = 0;
                            session.block_sent = 0;
                            session.read_file_bytes = 0;
                            session.sent_bytes = 0;
                            session.http_state = DownloadHttpState::Downloading;
                            flushDownload(fd);

                        }
                        else if (result.work_type==DownloadWorkType::ReadDisk){
                            if (session.http_state != DownloadHttpState::Downloading || !session.read_in_progress) continue;
                            // 完成结果已到达，当前磁盘读取任务结束。
                            session.read_in_progress = false;
                            if (result.error || result.offset != session.read_file_bytes ||session.read_file_bytes > session.expected_file_bytes ||result.read_bytes == 0 || result.read_bytes != result.data.size() ||result.read_bytes > session.expected_file_bytes - session.read_file_bytes ||
                            !session.download_response_pending_body.empty()) {
                                queueResponse(fd, 500, "download_read_failed");
                                continue;}
                            session.read_file_bytes+=result.read_bytes;
                            session.download_response_pending_body = std::move(result.data);
                            session.sending_buffer_bytes = session.download_response_pending_body.size();
                            // 新正文块从 0 开始发送，响应头的进度保持不变。
                            session.block_sent = 0;
                            flushDownload(fd);
                        }
                    }
                }
                else {
                    if (flags & (EPOLLERR | EPOLLHUP)) {
                        //发生错误或者对方关闭连接
                        removeSession(event_fd);
                        continue;
                    }
                    if (flags & (EPOLLIN | EPOLLRDHUP)) {
                        // 请求头解析完成后验证 token，再投递设备授权和打开文件任务。
                        const auto it = sessionMap.find(event_fd);
                        if (it == sessionMap.end()) continue;
                        const auto state = it->second.http_state;
                        if (state == DownloadHttpState::ReadingHeader) {
                            readRequestHeaders(event_fd);
                        }
                    }
                    if (flags & EPOLLOUT) {
                        const auto it = sessionMap.find(event_fd);
                        if (it == sessionMap.end()) continue;
                        if (it->second.http_state == DownloadHttpState::Downloading) {
                            flushDownload(event_fd);
                        } else if (it->second.http_state == DownloadHttpState::SendingResponse) {
                            flushResponse(event_fd);
                        }
                    }


                }
            }
            catch (...) {
                if (event_fd != incoming_connection_listen_fd_.get() && event_fd != disk_task_completion_event_fd_) removeSession(event_fd);
                backend::logging::Logger::write(backend::logging::Level::Error, "DownloadReactor", "event_failed");
            }
        }
    }
}
void DownloadReactor::removeSession(int connection_socket_fd) {
    const auto it = sessionMap.find(connection_socket_fd);
    if (it == sessionMap.end()) return;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, connection_socket_fd, nullptr);
    sessionMap.erase(it);
}
void DownloadReactor::readRequestHeaders(int connection_socket_fd) {
    auto iterator = sessionMap.find(connection_socket_fd);
    if (iterator == sessionMap.end() || iterator->second.http_state != DownloadHttpState::ReadingHeader) return;
    auto& session = iterator->second;
    std::array<char, 4096> buffer{};
    while (true) {
        // 只读连接 socket fd；此处没有打开文件 fd，也不会向磁盘写入。
        const ssize_t received = ::recv(connection_socket_fd, buffer.data(), buffer.size(), 0);
        if (received == -1) {
            const int error = errno;
            if (error == EINTR) continue;
            if (error == EAGAIN || error == EWOULDBLOCK) return;
            backend::logging::Logger::write(backend::logging::Level::Warn, "DownloadReactor", "header_read_failed",
                                            {{"fd", connection_socket_fd}, {"generation", session.generation}, {"errno", error}});
            removeSession(connection_socket_fd);
            return;
        }
        if (received == 0) {
            queueResponse(connection_socket_fd, 400, "incomplete_download_headers");
            return;
        }
        session.header_buffer.append(buffer.data(), static_cast<std::size_t>(received));
        auto parsed = HttpRequestHeadParser::parse(session.header_buffer);
        if (parsed.status == HttpRequestHeadStatus::NeedMoreData) continue;
        if (parsed.status == HttpRequestHeadStatus::Error) {
            queueResponse(connection_socket_fd, parsed.http_status, parsed.error_code);
            return;
        }

        // 下载请求只接收请求头。尾部字节不能进入下载响应缓冲。
        if (parsed.content_length != 0 || session.header_buffer.size() != parsed.header_bytes) {
            queueResponse(connection_socket_fd, 400, "unexpected_download_body");
            return;
        }
        session.declared_bytes = parsed.content_length;
        session.download_request = std::move(parsed.request);
        session.header_buffer.clear();
        backend::logging::Logger::write(backend::logging::Level::Info, "DownloadReactor", "headers_parsed",
                                        {{"fd", connection_socket_fd}, {"generation", session.generation}});
        if (session.download_request.path != "/download_file") {
            queueResponse(connection_socket_fd, 404, "route_not_found");
            return;
        }
        if (session.download_request.method != HttpMethod::GET) {
            queueResponse(connection_socket_fd, 405, "download_method_not_allowed");
            return;
        }
        auto attachment_id = session.download_request.headers.find("x-attachment-id");
        if (attachment_id == session.download_request.headers.end()) attachment_id = session.download_request.headers.find("attachment_id");
        if (attachment_id == session.download_request.headers.end()) {
            queueResponse(connection_socket_fd, 400, "missing_attachment_id");
            return;
        }
        auto uploader_id = session.download_request.headers.find("x-uploader-id");
        if (uploader_id == session.download_request.headers.end()) uploader_id = session.download_request.headers.find("uploader_id");
        if (uploader_id == session.download_request.headers.end()) {
            queueResponse(connection_socket_fd, 400, "missing_uploader_id");
            return;
        }
        try {
            session.file_id = boost::uuids::string_generator{}(attachment_id->second);
        } catch (const std::runtime_error&) {
            queueResponse(connection_socket_fd, 400, "invalid_attachment_id");
            return;
        }
        if (session.file_id.is_nil()) {
            queueResponse(connection_socket_fd, 400, "invalid_attachment_id");
            return;
        }
        try {
            session.uploader_id = boost::uuids::string_generator{}(uploader_id->second);
        } catch (const std::runtime_error&) {
            queueResponse(connection_socket_fd, 400, "invalid_uploader_id");
            return;
        }
        if (session.uploader_id.is_nil()) {
            queueResponse(connection_socket_fd, 400, "invalid_uploader_id");
            return;
        }
        const auto auth = auth_middle_ware_.authenticate(session.download_request);
        if (const auto* error = std::get_if<AuthError>(&auth.result)) {
            queueResponse(connection_socket_fd, 401, *error == AuthError::Expired ? "access_token_expired" : "invalid_access_token");
            return;
        }
        session.user_id = std::get<boost::uuids::uuid>(auth.result);
        const auto device = session.download_request.headers.find("deviceid");
        if (device == session.download_request.headers.end()) {
            queueResponse(connection_socket_fd, 400, "missing_device_id");
            return;
        }
        try {
            session.device_id = boost::uuids::string_generator{}(device->second);
        } catch (const std::runtime_error&) {
            queueResponse(connection_socket_fd, 400, "invalid_device_id");
            return;
        }
        if (session.device_id.is_nil()) {
            queueResponse(connection_socket_fd, 400, "invalid_device_id");
            return;
        }
        if (!submitOpenFileRequest(session)) {
            queueResponse(connection_socket_fd, 503, "download_disk_busy");
        }
        return;

    }
}
void DownloadReactor::queueResponse(int connection_socket_fd, int status, const std::string& code) {
    const auto it = sessionMap.find(connection_socket_fd);
    if (it == sessionMap.end()) return;
    auto& session = it->second;
    // 200 响应已经开始发送后，失败只能关闭连接，避免在文件正文中插入第二个 HTTP 响应。
    if (session.http_state == DownloadHttpState::Downloading && session.response_sent > 0) {
        removeSession(connection_socket_fd);
        return;
    }
    session.read_in_progress = false;
    session.download_response_pending_body.clear();
    session.block_sent = 0;
    session.sending_buffer_bytes = 0;
    const auto body = nlohmann::json{{"error", {{"code", code}}}}.dump();
    session.response_buffer = "HTTP/1.1 " + std::to_string(status) + " Download Error\r\nConnection: close\r\n"
        "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    if (status == 405) session.response_buffer += "Allow: GET\r\n";
    session.response_buffer += "\r\n" + body;
    session.response_sent = 0;
    session.http_state = DownloadHttpState::SendingResponse;
    epoll_event event{};
    event.events = EPOLLOUT;
    event.data.fd = connection_socket_fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection_socket_fd, &event) == -1) {
        removeSession(connection_socket_fd);
        return;
    }
    flushResponse(connection_socket_fd);
}

void DownloadReactor::flushResponse(int connection_socket_fd) {
    const auto it = sessionMap.find(connection_socket_fd);
    if (it == sessionMap.end() || it->second.http_state != DownloadHttpState::SendingResponse) return;
    auto& session = it->second;
    while (session.response_sent < session.response_buffer.size()) {
        const ssize_t sent = ::send(connection_socket_fd, session.response_buffer.data() + session.response_sent, session.response_buffer.size() - session.response_sent, MSG_NOSIGNAL);
        if (sent == -1 && errno == EINTR) continue;
        if (sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (sent <= 0) break;
        session.response_sent += static_cast<std::size_t>(sent);
    }
    removeSession(connection_socket_fd);


}
bool DownloadReactor::submitOpenFileRequest(DownloadSession& session) {
    if (session.http_state != DownloadHttpState::ReadingHeader) return false;
    auto task=std::make_shared<DownloadTask>();
    task->identity.download_request_fd=session.socket.get();
    task->identity.generation=session.generation;
    task->offset=0;
    task->file_id = session.file_id;
    task->uploader_id = session.uploader_id;
    task->user_id = session.user_id;
    task->device_id = session.device_id;
    const bool accepted = disk_pool_.receiveTask([this, task] {
        DownloadDiskReadResult result{};
        result.work_type = DownloadWorkType::OpenFile;
        result.identity = task->identity;
        try {
            result = disk_operation_.OpenReadFile(*task);
        } catch (...) {
            result.error = std::make_error_code(std::errc::io_error);
        }
        postReadResult(std::move(result));

    });
    // 队列接受任务后再改变状态；失败时仍可构造 HTTP 错误响应。
    if (accepted) {
        session.http_state = DownloadHttpState::WaitingAuthorization;
        // 查库和打开文件在磁盘线程执行，等待时暂停连接 socket 的可读通知。
        epoll_event event{};
        event.data.fd = session.socket.get();
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, session.socket.get(), &event) == -1) {
            const int fd = session.socket.get();
            removeSession(fd);
        }
    }
    return accepted;
}
void DownloadReactor::postReadResult(DownloadDiskReadResult disk_read_result) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        disk_pool_task_response_queue_.push(std::move(disk_read_result));
    }
    const std::uint64_t notification=1;
    ssize_t read;
    do {
        read=::write(disk_task_completion_event_fd_, &notification, sizeof(notification));

    }while (read==-1 && errno == EINTR);
    if (read==static_cast<ssize_t>(sizeof(notification))) return;
    if (read==-1&&errno == EAGAIN) return;
    const int error = read == -1 ? errno : EIO;
    backend::logging::Logger::write(
        backend::logging::Level::Error,
        "DownloadReactor",
        "disk_completion_notify_failed",
        {{"errno", error}}
    );
}
bool DownloadReactor::submitRead(DownloadSession& session) {
    if (session.http_state != DownloadHttpState::Downloading || session.read_in_progress || !session.file || !session.file->valid() ||
       !session.download_response_pending_body.empty()) {
        return false;
       }

    if (session.read_file_bytes >= session.expected_file_bytes) {
        return false;
    }

    constexpr std::uint64_t block_size = 8192;
    const auto remaining = session.expected_file_bytes - session.read_file_bytes;

    auto task = std::make_shared<DownloadTask>();
    task->identity.download_request_fd = session.socket.get();
    task->identity.generation = session.generation;
    task->offset = session.read_file_bytes;
    task->file = session.file;
    task->expected_data = std::min(remaining, block_size);

    const bool accepted = disk_pool_.receiveTask([this, task] {
        DownloadDiskReadResult result{};
        result.work_type = DownloadWorkType::ReadDisk;
        result.identity = task->identity;
        result.offset = task->offset;

        try {
            result = disk_operation_.ReadBlock(*task);
        } catch (...) {
            result.error = std::make_error_code(std::errc::io_error);
        }

        postReadResult(std::move(result));
    });

    if (!accepted) return false;

    session.read_in_progress = true;
    return true;
}
void DownloadReactor::flushDownload(int connection_socket_fd) {
    const auto it = sessionMap.find(connection_socket_fd);
    if (it == sessionMap.end()) return;
    auto& session = it->second;
    if (session.http_state != DownloadHttpState::Downloading) return;

    // response_sent 只推进响应头；block_sent 只推进当前正文块。
    // 两个缓冲区必须按顺序发送，send 返回正数后才更新偏移。
    while (session.response_sent < session.response_buffer.size() ||
           session.block_sent < session.download_response_pending_body.size()) {
        const bool sending_header = session.response_sent < session.response_buffer.size();
        const char* data = sending_header ? session.response_buffer.data() + session.response_sent :
            session.download_response_pending_body.data() + session.block_sent;
        const auto remaining = sending_header ? session.response_buffer.size() - session.response_sent :
            session.download_response_pending_body.size() - session.block_sent;
        const ssize_t sent = ::send(connection_socket_fd, data, remaining, MSG_NOSIGNAL);
        if (sent == -1 && errno == EINTR) continue;
        if (sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            epoll_event event{};
            event.events = EPOLLOUT;
            event.data.fd = connection_socket_fd;
            if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection_socket_fd, &event) == -1) {
                removeSession(connection_socket_fd);
            }
            return;
        }
        if (sent <= 0) {
            removeSession(connection_socket_fd);
            return;
        }
        if (sending_header) {
            session.response_sent += static_cast<std::size_t>(sent);
        } else {
            session.block_sent += static_cast<std::size_t>(sent);
            session.sent_bytes += static_cast<std::uint64_t>(sent);
        }
    }

    session.download_response_pending_body.clear();
    session.block_sent = 0;
    session.sending_buffer_bytes = 0;
    if (session.sent_bytes == session.expected_file_bytes) {
        removeSession(connection_socket_fd);
        return;
    }

    // 当前没有可发送数据。等待读盘结果期间暂停 EPOLLOUT，避免空转。
    epoll_event event{};
    event.events = 0;
    event.data.fd = connection_socket_fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection_socket_fd, &event) == -1) {
        removeSession(connection_socket_fd);
        return;
    }
    if (!session.read_in_progress && !submitRead(session)) {
        removeSession(connection_socket_fd);
    }
}

void DownloadReactor::run() {
    try {
        work();
    } catch (...) {
        while (!sessionMap.empty()) removeSession(sessionMap.begin()->first);
        throw;
    }
    while (!sessionMap.empty()) removeSession(sessionMap.begin()->first);
}

void DownloadReactor::close() {
    running_.store(false);
    const std::uint64_t notification = 1;
    ssize_t written;
    do {
        written = ::write(disk_task_completion_event_fd_, &notification, sizeof(notification));
    } while (written == -1 && errno == EINTR);
}

DownloadReactor::~DownloadReactor() {
    // 所有者先 join Reactor 线程并等待 DiskPool 结束，保证回投任务不再访问本对象。
    close();
    if (disk_task_completion_event_fd_ != -1) ::close(disk_task_completion_event_fd_);
    if (epoll_fd_ != -1) ::close(epoll_fd_);
}