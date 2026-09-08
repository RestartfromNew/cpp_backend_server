//
// Created by yangb on 2026/9/2.
//

#ifndef CPP_BACKEND_SERVER_USERSERVICE_H
#define CPP_BACKEND_SERVER_USERSERVICE_H
#include "repository/UserRepository.h"

class UserService {
public:
    explicit UserService(
        UserRepository& repository
    );

    std::optional<User> findUserByEmail(
        const std::string& email
    );

private:
    UserRepository& repository_;
};


#endif //CPP_BACKEND_SERVER_USERSERVICE_H
