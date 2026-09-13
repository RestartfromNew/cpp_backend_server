//
// Created by yangb on 2026/9/8.
//

#ifndef CPP_BACKEND_SERVER_USERHANDLER_H
#define CPP_BACKEND_SERVER_USERHANDLER_H


#include "http/HttpRequest.h"
#include "service/UserService.h"
#include "nlohmann/json.hpp"
#include "http/HttpResponse.h"

class UserHandler {
public:
    explicit UserHandler(UserService& userService);
    HttpResponse getUser(const HttpRequest& request);

private:
    //Handler 保存一个service示例
    UserService& userService_;
};


#endif //CPP_BACKEND_SERVER_USERHANDLER_H
