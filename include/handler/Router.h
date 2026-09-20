//
// Created by yangb on 2026/9/8.
//

#ifndef CPP_BACKEND_SERVER_ROUTER_H
#define CPP_BACKEND_SERVER_ROUTER_H
#include <functional>
#include <string>
#include <vector>
#include <boost/uuid/uuid.hpp>

#include "http/AuthMiddleWare.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
class Router {
public:
    Router(const AuthMiddleWare &authMiddleWare);
    //给类型为：参数为HttpRequest&，返回值为void的可调用对象取一个名字Handler,每一个Handler handler的可调用对象都满足void(const HttpRequest&);的形式
    using Handler =std::function<HttpResponse(const HttpRequest&)>;
    using Protected_Handler=std::function<HttpResponse(const HttpRequest&,const boost::uuids::uuid&)>;
    void addRoute(HttpMethod method,std::string path,Handler handler);
    void addProtectedRoute(HttpMethod method,std::string path,Protected_Handler handler);
    /**
     * 查找并执行匹配的 Handler。
     *
     * @return 找到路由时返回 true，否则返回 false。
     */
    [[nodiscard]]
    HttpResponse route(const HttpRequest& request) const;
    HttpResponse protected_route(const HttpRequest& request) const;

private:
    struct Route {
        //Method+path->决定用哪个handler
        HttpMethod method;
        std::string path;
        Handler handler;
    };
    struct Protected_Route {
        HttpMethod method;
        std::string path;
        Protected_Handler protected_handler;
    };
    std::vector<Route> routes_;
    std::vector<Protected_Route> protected_routes_;
   const AuthMiddleWare &authMiddleWare_;
};


#endif //CPP_BACKEND_SERVER_ROUTER_H
