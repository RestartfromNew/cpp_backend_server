//
// Created by yangb on 2026/9/8.
//

#include "../../include/handler/UserHandler.h"
#include "handler/UserHandler.h"

#include <iostream>
#include <stdexcept>

UserHandler::UserHandler(
    UserService& userService
)
    : userService_(userService)
{
}

void UserHandler::getUser(
    const HttpRequest& request
)
{
    const auto emailHeader =
        request.headers.find("X-User-Email");

    if (emailHeader == request.headers.end()) {
        throw std::invalid_argument(
            "Missing X-User-Email header"
        );
    }

    const std::string& email =
        emailHeader->second;

    auto user =
        userService_.findUserByEmail(email);

    // 暂时用控制台输出验证链路。
    // 将来这里改成返回 HttpResponse。
    if (!user) {
        std::cout
            << "User not found: "
            << email
            << '\n';

        return;
    }

    std::cout
        << "User found\n"
        << "id: " << user->id << '\n'
        << "username: " << user->username << '\n'
        << "email: " << user->email << '\n';
}