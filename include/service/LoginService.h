#ifndef CPP_BACKEND_SERVER_LOGINSERVICE_H
#define CPP_BACKEND_SERVER_LOGINSERVICE_H
#include "repository/UserRepository.h"
#include <string>
#include "service/LoginTicketService.h"

enum class LoginError {
    TooManyRequests, NoUserFound, InvalidCredentials, InternalError,
    ServiceUnavailable, NotActiveUser, Success
};
/** @brief 密码登录结果码及可选 ticket；只有 Success 才读取 ticket，原文只返回客户端。 */
struct LoginServiceResult {
    LoginError login;
    std::optional<std::string> login_ticket = std::nullopt;
};
/** @brief 验证密码与账号状态后签发短期 ticket；不直接签发正式 Token。数据库异常交 HTTP 边界处理。 */
class LoginService {
public:
    LoginService(UserRepository& userRepository, LoginTicketService& tickets);
    ~LoginService() = default;
    LoginServiceResult loginByEmail(const std::string& email, const std::string& password);
private:
    UserRepository& userRepository_;
    LoginTicketService& tickets_;
    bool verifyPassword(const std::string& password_hash, const std::string& password);
};
#endif
