//
// Created by yangb on 2026/9/14.
//

#ifndef CPP_BACKEND_SERVER_LOGINSERVICE_H
#define CPP_BACKEND_SERVER_LOGINSERVICE_H
#include "domain/User.h"
#include "string"
#include <boost/uuid/uuid.hpp>
#include <regex>
#include <sodium.h>
#include "nlohmann/json.hpp"
#include <repository/UserRepository.h>
#include <variant>

enum RegisterError{
    InvalidEmail,
    InvalidPassword,
    InvalidDisplayName,
    EmailExists,
    RegisterFailed,
};
struct RegisterResult {
    std::variant<User,RegisterError> result;
};
class RegisterService {
public:
    explicit RegisterService(UserRepository& repository);
    ~RegisterService()=default;

    RegisterResult RegisterUserByEmail(std::string email,std::string password, std::string display_name);
private:
    bool checkEmail(const std::string &email);
    bool checkDisplayName(const std::string &display_name);
    bool checkPassword(const std::string &password);
    std::string passwordHash(const std::string &password);
    UserRepository &userRepository_;

};


#endif //CPP_BACKEND_SERVER_LOGINSERVICE_H
