// Created by yangb on 2026/11/3.
#include "file/UploadReactor.h"
#include "file/UploadHttpParser.h"
#include "common/Logger.h"
#include <array>
#include <stdexcept>
#include <unistd.h>

UploadReactor::UploadReactor(int port,DiskPool &disk_pool):port_(port),disk_pool_(disk_pool) {
    if (port_ <= 0 || port_ > 65535) throw std::invalid_argument("invalid upload port");
    init();
}

UploadReactor::~UploadReactor() {
    // 所有者必须先让 run() 返回；不能与仍在运行的 Reactor 线程并发销毁对象。
    close();
    sessionMap.clear();
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

    session.http_state = UploadHttpState::WaitingAuthorization;
    requestUploadAuthorization(connection_socket_fd);
}

void UploadReactor::requestUploadAuthorization(int connection_socket_fd) {
    // TODO：在这里把 session.upload_request 和 declared_bytes 投递给业务线程池，由 Service 校验：
    // 1. Authorization 身份、设备归属与 active 状态。
    // 2. X-Upload-ID 对应任务、X-Upload-Token、到期时间、任务状态和批准大小。
    // 3. 用连接 socket fd + generation 回投结果；只有授权成功才能创建文件 fd、恢复接收正文。
    // 等待业务结果时应暂停 EPOLLIN；不能在 Reactor 线程查库，也不能把解析成功当成授权成功。
    // 当前尚未接入 Service，明确返回 503，不创建文件、不执行磁盘任务、不返回上传成功。
    //投递业务的代码
    queueResponse(connection_socket_fd, 503, "upload_authorization_not_implemented");
}

void UploadReactor::queueResponse(int connection_socket_fd, int status, const std::string& code) {
    auto iterator = sessionMap.find(connection_socket_fd);
    if (iterator == sessionMap.end()) return;
    auto& session = iterator->second;
    const std::string body = "{\"error\":{\"code\":\"" + code + "\"}}";
    session.response_buffer = "HTTP/1.1 " + std::to_string(status) + " Upload Error\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    if (status == 405) session.response_buffer += "Allow: PUT\r\n";
    session.response_buffer += "\r\n" + body;
    session.response_sent = 0;
    session.http_state = UploadHttpState::SendingResponse;
    epoll_event event{};
    event.events = EPOLLOUT; // 停止读取正文，只等待错误响应发完。
    event.data.fd = connection_socket_fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, connection_socket_fd, &event) == -1) {
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
        if (sent <= 0) break;
        session.response_sent += static_cast<std::size_t>(sent);
    }
    removeSession(connection_socket_fd);
}

void UploadReactor::removeSession(int connection_socket_fd) {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, connection_socket_fd, nullptr);
    sessionMap.erase(connection_socket_fd); // Session 的 UniqueFd 负责关闭连接 socket fd。
}

void UploadReactor::run() {
    work();
}

void UploadReactor::work() {
    //核心的流程
    std::array<epoll_event, 64> events{};
    while (running_.load()) {
        const int event_count = ::epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), -1);
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
                    //接收新的fd
                    //这里有一个服务验证的入口，表示此次上传被允许了
                    //TODO： 完成用户上传许可的验证
                    listen_fd_connection();
                } else if (event_fd == disk_task_completion_event_fd_) {
                    std::uint64_t value;
                    while (true) {
                        const auto count = ::read(disk_task_completion_event_fd_, &value, sizeof(value));
                        if (count > 0 || (count == -1 && errno == EINTR)) continue;
                        break;
                    }
                    // TODO：取出磁盘/业务完成队列的结果，只在 Reactor 线程更新 Session。
                } else {
                    // 此分支的 event_fd 是连接 socket fd，不是监听 socket fd 或文件 fd。
                    if (flags & (EPOLLERR | EPOLLHUP)) {
                        removeSession(event_fd);
                        continue;
                    }
                    if (flags & (EPOLLIN | EPOLLRDHUP)) readRequestHeaders(event_fd);
                    if (flags & EPOLLOUT) flushResponse(event_fd);
                }
            } catch (...) {
                if (event_fd != incoming_connection_listen_fd_.get() && event_fd != disk_task_completion_event_fd_) removeSession(event_fd);
                backend::logging::Logger::write(backend::logging::Level::Error, "UploadReactor", "event_failed");
            }
        }
    }
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
