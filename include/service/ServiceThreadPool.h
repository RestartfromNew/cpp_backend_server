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
    ChatService chatService;
    int i=1;
    std::string input="hello";
    Task task=[this,id=std::move(input), index=i]()mutable {
        chatService.printId(id,input);
    };
    ServiceThreadPool(int threadCount);
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
