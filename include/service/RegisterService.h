//
// Created by yangb on 2026/9/14.
//

#ifndef CPP_BACKEND_SERVER_REGISTERSERVICE_H
#define CPP_BACKEND_SERVER_REGISTERSERVICE_H
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
    InvalidUsername,
    EmailExists,
    RegisterFailed,
    UsernameExists,
};
struct RegisterResult {
    std::variant<User,RegisterError> result;
};
class RegisterService {
public:
    explicit RegisterService(UserRepository& repository);
    ~RegisterService()=default;

    RegisterResult RegisterUserByEmail( const std::string &email,const std::string & password,
        const std::string  &display_name,const std::string &username);
private:
    bool checkEmail(const std::string &email);
    bool checkDisplayName(const std::string &display_name);
    bool checkPassword(const std::string &password);
    bool checkUsername(const std::string &username);
    std::string passwordHash(const std::string &password);
    UserRepository &userRepository_;
};


#endif //CPP_BACKEND_SERVER_LOGINSERVICE_H
