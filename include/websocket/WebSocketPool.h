//
// Created by yangb on 2026/9/27.
//

#ifndef CPP_BACKEND_SERVER_WEBSOCKETPOOL_H
#define CPP_BACKEND_SERVER_WEBSOCKETPOOL_H
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "common/UniqueFd.h"
#include "http/HttpRequest.h"
#include "server/Connection.h"
#include "websocket/WebsocketWorker.h"
#include <memory>
#include <vector>

#include "http/HttpRequest.h"

struct WebSocketPoolConfig {
    int min_capacity;
    int max_task_capacity;
};
struct OnlineConnection {
    std::size_t worker_id;     // 属于哪个 Worker
    int fd;                    // 哪条连接
    std::uint64_t session_id;  // 防止断线后 fd 重用
};
class WebSocketDispatcher;

class WebSocketPool {
    public:
    using Task=std::function<void()>;
    WebSocketPool(const WebSocketPoolConfig &config,WebSocketDispatcher &dispatcher,AuthMiddleWare &auth_middle_ware,SessionIdentityService &sessionIdentityService);
    ~WebSocketPool();
    bool init();
    void close();
    bool forward_connections(PendingWebsocket && request);
    void push_To_Device(const boost::uuids::uuid &device_id,  nlohmann::json);
    void addIndex(const boost::uuids::uuid &device_id,std::size_t worker_id,int fd,std::uint64_t session_id);
    void removeIndex(const boost::uuids::uuid &device_id,std::size_t worker_id,int fd,std::uint64_t session_id);
    // HTTP/owner threads only; never wait from a WebSocket worker.
    bool closeSession(const boost::uuids::uuid &user_id,const boost::uuids::uuid &device_id);
private:
    //device_id 和worker的归谁关系
    std::unordered_map<boost::uuids::uuid,OnlineConnection> online_connections_;
    WebSocketDispatcher &dispatcher_;
    AuthMiddleWare &auth_middle_ware_;
    std::mutex mutex_;
    WebSocketPoolConfig config_;
    std::vector<std::unique_ptr<WebsocketWorker>> websocket_workers_;
    bool initialized_ = false;
    bool stopping = true;
    std::size_t next_worker_ = 0;
    SessionIdentityService &sessionIdentityService_;

};


#endif //CPP_BACKEND_SERVER_WEBSOCKETPOOL_H
