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
/** @brief 刷新 Token 的 HTTP 边界；解析凭据并映射服务结果，不将凭据写日志。 */
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
