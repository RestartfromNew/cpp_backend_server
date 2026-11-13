//
// Created by yangb on 2026/11/3.
//

#include "file/DiskPool.h"
#include "common/Logger.h"
DiskPool::DiskPool(int thread_num,int max_capacity):thread_num_(thread_num),max_capacity_(max_capacity) {
    init();
}
DiskPool::~DiskPool() {
    close();
}

void DiskPool::close() {
    backend::logging::Scope operationLog{"DiskPool", "close"};
    {
        std::lock_guard lock(mutex_);
        stopping_=true;
        //不要让任务未完成，reactor在等待
        // while (!task_queue.empty()) {
        //     task_queue.pop();
        // }

    }
    cv_.notify_all();
    for (auto& thread : threads)
        if (thread.joinable()) thread.join();
    threads.clear();
}

void DiskPool::init() {
    backend::logging::Scope operationLog{"DiskPool", "init"};
    std::unique_lock<std::mutex> lock{mutex_};
    if (thread_num_<=0||max_capacity_<=0) {
        throw std::invalid_argument{"invalid DiskPool configuration"};
    }
    threads.reserve(static_cast<size_t>(thread_num_));
    stopping_=false;
    try {
        for (int i=1;i<=thread_num_;i++) {
            threads.emplace_back([i,this]{
                worker(i);
            });
        }
        started_=true;
    }
    catch (...) {
        stopping_=true;
        lock.unlock();
        cv_.notify_all();
        for (auto&thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads.clear();
        throw;
    }
}
void DiskPool::worker(int index) {
    std::osyncstream(std::cout) << "[disk worker " << index << "] started; thread="
                             << std::this_thread::get_id() << '\n';
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock{mutex_};
            cv_.wait(lock,[this]{return stopping_||!task_queue.empty();});
            if (stopping_ && task_queue.empty()) break;
            task=std::move(task_queue.front());
            task_queue.pop();
            lock.unlock();
        }
        try {
            task();
        }
        catch (const std::exception& error) {
            std::osyncstream(std::cerr)
                << "[disk worker " << index << "] task failed: "
                << error.what() << '\n';
        }
        catch (...) {
            std::osyncstream(std::cerr)
                << "[disk worker " << index
                << "] task failed: unknown exception\n";
        }
    }
}
bool DiskPool::receiveTask(Task task) {
    std::unique_lock<std::mutex> lock{mutex_};
    if (stopping_||task_queue.size()>=max_capacity_) {
        return false;
    }
    task_queue.push(std::move(task));
    lock.unlock();
    cv_.notify_one();
    return true;
}

