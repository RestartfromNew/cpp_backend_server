//
// Created by yangb on 2026/9/17.
//

#ifndef CPP_BACKEND_SERVER_REFRESHTOKENHANDLER_H
#define CPP_BACKEND_SERVER_REFRESHTOKENHANDLER_H
#include "Auth/RefreshTokenService.h"
#include "nlohmann/json.hpp"
#include "Auth/AccessTokenService.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
class RefreshTokenHandler {
private:
    RefreshTokenService &refreshTokenService_;
    AccessTokenService &accessTokenService_;
public:
    explicit RefreshTokenHandler(RefreshTokenService &refreshTokenService,AccessTokenService &accessTokenService);
    ~RefreshTokenHandler()=default;
    HttpResponse VerifyRefreshToken(const HttpRequest& request);


};


#endif //CPP_BACKEND_SERVER_REFRESHTOKENHANDLER_H
