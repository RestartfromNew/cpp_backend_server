//
// Created by yangb on 2026/9/16.
//

#include "Auth/AccessTokenService.h"
AccessTokenService::AccessTokenService(const std::string &secret):secret_(secret){}
std::string AccessTokenService::generateAccessToken(const std::string& userId) {
    const auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_type("JWT")
        .set_issuer("cpp_backend_server")
        .set_audience("cpp_backend_api")
        .set_subject(userId)
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::minutes{15})
        .sign(jwt::algorithm::hs256{secret_});
}

AccessTokenResult AccessTokenService::verify(const std::string& accessToken) const {
    try {
        const auto decoded=jwt::decode(accessToken);
        if (!decoded.has_expires_at() ||!decoded.has_subject()) {
            return AccessTokenError::Invalid;
        }
        //verifier是一个验证器对象
        auto verifier = jwt::verify()
           .allow_algorithm(jwt::algorithm::hs256{secret_})
           .with_issuer("cpp_backend_server")
           .with_audience("cpp_backend_api")
           .with_type("JWT");
        std::error_code error;
        verifier.verify(decoded, error);
        if (error) {
            return AccessTokenError::Invalid;
        }
        if (std::chrono::system_clock::now() >=decoded.get_expires_at()) {
            return AccessTokenError::Expired;
        }
        const auto userId =boost::uuids::string_generator{}(decoded.get_subject());
        return AuthIdentity{userId};

    }
    catch (const std::invalid_argument&) {
        // token 格式不正确，或 subject 不是合法 UUID。
        return AccessTokenError::Invalid;
    }
    catch (const std::runtime_error&) {
        // token 解码或字段读取失败。
        return AccessTokenError::Invalid;
    }
}
