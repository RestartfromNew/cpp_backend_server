//
// Created by yangb on 2026/9/8.
//

#ifndef CPP_BACKEND_SERVER_USERHANDLER_H
#define CPP_BACKEND_SERVER_USERHANDLER_H


#include "http/HttpRequest.h"
#include "service/UserService.h"

class UserHandler {
public:
    explicit UserHandler(UserService& userService);
    void getUser(const HttpRequest& request);

private:
    UserService& userService_;
};


#endif //CPP_BACKEND_SERVER_USERHANDLER_H
