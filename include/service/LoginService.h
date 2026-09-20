//
// Created by yangb on 2026/9/16.
//

#ifndef CPP_BACKEND_SERVER_LOGINSERVICE_H
#define CPP_BACKEND_SERVER_LOGINSERVICE_H
#include "http/HttpResponse.h"
#include "http/HttpRequest.h"
#include "domain/User.h"
#include "variant"
#include "repository/UserRepository.h"
#include <sodium.h>
#include <jwt-cpp/jwt.h>
#include <boost/uuid/uuid_io.hpp>
#include <cstdlib>
#include <stdexcept>
#include "Auth/AccessTokenService.h"
#include "Auth/RefreshTokenService.h"
#include <string>
enum LoginError {
    TooManyRequests,
    NoUserFound,
    InvalidCredentials,
    InternalError,
    ServiceUnavailable,
    NotActiveUser
};
struct UserToken {
    User user;
    std::string access_token;
    std::string refresh_token;
};
struct LoginServiceResult {
    std::variant<UserToken,LoginError> login;
};


class LoginService {
public:
    LoginService(UserRepository &userRepository, RefreshTokenService &refreshTokenService,AccessTokenService &accessTokenService);
    ~LoginService()=default;
    LoginServiceResult loginByEmail(const std::string &email, const std::string &password);
private:
    AccessTokenService &accessTokenService_;
    RefreshTokenService &refreshTokenService_;
    UserRepository &userRepository_;
    bool verifyPassword( const std::string &password_hash, const std::string &password );


};


#endif //CPP_BACKEND_SERVER_LOGINSERVICE_H
