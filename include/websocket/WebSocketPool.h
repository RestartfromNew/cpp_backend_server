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

class WebSocketDispatcher;

class WebSocketPool {
    public:
    using Task=std::function<void()>;
    WebSocketPool(const WebSocketPoolConfig &config,WebSocketDispatcher &dispatcher);
    ~WebSocketPool();
    bool init();
    void close();
    bool forward_connections(PendingWebsocket && request);
private:
    WebSocketDispatcher &dispatcher_;
    std::mutex mutex_;
    WebSocketPoolConfig config_;
    std::vector<std::unique_ptr<WebsocketWorker>> websocket_workers_;
    bool initialized_ = false;
    bool stopping = true;
    std::size_t next_worker_ = 0;
};


#endif //CPP_BACKEND_SERVER_WEBSOCKETPOOL_H
