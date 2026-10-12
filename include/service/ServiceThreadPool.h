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

class ServiceThreadPool {
public:
    using Task=std::function<void()>;
    ChatService& chatService;
    ServiceThreadPool(int threadCount, ChatService& chatService);
    void init();
    void receiveTask(Task task);
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
