//
// Created by yangb on 2026/9/16.
//

#include "repository/RefreshTokenRepository.h"

namespace {

    TimePoint parseTimePoint(std::string_view text)
    {
        long long seconds = 0;
        // 把文本，例如 "1790000000"，转换成整数。
        const auto [end, error] = std::from_chars(text.data(),text.data() + text.size(),seconds);
        if (error != std::errc{} ||end != text.data() + text.size()) {
            throw std::runtime_error("Invalid database timestamp");
        }
        // 把 Unix 秒数转换成 system_clock 使用的时间单位。
        const auto duration =std::chrono::duration_cast<TimePoint::duration>(std::chrono::seconds{seconds});
        return TimePoint{duration};
    }

}
RefreshTokenRepository::RefreshTokenRepository(DatabaseConnection& database):database_(database){}
//查
std::optional<RefreshTokenResult> RefreshTokenRepository::getRefreshTokenRecord(const std::string &refreshToken_hash) {
    const std::string parameters[]={refreshToken_hash};
    auto result=database_.execute(R"(
        SELECT
            id,
            user_id,
            token_hash,
            floor(EXTRACT(EPOCH FROM expires_at))::bigint,
            floor(EXTRACT(EPOCH FROM revoked_at))::bigint,
            floor(EXTRACT(EPOCH FROM created_at))::bigint
        FROM app.refresh_tokens
        WHERE token_hash = $1
        LIMIT 1
    )",parameters);
    if (result.rowCount()==0){return std::nullopt;}
    std::string_view text_id = result.value(0, 0);
    std::string_view text_user_id = result.value(0, 1);
    return RefreshTokenResult{
        .id=boost::uuids::string_generator{}(text_id.begin(),text_id.end()),
        .user_id = boost::uuids::string_generator{}(text_user_id.begin(),text_user_id.end()),
        .token_hash = std::string{result.value(0,2)},
        .expires_at= parseTimePoint(result.value(0,3)),
        .revoked_at = result.isNull(0, 4)? std::optional<TimePoint>{}: std::optional<TimePoint>{parseTimePoint(result.value(0, 4))},
        .created_at= parseTimePoint(result.value(0,5)),
    };
}
//增
void RefreshTokenRepository::insertRefreshToken(const boost::uuids::uuid& userId,const std::string& refreshToken_hash,TimePoint expiresAt) {
    const auto expiresAtSeconds =std::chrono::duration_cast<std::chrono::seconds>(expiresAt.time_since_epoch()).count();
    const std::string refresh_Parameters[] = {boost::uuids::to_string(userId),refreshToken_hash,std::to_string(expiresAtSeconds)};
    auto result=database_.execute(R"(
        INSERT INTO app.refresh_tokens (
            user_id,
            token_hash,
            expires_at
        )
        VALUES (
            $1,
            $2,
            to_timestamp($3::double precision)
        )
    )",refresh_Parameters);
}
//改 标记为失效
void RefreshTokenRepository::revokeRefreshToken(const std::string &refreshToken_hash) {
    const std::string refresh_Parameters[]={refreshToken_hash};
    auto result=database_.execute(R"(
        UPDATE app.refresh_tokens
        SET revoked_at = CURRENT_TIMESTAMP
        WHERE token_hash = $1
          AND revoked_at IS NULL
    )",
    refresh_Parameters);

}
//删
void RefreshTokenRepository::deleteRefreshToken(const std::string &refreshToken_hash) {
    const std::string refresh_Parameters[]={refreshToken_hash};
    auto result=database_.execute(R"(
    DELETE FROM app.refresh_tokensWHERE token_hash = $1)",refresh_Parameters);

}
bool RefreshTokenRepository::rotate(const std::string& oldTokenHash,const std::string& newTokenHash) {
    //更新token，必须一起成功或者一起回滚
    return database_.withTransaction([&]() -> bool {
        const std::string refresh_Parameters[]={oldTokenHash};
        auto result = database_.execute(
            R"(
                UPDATE app.refresh_tokens
                SET revoked_at = CURRENT_TIMESTAMP
                WHERE token_hash = $1
                  AND revoked_at IS NULL
                  AND expires_at > CURRENT_TIMESTAMP
                RETURNING user_id,
            EXTRACT(EPOCH FROM expires_at)::bigint;
            )",
            refresh_Parameters
        );
        if (result.rowCount() == 0) {return false;}
        const std::string userId{result.value(0, 0)};
        const std::string new_token_Parameters[] = {userId,newTokenHash, std::string{result.value(0, 1)}};
        auto result_insert=database_.execute(R"(
        INSERT INTO app.refresh_tokens (
            user_id,
            token_hash,
            expires_at
        )
        VALUES (
            $1,
            $2,
            to_timestamp($3::double precision)
        )
        )",new_token_Parameters);
        return true;
    });


}

// 放在 RefreshTokenRepository.cpp 中。
std::size_t RefreshTokenRepository::deleteExpiredBefore(TimePoint cutoff)
{
    const auto seconds =std::chrono::duration_cast<std::chrono::seconds>(cutoff.time_since_epoch()).count();
    const std::string parameters[] = {std::to_string(seconds)};
    auto result = database_.execute(
        R"(
            WITH deleted AS (
                DELETE FROM app.refresh_tokens
                WHERE expires_at < to_timestamp($1::double precision)
                RETURNING id
            )
            SELECT count(*) FROM deleted
        )",
        parameters
    );
    return std::stoull(
        std::string{result.value(0, 0)}
    );
}