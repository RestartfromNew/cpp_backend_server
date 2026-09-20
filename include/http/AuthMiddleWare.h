//
// Created by yangb on 2026/9/17.
//

#ifndef CPP_BACKEND_SERVER_AUTHMIDDLEWARE_H
#define CPP_BACKEND_SERVER_AUTHMIDDLEWARE_H
#include "Auth/AccessTokenService.h"
#include "boost/uuid.hpp"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
enum class AuthError {
    UnAuthorized,
    Expired
};
struct AuthResult {
    std::variant<boost::uuids::uuid,AuthError> result;
};
class AuthMiddleWare {

    public:
    explicit AuthMiddleWare(AccessTokenService &accessTokenService);
    ~AuthMiddleWare()=default;
    AuthResult authenticate(const HttpRequest &request)const;
private:
    AccessTokenService &accessTokenService_;
};


#endif //CPP_BACKEND_SERVER_AUTHMIDDLEWARE_H
