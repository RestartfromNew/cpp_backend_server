//
// Created by yangb on 2026/9/2.
//

#include "service/UserService.h"
UserService::UserService(
    UserRepository& repository
)
    : repository_(repository)
{
}

std::optional<User>
UserService::findUserByEmail(
    const std::string& email
)
{
    return repository_.findByEmail(email);
    //后面可能加上其它业务逻辑
}