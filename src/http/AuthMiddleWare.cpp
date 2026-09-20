//
// Created by yangb on 2026/9/17.
//

#include "http/AuthMiddleWare.h"
AuthMiddleWare::AuthMiddleWare(AccessTokenService &accessTokenService):accessTokenService_(accessTokenService) {}
AuthResult AuthMiddleWare::authenticate(const HttpRequest &request)const{
    const auto iterator =request.headers.find("authorization");
    if (iterator == request.headers.end())
        return AuthResult{AuthError::UnAuthorized};
    const std::string& authorization = iterator->second;
    // 先用简单写法检查固定前缀。
    const std::string prefix = "Bearer ";
    if (!authorization.starts_with(prefix))
        return AuthResult{AuthError::UnAuthorized};
    const std::string access_token =authorization.substr(prefix.size());
    AccessTokenResult access_token_result=accessTokenService_.verify(access_token);
    if (std::holds_alternative<AccessTokenError>(access_token_result)) {
        const auto & error_type =std::get<AccessTokenError>(access_token_result);
        if (error_type==AccessTokenError::Invalid)
            return AuthResult{AuthError::UnAuthorized};
        if (error_type==AccessTokenError::Expired)
            return AuthResult{AuthError::Expired};
    }
    const boost::uuids::uuid authId=std::get<AuthIdentity>(access_token_result).userId;
    return AuthResult{authId};
}