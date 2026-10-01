#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <vector>
#include "common/UniqueFd.h"
#include "server/TcpServer.h"
#include "handler/Router.h"
#include "websocket/WebSocketPool.h"

struct ThreadPoolConfig {
    int min_capacity; // Fixed worker count.
    int max_capacity; // Queue capacity, not dynamic thread count.
};
class ThreadPool {
public:
    ThreadPool(const ThreadPoolConfig&, TcpServer&, Router&, WebSocketPool&);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    bool init();
    void running(const std::function<bool()>& stopRequested = {});
    void requestStop(); // Thread-safe, no join.
    void close(); // Owner thread only, after running() returns; repeatable.
    bool is_Running();
private:
    void worker(std::size_t index);
    std::queue<UniqueFd> task_queue;
    std::vector<std::thread> threads;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::set<int> active_fds_;
    bool initialized_ = false;
    bool is_running = false;
    bool stopping = true;
    ThreadPoolConfig config_;
    TcpServer& server_;
    Router& router_;
    WebSocketPool &webSocketPool_;
};
