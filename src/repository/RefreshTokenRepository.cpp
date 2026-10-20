#include "common/Logger.h"
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
RefreshTokenRepository::RefreshTokenRepository(DatabasePool &databasePool):databasePool_(databasePool){}
//查
std::optional<RefreshTokenResult> RefreshTokenRepository::getRefreshTokenRecord(const std::string &refreshToken_hash) {
    backend::logging::Scope operationLog{"RefreshTokenRepository", "getRefreshTokenRecord"};
    const std::string parameters[]={refreshToken_hash};
    ConnectionLease lease(databasePool_);
    auto result=lease.connection().execute(R"(
        SELECT
            rt.id, rt.user_id, rt.token_hash,
            floor(EXTRACT(EPOCH FROM rt.expires_at))::bigint,
            floor(EXTRACT(EPOCH FROM rt.revoked_at))::bigint,
            floor(EXTRACT(EPOCH FROM rt.created_at))::bigint
        FROM app.refresh_tokens AS rt
        JOIN app.user_devices AS ud
          ON ud.device_id = rt.device_id AND ud.user_id = rt.user_id
        JOIN app.users AS u ON u.id = rt.user_id
        WHERE rt.token_hash = $1 AND ud.status = 'active' AND u.is_active = TRUE
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
void RefreshTokenRepository::insertRefreshToken(DatabaseConnection& connection,
    const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id,
    const std::string& refreshToken_hash) {
    backend::logging::Scope operationLog{"RefreshTokenRepository", "insertRefreshToken"};
    const std::string parameters[] = {
        boost::uuids::to_string(user_id), boost::uuids::to_string(device_id), refreshToken_hash};
    auto result = connection.execute(R"(
        INSERT INTO app.refresh_tokens (user_id, device_id, token_hash, expires_at)
        VALUES ($1::uuid, $2::uuid, $3::text, clock_timestamp() + INTERVAL '7 days')
        RETURNING id
    )", parameters);
    if (result.rowCount() != 1) throw std::runtime_error("Refresh token insert failed");
}

//改 标记为失效
void RefreshTokenRepository::revokeRefreshToken(const std::string &refreshToken_hash) {
    backend::logging::Scope operationLog{"RefreshTokenRepository", "revokeRefreshToken"};
    const std::string refresh_Parameters[]={refreshToken_hash};
    ConnectionLease lease(databasePool_);
    auto result=lease.connection().execute(R"(
        UPDATE app.refresh_tokens
        SET revoked_at = CURRENT_TIMESTAMP
        WHERE token_hash = $1
          AND revoked_at IS NULL
    )",
    refresh_Parameters);

}
//删
void RefreshTokenRepository::deleteRefreshToken(const std::string &refreshToken_hash) {
    backend::logging::Scope operationLog{"RefreshTokenRepository", "deleteRefreshToken"};
    const std::string refresh_Parameters[]={refreshToken_hash};
    ConnectionLease lease(databasePool_);
    auto result=lease.connection().execute(R"(
    DELETE FROM app.refresh_tokens WHERE token_hash = $1)",refresh_Parameters);

}
bool RefreshTokenRepository::rotate(const std::string& oldTokenHash,const std::string& newTokenHash) {
    backend::logging::Scope operationLog{"RefreshTokenRepository", "rotate"};
    //更新token，必须一起成功或者一起回滚
    ConnectionLease lease(databasePool_);
    return lease.connection().withTransaction([&]() -> bool {
        const std::string refresh_Parameters[]={oldTokenHash};
        auto result = lease.connection().execute(
            R"(
                UPDATE app.refresh_tokens
                SET revoked_at = CURRENT_TIMESTAMP
                WHERE token_hash = $1
                  AND revoked_at IS NULL
                  AND expires_at > CURRENT_TIMESTAMP
                RETURNING user_id, EXTRACT(EPOCH FROM expires_at)::bigint, device_id
            )",
            refresh_Parameters
        );
        if (result.rowCount() == 0) {return false;}
        const std::string userId{result.value(0, 0)};
        const std::string new_token_Parameters[] = {userId,newTokenHash, std::string{result.value(0, 1)}, std::string{result.value(0, 2)}};
        auto result_insert=lease.connection().execute(R"(
        INSERT INTO app.refresh_tokens (
            user_id,
            token_hash,
            expires_at,
            device_id
        )
        VALUES (
            $1::uuid,
            $2,
            to_timestamp($3::double precision),
            $4::uuid
        )
        )",new_token_Parameters);
        return true;
    });


}

// 放在 RefreshTokenRepository.cpp 中。
std::size_t RefreshTokenRepository::deleteExpiredBefore(TimePoint cutoff)
{
    backend::logging::Scope operationLog{"RefreshTokenRepository", "deleteExpiredBefore"};
    const auto seconds =std::chrono::duration_cast<std::chrono::seconds>(cutoff.time_since_epoch()).count();
    const std::string parameters[] = {std::to_string(seconds)};
    ConnectionLease lease(databasePool_);
    auto result = lease.connection().execute(
        R"(
            WITH deleted AS (
                DELETE FROM app.refresh_tokens WHERE expires_at < to_timestamp($1::double precision)
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