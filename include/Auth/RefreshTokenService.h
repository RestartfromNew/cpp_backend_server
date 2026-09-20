//
// Created by yangb on 2026/9/16.
//

#ifndef CPP_BACKEND_SERVER_REFRESHTOKENSERVICE_H
#define CPP_BACKEND_SERVER_REFRESHTOKENSERVICE_H

#include "string"
#include <chrono>
#include <cstddef>
#include <sodium.h>
#include "repository/RefreshTokenRepository.h"
#include "boost/uuid/string_generator.hpp"
#include "variant"
enum RefreshTokenStatus {
    Invalid,
    Expired
};
class RefreshTokenService {
public:
    RefreshTokenService(RefreshTokenRepository &refreshTokenRepository);
    ~RefreshTokenService()=default;
    //生成新的token
    std::string generateRefreshToken();
    //验证旧的token是否过期和合法，如果过期就生成新的token
    std::variant<std::string,bool> fleshRefreshToken(const std::string& refreshToken_old);
    //定时检查并删除失效token
    std::size_t deleteExpiredBefore(std::chrono::system_clock::time_point cutoff);
    std::string RefreshTokenHash(const std::string& refreshToken_origin);
    std::variant<RefreshTokenStatus,std::string> validate (const std::string& refreshToken_origin);
private:
    RefreshTokenRepository &refreshTokenRepository_;
};


#endif //CPP_BACKEND_SERVER_REFRESHTOKENSERVICE_H
