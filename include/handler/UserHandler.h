//
// Created by yangb on 2026/9/8.
//

#ifndef CPP_BACKEND_SERVER_USERHANDLER_H
#define CPP_BACKEND_SERVER_USERHANDLER_H


#include "http/HttpRequest.h"
#include "service/UserService.h"
#include "nlohmann/json.hpp"
#include "http/HttpResponse.h"
#include <service/RegisterService.h>
#include "service/LoginService.h"
#include <boost/uuid/uuid_io.hpp>




class UserHandler {
public:
    explicit UserHandler(UserService& userService,RegisterService& registerService,LoginService& loginService);
    HttpResponse getUser(const HttpRequest& request);
    HttpResponse Register(const HttpRequest& request);
    HttpResponse LoginByEmail(const HttpRequest& request);
    HttpResponse verifyaccess(const HttpRequest& request);


private:
    std::string toLower(std::string str);
    std::string conversionInput(const std::string &input);
    //Handler 保存一个service示例
    UserService& userService_;
    RegisterService& registerService_;
    LoginService & loginService_;
};


#endif //CPP_BACKEND_SERVER_USERHANDLER_H
