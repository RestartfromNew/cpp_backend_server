//
// Created by yangb on 2026/9/28.
//

#include "websocket/WebsocketWorker.h"

#include <fcntl.h>
#include <openssl/evp.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <unistd.h>

#include "http/HttpResponseGenerator.h"
#include "websocket/WebSocketDispatcher.h"

namespace {

std::atomic<std::uint64_t> nextSessionId{1};

std::uint64_t generateSessionId() noexcept {
    return nextSessionId.fetch_add(1, std::memory_order_relaxed);
}

std::string makeWebSocketAccept(std::string_view clientKey) {
    constexpr std::string_view websocketGuid =
        "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    std::string source;
    source.reserve(clientKey.size() + websocketGuid.size());
    source.append(clientKey);
    source.append(websocketGuid);

    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digestLength = 0;
    if (EVP_Digest(
            source.data(),
            source.size(),
            digest,
            &digestLength,
            EVP_sha1(),
            nullptr
        ) != 1) {
        throw std::runtime_error("Failed to calculate WebSocket SHA-1 digest");
    }

    const std::size_t encodedCapacity =
        4 * ((static_cast<std::size_t>(digestLength) + 2) / 3);
    std::string encoded(encodedCapacity, '\0');

    const int encodedLength = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(encoded.data()),
        digest,
        static_cast<int>(digestLength)
    );
    if (encodedLength < 0) {
        throw std::runtime_error("Failed to encode WebSocket accept value");
    }

    encoded.resize(static_cast<std::size_t>(encodedLength));
    return encoded;
}

}

WebsocketWorker::WebsocketWorker(WebSocketDispatcher &dispatcher):dispatcher_(dispatcher) {
    //初始化
    epoll_fd_= ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1)
        throw std::system_error(errno, std::generic_category(),"Failed to create epoll");
    //其它线程投递任务用于唤醒worker的通信机制
    event_fd_ =::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd_ == -1) {
        ::close(epoll_fd_);
        throw std::system_error(errno,std::generic_category(),"Failed to create eventfd");
    }
    business_fd_=::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (business_fd_ == -1) {
        ::close(event_fd_);
        ::close(epoll_fd_);
        throw std::system_error(errno,std::generic_category(),"Failed to create business eventfd");
    }
    timer_fd_=::timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC);
    if (timer_fd_==-1) {
        ::close(business_fd_);
        ::close(event_fd_);
        ::close(epoll_fd_);
        throw std::system_error(errno,std::generic_category(),"Failed to create timer");
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = event_fd_;
    //把event_fd_注册到epoll
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &event) == -1) {
        ::close(business_fd_);
        ::close(event_fd_);
        ::close(epoll_fd_);
        ::close(timer_fd_);
        throw std::system_error(errno,std::generic_category(),"Failed to register eventfd");
    }

    epoll_event businessEvent{};
    businessEvent.events = EPOLLIN;
    businessEvent.data.fd = business_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, business_fd_, &businessEvent) == -1) {
        ::close(business_fd_);
        ::close(event_fd_);
        ::close(epoll_fd_);
        ::close(timer_fd_);
        throw std::system_error(errno,std::generic_category(),"Failed to register business eventfd");
    }

    itimerspec timerSpec{};
    timerSpec.it_value.tv_sec = 5;
    timerSpec.it_value.tv_nsec = 0;
    timerSpec.it_interval.tv_sec = 5;
    timerSpec.it_interval.tv_nsec = 0;

    if (::timerfd_settime(timer_fd_,0,&timerSpec,nullptr) == -1) {
        ::close(timer_fd_);
        ::close(business_fd_);
        ::close(event_fd_);
        ::close(epoll_fd_);
        throw std::system_error(errno,std::generic_category(),"Failed to configure heartbeat timerfd");
    }
    epoll_event timerEvent{};
    timerEvent.events = EPOLLIN;
    timerEvent.data.fd = timer_fd_;

    if (::epoll_ctl(epoll_fd_,EPOLL_CTL_ADD,timer_fd_,&timerEvent) == -1) {
        ::close(business_fd_);
        ::close(event_fd_);
        ::close(epoll_fd_);
        ::close(timer_fd_);
        throw std::system_error(errno,std::generic_category(),"Failed to register heartbeat timerfd");
    }
    running_.store(true);
    try {
        thread_=std::thread(&WebsocketWorker::worker,this);
    } catch (...) {
        running_.store(false);
        ::close(timer_fd_);
        ::close(business_fd_);
        ::close(event_fd_);
        ::close(epoll_fd_);
        timer_fd_ = -1;
        business_fd_ = -1;
        event_fd_ = -1;
        epoll_fd_ = -1;
        throw;
    }
}
WebsocketWorker::~WebsocketWorker() {
    running_.store(false);

    if (event_fd_ != -1) {
        const std::uint64_t value = 1;
        static_cast<void>(::write(event_fd_, &value, sizeof(value)));
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    websocket_sessions_map.clear();
    if (business_fd_ != -1) ::close(business_fd_);
    if (event_fd_ != -1) ::close(event_fd_);
    if (epoll_fd_ != -1) ::close(epoll_fd_);
    if (timer_fd_ != -1) ::close(timer_fd_);
}
void WebsocketWorker::worker() {
    constexpr int maxEvents = 64;
    //提供给内核的结果函数，调用wait后，内核会把结果放在这个array中
    std::array<epoll_event, maxEvents> events{};
    while (running_.load()) {
        //一次最多取出64个事件
        //大部分时间在等待，timeout=-1表示至少发生一个事件
        const int eventCount = ::epoll_wait(epoll_fd_,events.data(),
            static_cast<int>(events.size()),-1);
        if (eventCount == -1) {
            //中断，不代表错误
            if (errno == EINTR)
                continue;
            break;
        }
        if (!running_.load()) break;
        for (int i = 0; i < eventCount; ++i) {
            //来自哪个文件描述符
            const int fd = events[i].data.fd;
            //位掩码，因为fd可能既可以读也可以写
            const std::uint32_t flags = events[i].events;
            try {
                if (fd == event_fd_) {
                    std::cout<<"收到新转交连接"<<std::endl;
                    //event_fd_是其它线程唤醒worker的通道，表示Http转交的Request
                    handleWakeup();
                    continue;
                }
                if (fd == business_fd_) {
                    //从business中接收到Task
                    handleTask();
                    continue;
                }
                if (fd==timer_fd_) {
                    handleTimer();
                    continue;
                }
                if ((flags & (EPOLLERR | EPOLLHUP)) != 0) {
                    removeSession(fd);
                    continue;
                }
                if ((flags & EPOLLIN) != 0) {
                    //表示数据可读
                    handleReadable(fd);
                }
                if (websocket_sessions_map.find(fd) == websocket_sessions_map.end()) {
                    continue;
                }
                if ((flags & EPOLLOUT) != 0) {
                    //表示可写
                    //fd可能同时可读或者可写
                    handleWritable(fd);
                }
                if (websocket_sessions_map.find(fd) == websocket_sessions_map.end()) {
                    continue;
                }
                if ((flags & EPOLLRDHUP) != 0 && !closing_sessions_.contains(fd)) {
                    removeSession(fd);
                }
            } catch (const std::exception& error) {
                std::cerr << "WebSocket worker event failed: " << error.what() << '\n';
            }
        }
    }

}
void WebsocketWorker::removeSession(int fd) {
    //取消监听，删除fd,自动析构
    std::uint64_t sessionId = 0;
    const auto iterator = websocket_sessions_map.find(fd);
    if (iterator != websocket_sessions_map.end()) {
        sessionId = iterator->second->id();
    }

    ::epoll_ctl(epoll_fd_,EPOLL_CTL_DEL,fd,nullptr);
    closing_sessions_.erase(fd);
    websocket_sessions_map.erase(fd);
    std::cout
        << "WebSocket session closed: session="
        << sessionId
        << ", fd="
        << fd
        << std::endl;
}
void WebsocketWorker::wakeup() {
    //唤醒接口
    const std::uint64_t value=1;
    const ssize_t result = ::write(event_fd_,&value,sizeof(value));
    if (result == -1&&errno !=EAGAIN) {
        throw std::system_error(errno,std::system_category(),"Failed to wakeup websocket worker");
    }
}

void WebsocketWorker::transferSession(PendingWebsocket session) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_session_queue_.push(std::move(session));
    }
    wakeup();
}
void WebsocketWorker::transferTask(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        task_queue_.push(std::move(task));
    }
    taskWakeup();
}
void  WebsocketWorker::taskWakeup() {
    const std::uint64_t value=1;
    const ssize_t result = ::write(business_fd_,&value,sizeof(value));
    if (result == -1&&errno !=EAGAIN) {
        throw std::system_error(errno,std::system_category(),"Business thread failed to wakeup websocket worker");
    }
}
void WebsocketWorker::handleWakeup() {
    std::uint64_t wakeupCount=0;
    while (true) {
        const ssize_t result = ::read(event_fd_,&wakeupCount,sizeof(wakeupCount));
        if (result==static_cast<ssize_t>(sizeof(wakeupCount)))
            break;
        if (result==-1&&errno==EINTR)
            continue;
        if (result==-1&&errno==EAGAIN)
            break;
        throw std::system_error(errno,std::generic_category(),"Failed to read WebSocket worker eventfd");
    }
    std::queue<PendingWebsocket> pendingSessionsLocal;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingSessionsLocal.swap(this->pending_session_queue_);
    }
    while (!pendingSessionsLocal.empty()) {
        PendingWebsocket pending = std::move(pendingSessionsLocal.front());
        pendingSessionsLocal.pop();
        try {
            acceptingSessions(std::move(pending));
        } catch (const std::exception& error) {
            std::cerr << "WebSocket handshake failed: " << error.what() << '\n';
        }
    }
}
void WebsocketWorker::handleTask() {
    std::uint64_t wakeupCount = 0;
    while (true) {
        const ssize_t result = ::read(business_fd_, &wakeupCount, sizeof(wakeupCount));
        if (result == static_cast<ssize_t>(sizeof(wakeupCount))) break;
        if (result == -1 && errno == EINTR) continue;
        if (result == -1 && errno == EAGAIN) break;
        throw std::system_error(errno, std::generic_category(), "Failed to read business eventfd");
    }

    std::queue<Task> localTasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        localTasks.swap(task_queue_);
    }

    while (!localTasks.empty()) {
        Task task = std::move(localTasks.front());
        localTasks.pop();
        try {
            task();
        } catch (const std::exception& error) {
            std::cerr << "WebSocket task failed: " << error.what() << '\n';
        }
    }

}
void WebsocketWorker::acceptingSessions(PendingWebsocket session) {
    const HttpRequest& request=session.request;
    HttpResponse response;
    const auto keyIt = request.headers.find("Sec-WebSocket-Key");

    if (keyIt == request.headers.end() || keyIt->second.empty()) {
        throw std::runtime_error(
            "Missing Sec-WebSocket-Key header"
        );
    }
    const std::string acceptValue =makeWebSocketAccept(keyIt->second);
    response.status=HttpStatus::Switching_Protocol;
    response.headers["Upgrade"]="websocket";
    response.headers["Sec-WebSocket-Accept"]=acceptValue;
    response.headers["Connection"]="Upgrade";
    std::string handshake=response_generator_.generateHttpResponse(response);
    //握手
    session.connection.sendAll(handshake);

    const int socketFd = session.connection.fd();
    const int currentFlags = ::fcntl(socketFd, F_GETFL, 0);
    if (currentFlags == -1 ||
        ::fcntl(socketFd, F_SETFL, currentFlags | O_NONBLOCK) == -1) {
        throw std::system_error(errno, std::generic_category(), "Failed to set WebSocket non-blocking mode");
    }

    //构造新session
    const std::uint64_t sessionId = generateSessionId();
    auto newSessionPtr =std::make_unique<WebSocketSession>(sessionId,std::move(session.connection));
    const bool inserted = websocket_sessions_map.emplace(socketFd,std::move(newSessionPtr)).second;
    if (!inserted) {
        throw std::runtime_error("WebSocket fd is already registered");
    }

    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.fd = socketFd;
    //注册监听
    if (::epoll_ctl(epoll_fd_,EPOLL_CTL_ADD,socketFd,&event) == -1) {
        websocket_sessions_map.erase(socketFd);
        throw std::system_error(errno,std::generic_category(),"Failed to register WebSocket session");
     }

}
void WebsocketWorker::handleReadable(int fd) {
    auto iterator=websocket_sessions_map.find(fd);
    if (iterator==websocket_sessions_map.end()) {
        return;
    }
    WebSocketSession& session =*iterator->second;
    WebSocketHandleResult result =message_handler_.handleReadable(session);

    if (result.output_queued) {
        setWriteInterest(fd, true);
        if (websocket_sessions_map.find(fd) == websocket_sessions_map.end()) return;
    }

    for (Frame& frame : result.frames) {
        nlohmann::json applicationMessage = session.ApplicationParse(frame);
        const std::uint64_t sessionId = session.id();

        std::cout
            << "WebSocket message reached business-pool boundary: session="
            << sessionId
            << ", opcode="
            << static_cast<unsigned int>(frame.opcode)
            << ", payload_bytes="
            << frame.payload.size()
            << ", application="
            << applicationMessage.dump()
            << '\n';

        //dispatch的第一个参数是json,第二个是整个reply;在task中传入的response，传入这个lambda
        dispatcher_.dispatch(std::move(applicationMessage),[this, fd, sessionId](nlohmann::json response) mutable {
            //整个lambda函数体就是一个task,调用transferTask会把整个构造任务放在worker的task.queue中
            transferTask(
                    //这里还没有进入投递
                    [this,fd,sessionId,response = std::move(response)]() mutable {
                        const auto sessionIterator =websocket_sessions_map.find(fd);
                        if (sessionIterator ==websocket_sessions_map.end()) {
                            return;
                        }
                        WebSocketSession& targetSession =*sessionIterator->second;
                        if (targetSession.id() != sessionId) {
                            return;
                        }
                        const std::string responseText =response.dump();
                        Frame responseFrame;
                        responseFrame.fin = true;
                        responseFrame.opcode = Opcode::Text;
                        responseFrame.payload.assign(responseText.begin(),responseText.end());
                        const std::vector<std::uint8_t> encoded =targetSession.FrameEncoder(responseFrame);
                        targetSession.connection().write(std::string_view{reinterpret_cast<const char*>(encoded.data()),encoded.size()});
                        setWriteInterest(fd, true);
                    }
                );
            }
        );
    }

    switch (result.status) {
        case WebSocketHandleStatus::MessagesReady:
        case WebSocketHandleStatus::NeedMoreData:
            return;

        case WebSocketHandleStatus::PeerClosed:
            if (result.output_queued) {
                closing_sessions_.insert(fd);
                std::cout
                    << "WebSocket Close received; reply queued: session="
                    << session.id()
                    << ", fd="
                    << fd
                    << std::endl;
                return;
            }
            removeSession(fd);
            return;

        case WebSocketHandleStatus::ProtocolError:
        case WebSocketHandleStatus::IoError:
            removeSession(fd);
            return;
    }

}

void WebsocketWorker::handleWritable(int fd) {
    const auto iterator = websocket_sessions_map.find(fd);
    if (iterator == websocket_sessions_map.end()) return;

    Connection& connection = iterator->second->connection();
    const ssize_t bytesWritten = connection.flush();
    if (bytesWritten > 0) {
        if (!connection.hasPendingOutput()) {
            if (closing_sessions_.contains(fd)) {
                std::cout
                    << "WebSocket Close reply sent: session="
                    << iterator->second->id()
                    << ", fd="
                    << fd
                    << std::endl;
                removeSession(fd);
            } else {
                setWriteInterest(fd, false);
            }
        }
        return;
    }
    if (bytesWritten == 0 && !connection.hasPendingOutput()) {
        if (closing_sessions_.contains(fd)) {
            std::cout
                << "WebSocket Close reply sent: session="
                << iterator->second->id()
                << ", fd="
                << fd
                << std::endl;
            removeSession(fd);
        } else {
            setWriteInterest(fd, false);
        }
        return;
    }

    if (bytesWritten == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    removeSession(fd);
}

void WebsocketWorker::setWriteInterest(int fd, bool enabled) {
    //开启该socket的EPOLLOUT,等待内核通知可以发送
    if (websocket_sessions_map.find(fd) == websocket_sessions_map.end()) return;
    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP;
    if (enabled) event.events |= EPOLLOUT;
    event.data.fd = fd;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) == -1) {
        removeSession(fd);
    }
}
void WebsocketWorker::handleTimer() {
    std::uint64_t expirationCount = 0;
    while (true) {
        //一定要清除这个状态，不然会一直通知
        const ssize_t result = ::read(timer_fd_,&expirationCount,sizeof(expirationCount));
        if (result ==static_cast<ssize_t>(sizeof(expirationCount))) {
            break;
        }
        if (result == -1 && errno == EINTR) {
            continue;
        }
        if (result == -1 &&(errno == EAGAIN ||errno == EWOULDBLOCK)) {
            return;
        }
        throw std::system_error(errno,std::generic_category(),"Failed to read heartbeat timerfd");
    }
    std::vector<int> expiredFds;
    std::vector<int> writableFds;
    auto now=std::chrono::steady_clock::now();
    for (auto& [fd, session] :websocket_sessions_map){
        if (session->PongTimeout(now,pong_timeout_)) {
            //超时检测
            expiredFds.push_back(fd);
            continue;
        }
        if (!session->shouldSendPing(now,heartbeat_interval_))
            //不需要ping
            continue;
        //剩余的构造ping进行发送
        std::cout<<"发送心跳检测"<<std::endl;
        Frame ping;
        ping.fin = true;
        ping.opcode = Opcode::Ping;
        ping.payload.clear();
        auto pingStream=session->FrameEncoder(ping);
        session->connection().write(std::string_view{reinterpret_cast<const char*>(pingStream.data()),pingStream.size()});
        session->markPingSent(now);
        writableFds.push_back(fd);
    }

    for (const int fd: expiredFds) {
        removeSession(fd);
    }
    for (const int fd: writableFds) {
        setWriteInterest(fd,true);
    }
}
