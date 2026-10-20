//
// Created by yangb on 2026/9/28.
//

#ifndef CPP_BACKEND_SERVER_SERVICETHREADPOOL_H
#define CPP_BACKEND_SERVER_SERVICETHREADPOOL_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <nlohmann/json_fwd.hpp>


#include "service/ChatService.h"

/** @brief 执行可能查库的业务 Task；互斥队列取出后释放锁再执行。回复回投 Worker，不能在业务线程改 Session。 */
class ServiceThreadPool {
public:
    using Task=std::function<void()>;
    ChatService& chatService;
    ServiceThreadPool(int threadCount, ChatService& chatService);
    void init();
    void receiveTask(Task task);
    // 停止接收后排空已入队任务并 join；由所有者线程调用，不能在自己的业务 Task 内等待自身退出。
    void close();
private:
    int threadCount=0;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Task> tasks_;
    std::vector<std::thread> threads_;
    bool stop_=false;

};


#endif //CPP_BACKEND_SERVER_SERVICETHREADPOOL_H
