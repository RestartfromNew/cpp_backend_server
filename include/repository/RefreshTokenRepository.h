//
// Created by yangb on 2026/9/16.
//

#ifndef CPP_BACKEND_SERVER_REFRESHTOKENREPOSITORY_H
#define CPP_BACKEND_SERVER_REFRESHTOKENREPOSITORY_H
#include "databases/DatabaseConnection.h"
#include "boost/uuid/uuid_io.hpp"
#include "databases/DatabaseError.h"
#include "string"
#include <chrono>
#include <optional>
#include <boost/uuid/string_generator.hpp>
#include <charconv>
#include <string_view>
#include <system_error>
#include "databases/DatabasePool.h"
#include "databases/ConnectionLease.h"
using TimePoint = std::chrono::system_clock::time_point;
struct RefreshTokenResult{
    boost::uuids::uuid id;
    boost::uuids::uuid user_id;
    std::string token_hash;
    TimePoint expires_at;
    std::optional<TimePoint> revoked_at;
    TimePoint created_at;
};

class RefreshTokenRepository {
    //负责是实现refresh token的数据库增删改查
    public:
    explicit RefreshTokenRepository(DatabasePool &databasePool);
    //查
    std::optional<RefreshTokenResult> getRefreshTokenRecord(const std::string &refreshToken_hash);
    //增
    void insertRefreshToken(const boost::uuids::uuid& userId,const std::string& refreshToken_hash,TimePoint expiresAt);
    //改 标记为失效
    void revokeRefreshToken(const std::string &refreshToken_hash);
    //删
    void deleteRefreshToken(const std::string &refreshToken_hash);
    bool rotate(const std::string& oldTokenHash,const std::string& newTokenHash);
    std::size_t deleteExpiredBefore(TimePoint cutoff);
 


private:
    DatabasePool &databasePool_;
};


#endif //CPP_BACKEND_SERVER_REFRESHTOKENREPOSITORY_H
