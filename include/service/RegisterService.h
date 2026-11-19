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
#include <array>
#include "domain/EmailVerifcationResult.h"

enum RegisterError{
    InvalidEmail,
    InvalidPassword,
    InvalidDisplayName,
    InvalidUsername,
    EmailExists,
    RegisterFailed,
    UsernameExists,
    CodeDoNotMatch, CodeExpired,ToMuchTry
};
/** @brief 账号注册业务返回值；状态与可选用户数据的含义由 Handler 映射为 HTTP 响应。 */
struct RegisterResult {
    std::variant<User,RegisterError> result;
};
/** @brief 账号注册校验与密码哈希业务；不处理 HTTP 编解码，密码和派生哈希均不得记录。 */
class RegisterService {
public:
    explicit RegisterService(UserRepository& repository);
    ~RegisterService()=default;

    RegisterResult RegisterUserByEmail( const std::string &email,const std::string & password,
        const std::string  &display_name,const std::string &username,const std::string &code);
    bool checkEmailInfoAvaliability(const std::string &register_info);
    bool checkUsernameInfoAvaliability(const std::string &register_info);
private:
    bool checkEmail(const std::string &email);
    bool checkDisplayName(const std::string &display_name);
    bool checkPassword(const std::string &password);
    bool checkUsername(const std::string &username);
    EmailCodeVerificationResult verifyCode(const std::string &email,const std::string &code);
    std::string passwordHash(const std::string &password);
    UserRepository &userRepository_;
    std::array<unsigned char, crypto_auth_hmacsha256_KEYBYTES> code_hmac_key_{};
};


#endif //CPP_BACKEND_SERVER_LOGINSERVICE_H
