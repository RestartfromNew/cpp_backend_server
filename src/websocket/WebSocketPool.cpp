//
// Created by yangb on 2026/9/27.
//

#include "websocket/WebSocketPool.h"

#include <stdexcept>

WebSocketPool::WebSocketPool(const WebSocketPoolConfig &config,WebSocketDispatcher &dispatcher):dispatcher_(dispatcher),config_(config) {

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
            workers.push_back(std::make_unique<WebsocketWorker>(dispatcher_));
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
        workersToDestroy.swap(websocket_workers_);
    }
    std::cout<<"Pool关闭"<<std::endl;
    workersToDestroy.clear();
}
