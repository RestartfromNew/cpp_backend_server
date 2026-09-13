//
// Created by yangb on 2026/9/8.
//

#include "../../include/handler/UserHandler.h"
#include "handler/UserHandler.h"
#include "nlohmann/json.hpp"



UserHandler::UserHandler(UserService& userService): userService_(userService){}

HttpResponse UserHandler::getUser(const HttpRequest& request)
{
    const auto emailHeader =request.headers.find("X-User-Email");
    HttpResponse response;
    response.headers["Content-Type"] = "application/json";



    // 请求缺少必要参数。
    if (emailHeader == request.headers.end() ||emailHeader->second.empty()) {
       response.status=HttpStatus::Bad_Request;
        response=ErrorResponseMaker(HttpStatus::Bad_Request,"missing_user_email","Missing X-User-Email header");
        return response;
        }

    const std::string& email = emailHeader->second;

    auto user = userService_.findUserByEmail(email);

    // 查询成功，但没有对应用户。
    if (!user) {
       response=ErrorResponseMaker(HttpStatus::Not_Found,"user_not_found","User not found");
        return response;
    }

    // 查询到用户。
    response.status=HttpStatus::Ok;
    nlohmann::json body;
    body["id"] = user->id;
    body["email"] = user->email;
    body["username"] = user->username;

    response.body = body.dump();
    return response;
}