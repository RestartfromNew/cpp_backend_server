//
// Created by yangb on 2026/9/28.
//

#include "service/ServiceThreadPool.h"

#include <stdexcept>
ServiceThreadPool::ServiceThreadPool(int threadCount):threadCount(threadCount) {};

void ServiceThreadPool::init() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!threads_.empty()) {
            return;
        }
        stop_ = false;
    }
    threads_.reserve(threadCount);
    for (std::size_t index = 0; index < threadCount; ++index) {
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

                task();
            }
        });
    }
}

void ServiceThreadPool::receiveTask(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            throw std::runtime_error("ServiceThreadPool is stopping");
        }
        tasks_.push(std::move(task));
    }

    cv_.notify_one();
}

void ServiceThreadPool::close() {
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
