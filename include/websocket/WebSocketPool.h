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
#include <boost/uuid/uuid.hpp>

#include "http/HttpRequest.h"

/** @brief WebSocket worker 数及现有容量配置；不应把字段存在视为所有内部队列已有背压限制。 */
struct WebSocketPoolConfig {
    int min_capacity;
    int max_task_capacity;
};
/** @brief 在线目录快照：worker/fd/session_id/user；投递后目标 Worker 再核对，避免断线重连和 fd 重用误发。 */
struct OnlineConnection {
    std::size_t worker_id;     // 属于哪个 Worker
    int fd;                    // 哪条连接
    std::uint64_t session_id;  // 防止断线后 fd 重用
    boost::uuids::uuid user_id;
};
class WebSocketDispatcher;

/** @brief 互斥保护设备在线目录并转投目标 Worker；不共享 Session 对象，不持有独立业务 Task 执行队列。 */
class WebSocketPool {
    public:
    using Task=std::function<void()>;
    WebSocketPool(const WebSocketPoolConfig &config,WebSocketDispatcher &dispatcher,AuthMiddleWare &auth_middle_ware,SessionIdentityService &sessionIdentityService);
    ~WebSocketPool();
    bool init();
    void close();
    bool forward_connections(PendingWebsocket && request);
    void push_To_Device(const boost::uuids::uuid &device_id,  nlohmann::json);
    void push_To_User(const boost::uuids::uuid &user_id,  nlohmann::json);
    void addIndex(const boost::uuids::uuid &device_id,const boost::uuids::uuid &user_id,std::size_t worker_id,int fd,std::uint64_t session_id);
    // 必须匹配 worker/fd/session_id 才删除，避免旧连接清理覆盖同设备新连接。
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
