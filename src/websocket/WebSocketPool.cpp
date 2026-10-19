//
// Created by yangb on 2026/9/27.
//

#include "websocket/WebSocketPool.h"

#include <stdexcept>

WebSocketPool::WebSocketPool(const WebSocketPoolConfig &config,WebSocketDispatcher &dispatcher,AuthMiddleWare &auth_middle_ware,SessionIdentityService &sessionIdentityService):dispatcher_(dispatcher),config_(config),auth_middle_ware_(auth_middle_ware),sessionIdentityService_(sessionIdentityService) {

}
WebSocketPool::~WebSocketPool() {
    close();
}
bool WebSocketPool::init() {
    {
        std::lock_guard lock(mutex_);
        if (initialized_ || !websocket_workers_.empty()) return false;
        if (config_.max_task_capacity<=0||config_.min_capacity<=0)
            throw std::invalid_argument("WebSocket Configuration must be positive");
    }

    std::vector<std::unique_ptr<WebsocketWorker>> workers;
    try {
        const size_t workerCount =static_cast<std::size_t>(config_.min_capacity);
        workers.reserve(workerCount);
        for (std::size_t i = 0;i < workerCount;++i) {
            workers.push_back(std::make_unique<WebsocketWorker>(i,dispatcher_,auth_middle_ware_,sessionIdentityService_,*this));
        }
    }
    catch (...) {
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        if (initialized_ || !websocket_workers_.empty()) return false;
        websocket_workers_ = std::move(workers);
        next_worker_ = 0;
        stopping = false;
        initialized_ = true;
    }
    std::cout<<"Pool启动"<<std::endl;
    return true;

}

bool WebSocketPool::forward_connections(PendingWebsocket && request) {
    std::lock_guard lock(mutex_);
    if (!initialized_ ||stopping ||websocket_workers_.empty()) {return false;
    }
    const std::size_t index =next_worker_ %websocket_workers_.size();
    ++next_worker_;
    websocket_workers_[index]->transferSession(std::move(request));
    std::cout<<"收到websocket请求，转交给worker:"<<index<<std::endl;
    return true;
}
void WebSocketPool::close() {
    std::vector<std::unique_ptr<WebsocketWorker>> workersToDestroy;
    {
        std::lock_guard lock(mutex_);
        if (stopping &&websocket_workers_.empty())
            return;
        stopping = true;
        initialized_ = false;
        next_worker_ = 0;
        online_connections_.clear();
        workersToDestroy.swap(websocket_workers_);
    }
    std::cout<<"Pool关闭"<<std::endl;
    workersToDestroy.clear();
}
void WebSocketPool::addIndex(const boost::uuids::uuid &device_id,const boost::uuids::uuid &user_id,std::size_t worker_id,int fd,std::uint64_t session_id) {
    std::lock_guard lock(mutex_);
    if (stopping || !initialized_ || worker_id >= websocket_workers_.size()) return;
    OnlineConnection connection{worker_id,fd,session_id,user_id};
    online_connections_.insert_or_assign(device_id, connection);
}
void WebSocketPool::removeIndex(const boost::uuids::uuid &device_id,std::size_t worker_id,int fd,std::uint64_t session_id) {
    std::lock_guard lock(mutex_);
    const auto element = online_connections_.find(device_id);
    if (element == online_connections_.end()) {
        return;
    }
    const auto& connection = element->second;
    if (connection.worker_id == worker_id &&connection.fd == fd &&connection.session_id == session_id) {
        online_connections_.erase(element);
        }
}

void WebSocketPool::push_To_Device(const boost::uuids::uuid &device_id,  nlohmann::json message) {
    std::lock_guard lock(mutex_);

    if (stopping || !initialized_)
        return;
    const auto element = online_connections_.find(device_id);
    if (element == online_connections_.end()) {
        return; // 设备离线，之后通过同步补取。
    }
    const OnlineConnection connection = element->second;
    if (connection.worker_id >= websocket_workers_.size())
        return;

    websocket_workers_[connection.worker_id]->enqueuePush(connection.fd,connection.session_id,device_id,std::move(message));
}
void WebSocketPool::push_To_User(const boost::uuids::uuid& user_id, nlohmann::json message) {
    std::lock_guard lock(mutex_);
    if (stopping || !initialized_) return;
    for (const auto& [deviceId, connection] : online_connections_) {
        if (connection.user_id == user_id && connection.worker_id < websocket_workers_.size()) {
            // Copy for each device: moving would empty the next device's notification.
            websocket_workers_[connection.worker_id]->enqueuePush(
                connection.fd, connection.session_id, deviceId, message);
        }
    }
}
bool WebSocketPool::closeSession(const boost::uuids::uuid &user_id,const boost::uuids::uuid &device_id) {
    std::vector<std::future<void>> completions;
    bool allQueued = true;
    {
        std::lock_guard lock(mutex_);
        if (stopping || !initialized_) return false;
        for (auto& worker : websocket_workers_) {
            try {
                completions.push_back(worker->revokeDeviceConnections(user_id, device_id));
            } catch (const std::exception&) { allQueued = false; }
        }
    } // Workers need this mutex for removeIndex; never hold it while waiting.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
    for (auto& completion : completions) {
        try {
            if (completion.wait_until(deadline) != std::future_status::ready)
                allQueued = false;
            else completion.get();
        } catch (const std::exception&) { allQueued = false; }
    }
    return allQueued;
}
