//
// Created by yangb on 2026/11/3.
//

#ifndef CPP_BACKEND_SERVER_DISKPOOL_H
#define CPP_BACKEND_SERVER_DISKPOOL_H
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>

#include "domain/UploadPermission.h"


class DiskPool {
    public:
    using Task=std::function<void()>;
    DiskPool(int thread_num,int max_capacity);
    ~DiskPool();
    DiskPool(const DiskPool&) = delete;
    DiskPool& operator=(const DiskPool&) = delete;
    DiskPool(DiskPool&&) = delete;
    DiskPool& operator=(DiskPool&&) = delete;
    void init();
    void close();
    [[nodiscard]]
    bool receiveTask(Task task);



private:
    void worker(int index);
    std::queue<Task> task_queue;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> threads;
    int max_capacity_;
    int thread_num_;
    bool started_=false;
    bool stopping_=true;
};


#endif //CPP_BACKEND_SERVER_DISKPOOL_H
