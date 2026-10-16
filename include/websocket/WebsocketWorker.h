//
// Created by yangb on 2026/9/28.
//

#ifndef CPP_BACKEND_SERVER_WEBSOCKETWORKER_H
#define CPP_BACKEND_SERVER_WEBSOCKETWORKER_H
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <vector>
#include <memory>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "websocket/WebSocketSession.h"
#include "http/HttpResponseGenerator.h"
#include <sys/timerfd.h>

#include "http/AuthMiddleWare.h"
#include "http/HttpRequest.h"
#include "service/SessionIdentityService.h"
#include "websocket/WebSocketHandler.h"

class WebSocketPool;
class WebSocketDispatcher;

struct PendingWebsocket {
    Connection connection;
    HttpRequest request;
};
class WebsocketWorker {
public:
    using Task=std::function<void()>;
    WebsocketWorker(std::size_t worker_id,WebSocketDispatcher &dispatcher,
        AuthMiddleWare &auth_middle_ware,SessionIdentityService &sessionIdentityService,WebSocketPool &pool);
    ~WebsocketWorker();
    void transferSession(PendingWebsocket session);
    void transferTask(Task task);
    void wakeup();
    void taskWakeup();
    void enqueuecloseSession(int fd,std::uint64_t session_id,const boost::uuids::uuid& device_id);
    std::future<void> revokeDeviceConnections(const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& device_id);
    void enqueuePush(int fd,std::uint64_t session_id,const boost::uuids::uuid& device_id,nlohmann::json message);
private:
    std::size_t worker_id_;
    WebSocketDispatcher &dispatcher_;
    std::thread thread_;
    int epoll_fd_=-1;
    int event_fd_=-1;
    int business_fd_=-1;

    //timer用于定时唤醒线程来检测心跳
    int timer_fd_ = -1;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    HttpResponseGenerator response_generator_;
    //注意，这是worker从业务池中返回的，需要worker操作的任务，需要从这里取任务发出
    //和worker放入业务池的队列区别
    std::queue<Task> task_queue_;
    std::queue<PendingWebsocket> pending_session_queue_;
    std::unordered_map<int,std::unique_ptr<WebSocketSession>> websocket_sessions_map;
    std::unordered_set<int> closing_sessions_;
    std::unordered_map<int, std::chrono::steady_clock::time_point> revoke_deadlines_;
    std::unordered_map<int, std::vector<std::function<void()>>> close_completions_;
    WebSocketMessageHandler message_handler_;
    std::chrono::seconds heartbeat_interval_{30};
    std::chrono::seconds pong_timeout_{10};
    void worker();
    void handleWakeup();
    void handleTask();
    void acceptingSessions(PendingWebsocket session);
    void removeSession(int fd);
    void handleReadable(int fd);
    void handleWritable(int fd);
    void setWriteInterest(int fd, bool enabled);
    void handleTimer();
    SessionIdentityService &sessionIdentityService_;
    AuthMiddleWare &auth_middle_ware_;
    WebSocketPool &pool_;



};


#endif //CPP_BACKEND_SERVER_WEBSOCKETWORKER_H
