//
// Created by yangb on 2026/9/30.
//

#ifndef CPP_BACKEND_SERVER_WEBSOCKETDISPATCHER_H
#define CPP_BACKEND_SERVER_WEBSOCKETDISPATCHER_H
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <nlohmann/json.hpp>

class ServiceThreadPool;

class WebSocketDispatcher {
    public:
    using Task=std::function<void()>;
    using Reply = std::function<void(nlohmann::json)>;
    //这个东西负责协调两个池
    explicit WebSocketDispatcher(ServiceThreadPool &threadPool);
    void dispatch(nlohmann::json frame,Reply reply);
private:
    ServiceThreadPool &threadPool_;
};


#endif //CPP_BACKEND_SERVER_WEBSOCKETDISPATCHER_H
