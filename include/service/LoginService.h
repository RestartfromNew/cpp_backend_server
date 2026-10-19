#ifndef CPP_BACKEND_SERVER_LOGINSERVICE_H
#define CPP_BACKEND_SERVER_LOGINSERVICE_H
#include "repository/UserRepository.h"
#include <string>
#include "service/LoginTicketService.h"

enum class LoginError {
    TooManyRequests, NoUserFound, InvalidCredentials, InternalError,
    ServiceUnavailable, NotActiveUser, Success
};
struct LoginServiceResult {
    LoginError login;
    std::optional<std::string> login_ticket = std::nullopt;
};
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
