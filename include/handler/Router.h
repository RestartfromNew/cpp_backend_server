//
// Created by yangb on 2026/9/8.
//

#ifndef CPP_BACKEND_SERVER_ROUTER_H
#define CPP_BACKEND_SERVER_ROUTER_H
#include <functional>
#include <string>
#include <vector>
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
class Router {
public:
    //给类型为：参数为HttpRequest&，返回值为void的可调用对象取一个名字Handler,每一个Handler handler的可调用对象都满足void(const HttpRequest&);的形式
    using Handler =std::function<HttpResponse(const HttpRequest&)>;

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
    HttpResponse route(const HttpRequest& request) const;
private:
    struct Route {
        //Method+path->决定用哪个handler
        HttpMethod method;
        std::string path;
        Handler handler;
    };

    std::vector<Route> routes_;
};


#endif //CPP_BACKEND_SERVER_ROUTER_H
