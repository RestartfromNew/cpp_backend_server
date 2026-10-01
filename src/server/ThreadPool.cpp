#include "server/ThreadPool.h"
#include "http/HttpSession.h"
#include <iostream>
#include <stdexcept>
#include <syncstream>
#include <sys/socket.h>

ThreadPool::ThreadPool(const ThreadPoolConfig& config, TcpServer& server, Router& router,WebSocketPool& webSocketPool)
    : config_(config), server_(server), router_(router), webSocketPool_(webSocketPool) {}
ThreadPool::~ThreadPool() { close(); }

bool ThreadPool::init() {
    {
        std::lock_guard lock(mutex_);
        if (initialized_) return false;
        if (config_.min_capacity <= 0 || config_.max_capacity <= 0)
            throw std::invalid_argument("Worker count and queue capacity must be positive");
        stopping = false;
        initialized_ = true;
    }
    try {
        threads.reserve(static_cast<std::size_t>(config_.min_capacity));
        for (int i = 0; i < config_.min_capacity; ++i)
            threads.emplace_back(&ThreadPool::worker, this, static_cast<std::size_t>(i));
    } catch (...) {
        close(); // Never join while holding the queue mutex.
        return false;
    }
    return true;
}
void ThreadPool::running(const std::function<bool()>& stopRequested) {
    {
        std::lock_guard lock(mutex_);
        if (!initialized_ || stopping || is_running)
            throw std::logic_error("Thread pool is not ready to run");
        is_running = true;
    }
    try {
        while (true) {
            if (stopRequested && stopRequested()) break;
            {
                std::lock_guard lock(mutex_);
                if (stopping) break;
            }
            auto fd = server_.acceptConnection(); // Bounded poll, nonblocking accept.
            if (!fd.valid()) continue;
            {
                std::lock_guard lock(mutex_);
                if (stopping) break;
                if (task_queue.size() >= static_cast<std::size_t>(config_.max_capacity))
                    continue; // RAII closes rejected connection.
                task_queue.push(std::move(fd));
            }
            condition_.notify_one();
        }
    } catch (...) {
        { std::lock_guard lock(mutex_); is_running = false; }
        requestStop();
        throw;
    }
    { std::lock_guard lock(mutex_); is_running = false; }
    requestStop();
}
void ThreadPool::requestStop() {
    {
        std::lock_guard lock(mutex_);
        stopping = true;
        while (!task_queue.empty()) task_queue.pop();
        // Interrupt I/O, but leave close to the owning worker.
        // Workers unregister under this same mutex BEFORE closing descriptors.
        for (int fd : active_fds_) ::shutdown(fd, SHUT_RDWR);
    }
    condition_.notify_all();
}
void ThreadPool::close() {
    requestStop();
    for (auto& thread : threads)
        if (thread.joinable()) thread.join();
    threads.clear();
}
void ThreadPool::worker(std::size_t index) {
    std::osyncstream(std::cout) << "[worker " << index << "] started; thread="
                              << std::this_thread::get_id() << '\n';
    while (true) {
        UniqueFd fd;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping || !task_queue.empty(); });
            if (stopping) break;
            fd = std::move(task_queue.front());
            task_queue.pop();
        }
        try {
            const int rawFd = fd.get();
            Connection connection{std::move(fd)};
            HttpSession session{std::move(connection), router_};
            {
                std::lock_guard lock(mutex_);
                if (stopping) continue;
                active_fds_.insert(rawFd);
            }
            try {
                std::optional<PendingWebsocket> result=session.HandleHttpSession();
                if (result.has_value()) {
                    webSocketPool_.forward_connections(std::move(*result));
                    std::lock_guard lock(mutex_);
                    active_fds_.erase(rawFd);
                }
            } catch (...) {
                // Includes shutdown cancellation. Do not let exceptions kill a worker.
            }
            {
                std::lock_guard lock(mutex_);
                active_fds_.erase(rawFd);
            }

        } catch (...) {
            std::osyncstream(std::cerr) << "[worker " << index << "] could not prepare connection\n";
        }
    }
}
bool ThreadPool::is_Running() {
    std::lock_guard lock(mutex_);
    return is_running;
}
