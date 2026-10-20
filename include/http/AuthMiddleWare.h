//
// Created by yangb on 2026/9/17.
//

#ifndef CPP_BACKEND_SERVER_AUTHMIDDLEWARE_H
#define CPP_BACKEND_SERVER_AUTHMIDDLEWARE_H
#include "Auth/AccessTokenService.h"
#include "boost/uuid.hpp"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
// 认证拒绝状态；成功分支是用户 UUID。设备权限另由 Session/Repository 校验。
enum class AuthError {
    UnAuthorized,
    Expired
};
/** @brief 认证结果联合体：UUID 表示用户身份，AuthError 表示拒绝；先判断分支再取值。 */
struct AuthResult {
    std::variant<boost::uuids::uuid,AuthError> result;
};
/** @brief 适配 HTTP 头或 Bearer 字符串到 Token 验证结果；不拥有认证服务，不执行设备权限查库。 */
class AuthMiddleWare {

    public:
    explicit AuthMiddleWare(AccessTokenService &accessTokenService);
    ~AuthMiddleWare()=default;
    AuthResult authenticate(const HttpRequest &request)const;
    AuthResult authenticate(const std::string& authorization) const;
private:
    AccessTokenService &accessTokenService_;
};


#endif //CPP_BACKEND_SERVER_AUTHMIDDLEWARE_H
