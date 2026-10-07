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
#include <domain/FriendshipRequestRecord.h>
#include <stdexcept>

#include <exception>
#include <iostream>

#include "service/KeyService.h"


class UserHandler {
public:
    explicit UserHandler(UserService& userService,RegisterService& registerService,LoginService& loginService,KeyService& keyService);
    HttpResponse getUser(const HttpRequest& request);
    HttpResponse Register(const HttpRequest& request);
    HttpResponse LoginByEmail(const HttpRequest& request);
    HttpResponse FindUserByUsername(const HttpRequest& request,const boost::uuids::uuid&);
    HttpResponse RequestFriendship(const HttpRequest& request,const boost::uuids::uuid&);
    HttpResponse FetchUnprocessedFriendship(const HttpRequest& request,const boost::uuids::uuid&);
    HttpResponse FetchFriends(const HttpRequest& request,const boost::uuids::uuid& my_uuid);
    HttpResponse ProcessFriendshipRequest(const HttpRequest& request,
        const boost::uuids::uuid& my_uuid);
    HttpResponse verifyaccess(const HttpRequest& request);


private:
    std::string toLower(std::string str);
    std::string conversionInput(const std::string &input);
    //Handler 保存一个service示例
    UserService& userService_;
    RegisterService& registerService_;
    LoginService & loginService_;
    KeyService& keyService_;

};


#endif //CPP_BACKEND_SERVER_USERHANDLER_H
