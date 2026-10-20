#include "common/Logger.h"
//
// Created by yangb on 2026/9/28.
//

#include "service/ServiceThreadPool.h"

#include <stdexcept>
#include <iostream>
ServiceThreadPool::ServiceThreadPool(int count, ChatService& service)
    : chatService(service), threadCount(count) {
    if (count <= 0) throw std::invalid_argument("Thread count must be positive");
}

void ServiceThreadPool::init() {
    backend::logging::Scope operationLog{"ServiceThreadPool", "init"};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!threads_.empty()) {
            return;
        }
        stop_ = false;
    }
    threads_.reserve(threadCount);
    for (std::size_t index = 0; index < static_cast<std::size_t>(threadCount); ++index) {
        threads_.emplace_back([this] {
            while (true) {
                Task task;

                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this] {return stop_ || !tasks_.empty();});
                    if (stop_ && tasks_.empty()) {
                        return;
                    }
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }

                try {
                    task();
                } catch (const std::exception&) {
                    backend::logging::Logger::write(backend::logging::Level::Error, "business_pool", "task_exception");
                    std::cerr << "[ServiceThreadPool] task failed\n";
                } catch (...) {
                    backend::logging::Logger::write(backend::logging::Level::Error, "business_pool", "task_unknown_exception");
                    std::cerr << "[ServiceThreadPool] unknown task failure\n";
                }
            }
        });
    }
}

void ServiceThreadPool::receiveTask(Task task) {
    backend::logging::Scope operationLog{"ServiceThreadPool", "receiveTask"};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            backend::logging::Logger::write(backend::logging::Level::Warn, "business_pool", "enqueue_rejected");
            throw std::runtime_error("ServiceThreadPool is stopping");
        }
        tasks_.push(std::move(task));
    }

    cv_.notify_one();
}

void ServiceThreadPool::close() {
    backend::logging::Scope operationLog{"ServiceThreadPool", "close"};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }

    cv_.notify_all();

    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    threads_.clear();
}
