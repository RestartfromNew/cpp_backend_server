//
// Created by yangb on 2026/9/8.
//

#ifndef CPP_BACKEND_SERVER_ROUTER_H
#define CPP_BACKEND_SERVER_ROUTER_H
#include <functional>
#include <string>
#include <vector>
#include "http/HttpRequest.h"
class Router {
public:
    using Handler =
        std::function<void(const HttpRequest&)>;

    void addRoute(
        HttpMethod method,
        std::string path,
        Handler handler
    );
    /**
     * 查找并执行匹配的 Handler。
     *
     * @return 找到路由时返回 true，否则返回 false。
     */
    [[nodiscard]]
    bool route(const HttpRequest& request) const;
private:
    struct Route {
        HttpMethod method;
        std::string path;
        Handler handler;
    };

    std::vector<Route> routes_;
};


#endif //CPP_BACKEND_SERVER_ROUTER_H
