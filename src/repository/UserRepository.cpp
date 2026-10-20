#include "common/Logger.h"
//
// Created by yangb on 2026/9/2.
//

#include "repository/UserRepository.h"
#include <string>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include "databases/DatabaseError.h"
//知道数据库结果，把数据库转换成领域里的对象
namespace {
    TimePoint parseRequestTime(std::string_view text) {
        std::int64_t microseconds = 0;
        const auto [end, error] = std::from_chars(
            text.data(), text.data() + text.size(), microseconds);
        if (error != std::errc{} || end != text.data() + text.size()) {
            throw DatabaseError(DatabaseErrorKind::DataConversion, "", "Invalid friend request timestamp");
        }
        return TimePoint{std::chrono::duration_cast<TimePoint::duration>(
            std::chrono::microseconds{microseconds})};
    }
    //这个命名空间是cpp内部的，只在这里使用
    boost::uuids::uuid parseId(std::string_view text)
    {
        // 将 UUID 文本解析为 boost::uuids::uuid。
        // 格式无效时，string_generator 会抛出异常。
        return boost::uuids::string_generator{}(
            text.begin(), text.end()
        );
    }

} // namespace

UserRepository::UserRepository(DatabasePool &databasePool): databasePool_(databasePool){}
std::optional<PasswordLoginRecord> UserRepository::FindLoginRecordByEmail(const std::string &email) {
    backend::logging::Scope operationLog{"UserRepository", "FindLoginRecordByEmail"};
    const std::string parameters[] = {email};
    ConnectionLease lease(databasePool_);
    auto result = lease.connection().execute(
        R"(
        SELECT
            u.id,
            u.display_name,
            u.is_active,
            p.password_hash,
            u.username
        FROM app.password_credentials AS p
        JOIN app.users AS u
            ON u.id = p.user_id
        WHERE lower(p.login_email) = lower($1)
    )",
        parameters
    );

    if (result.rowCount() == 0) {
        return std::nullopt;
    }
    return PasswordLoginRecord{
        .id = parseId(result.value(0, 0)),
        .display_name = std::string{result.value(0, 1)},
        .is_active = (result.value(0, 2) == "t"),
        .password_hash = std::string{result.value(0, 3)},
        .username = std::string{result.value(0, 4)}
    };
}
std::optional<std::string> UserRepository::FindLoginRecordByUserId(const boost::uuids::uuid &user_id) {
    backend::logging::Scope operationLog{"UserRepository", "FindLoginRecordByUserId"};
    const std::string parameters[] = {boost::uuids::to_string(user_id)};
    ConnectionLease lease(databasePool_);
    auto result = lease.connection().execute(
        R"(
        SELECT
            p.password_hash
        FROM app.password_credentials AS p
        WHERE p.user_id = $1::uuid
    )",
        parameters
    );
    if (result.rowCount() == 0) {
        return std::nullopt;
    }
    return  std::string{result.value(0, 0)};
}

std::optional<User> UserRepository::findByEmail(const std::string& email)
{
    backend::logging::Scope operationLog{"UserRepository", "findByEmail"};
    //构造一个字符串数组
    const std::string parameters[] = {email};
    ConnectionLease lease(databasePool_);
    //表示第一个参数，就是email
    auto result = lease.connection().execute(
        R"(
            SELECT id, username, email
            FROM users
            WHERE email = $1
            LIMIT 1
        )",
        parameters
    );

    //如果没结果返回nullopt
    if (result.rowCount() == 0) {return std::nullopt;}

    return User{.id = parseId(result.value(0, 0)),
        .display_name = std::string{result.value(0, 1)},
    };
}

std::optional<User> UserRepository::CreateNewUserByEmail(const std::string &email,const std::string & password_hash, const std::string & display_name,const std::string &username) {
    backend::logging::Scope operationLog{"UserRepository", "CreateNewUserByEmail"};
    ConnectionLease lease(databasePool_);

    return lease.connection().withTransaction([&]() -> User {
        //这是一个lambda函数，捕获this,不接受参数，返回User，他先把结果返回给withTransaction，然后再返回给service
        const std::string userParameters[] = {display_name,username};
        auto result = lease.connection().execute(
            R"(
                INSERT INTO app.users (display_name,username)
                VALUES ($1,$2)
                RETURNING id, display_name,username
            )",
            userParameters
        );
        const std::string userIdText{
            result.value(0, 0)
        };
        User user{
            .id = boost::uuids::string_generator{}(userIdText),
            .display_name = std::string{result.value(0, 1)},
            .username = std::string{result.value(0, 2)}
        };
        const std::string credentialParameters[] = {
            userIdText,
            email,
            password_hash
        };
        auto tempResult=lease.connection().execute(
            R"(
                INSERT INTO app.password_credentials (
                    user_id,
                    login_email,
                    password_hash
                )
                VALUES ($1, $2, $3)
            )",   credentialParameters
        );

        return user;
    });

}
std::optional<User> UserRepository::FindUserByUsername(const std::string &username) {
    backend::logging::Scope operationLog{"UserRepository", "FindUserByUsername"};
    const std::string Parameters[] = {username};
    ConnectionLease lease(databasePool_);
    //表示第一个参数，就是email
    auto result = lease.connection().execute(
        R"(
            SELECT id, display_name, username
            FROM users
            WHERE username = $1
            LIMIT 1
        )",
        Parameters
    );
    //如果没结果返回nullopt
    if (result.rowCount() == 0) {return std::nullopt;}
    return User{.id = parseId(result.value(0, 0)),
        .display_name = std::string{result.value(0, 1)},
        .username = std::string{result.value(0, 2)},
    };

}
RevokeDeviceResult UserRepository::revokeUserDevice(
    const boost::uuids::uuid& my_id, const boost::uuids::uuid& device_id) {
    backend::logging::Scope operationLog{"UserRepository", "revokeUserDevice"};
    const std::string parameters[] = {boost::uuids::to_string(device_id)};
    const std::string actor = boost::uuids::to_string(my_id);
    ConnectionLease lease{databasePool_};
    return lease.connection().withTransaction([&]() -> RevokeDeviceResult {
        // All dependent writes lock this row FOR SHARE and check active
        // in their own transaction. No table-wide or child-row locks needed.
        auto device = lease.connection().execute(R"(
            SELECT user_id, status
            FROM app.user_devices
            WHERE device_id = $1::uuid
            FOR UPDATE
        )", parameters);
        if (device.rowCount() == 0) return RevokeDeviceResult::DeviceNotFound;
        // Check ownership before exposing lifecycle state.
        if (device.value(0, 0) != actor) return RevokeDeviceResult::NotYourDevice;
        if (device.value(0, 1) == "revoked") return RevokeDeviceResult::AlreadyRevoked;
        if (device.value(0, 1) != "active") return RevokeDeviceResult::DeviceNotActive;
        auto updated = lease.connection().execute(R"(
            UPDATE app.user_devices
            SET status = 'revoked', revoked_at = CURRENT_TIMESTAMP
            WHERE device_id = $1::uuid AND status = 'active'
            RETURNING device_id
        )", parameters);
        if (updated.rowCount() != 1) {
            throw std::logic_error("Locked device was not revoked");
        }
        auto tokens = lease.connection().execute(R"(
            UPDATE app.refresh_tokens SET revoked_at = COALESCE(revoked_at, CURRENT_TIMESTAMP)
            WHERE device_id = $1::uuid
        )", parameters);
        // Preserve historical keys/envelopes. Device status controls access.
        return RevokeDeviceResult::Success;
    }); // Commit before success; database failures propagate.
}

FriendshipRequestRecord UserRepository::CreateNewFriendship(const boost::uuids::uuid &my_id,const boost::uuids::uuid &friend_id,const std::string &message) {
    backend::logging::Scope operationLog{"UserRepository", "CreateNewFriendship"};
    const std::string Parameters[] = {boost::uuids::to_string(my_id),boost::uuids::to_string(friend_id),"pending",message};
    ConnectionLease lease(databasePool_);
    auto result = lease.connection().execute(
    R"(
        WITH inserted AS (
            INSERT INTO app.friend_request (
                requester_id,
                recipient_id,
                status,
                message
            )
            VALUES ($1::uuid, $2::uuid, $3::text, $4::text)
            RETURNING id, requester_id, message, created_at
        )
        SELECT
            inserted.id AS request_id,
            u.id,
            u.username,
            u.display_name,
            COALESCE(inserted.message, '') AS message,
            (EXTRACT(EPOCH FROM inserted.created_at) * 1000000)::bigint AS request_time
        FROM inserted
        JOIN app.users AS u ON u.id = inserted.requester_id
    )",
    Parameters
);

    if (result.rowCount() != 1) {
        throw std::runtime_error(
            "Insert friend request returned unexpected row count"
        );
    }

    return FriendshipRequestRecord{
        .request_id = boost::uuids::string_generator{}(
            std::string{result.value(0, 0)}
        ),
        .id = boost::uuids::string_generator{}(
            std::string{result.value(0, 1)}
        ),
        .username = std::string{result.value(0, 2)},
        .display_name = std::string{result.value(0, 3)},
        .message = std::string{result.value(0, 4)},
        .request_time = parseRequestTime(result.value(0, 5))
    };

}

std::optional<std::vector<FriendshipRequestRecord>> UserRepository::FetchUnprocessedRequest(const boost::uuids::uuid &my_id) {
    backend::logging::Scope operationLog{"UserRepository", "FetchUnprocessedRequest"};
    const std::string Parameters[] = {boost::uuids::to_string(my_id),"pending"};
    ConnectionLease lease{databasePool_};
    auto result = lease.connection().execute(
        R"(
        SELECT
            fr.id AS request_id,
            u.id AS requester_id,
            u.username,
            u.display_name,
            fr.message,
            (EXTRACT(EPOCH FROM fr.created_at) * 1000000)::bigint AS request_time
        FROM app.friend_request AS fr
        JOIN app.users AS u
            ON u.id = fr.requester_id
        WHERE fr.recipient_id = $1
          AND fr.status = $2
        ORDER BY fr.created_at DESC, fr.id DESC
    )",
        Parameters
    );
    std::vector<FriendshipRequestRecord> request_summary;

    if (result.rowCount() == 0) {return std::nullopt;}
    request_summary.reserve(result.rowCount());

    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        request_summary.push_back(FriendshipRequestRecord{
            .request_id = boost::uuids::string_generator{}(std::string{result.value(i, 0)}),
            .id = boost::uuids::string_generator{}(std::string{result.value(i, 1)}),
            .username = std::string{result.value(i, 2)},
            .display_name = std::string{result.value(i, 3)},
            .message = result.isNull(i, 4)? std::string{}: std::string{result.value(i, 4)},
            .request_time = parseRequestTime(result.value(i, 5))
        });
    }
    return request_summary;
}
std::vector<User> UserRepository::FetchFriends(const boost::uuids::uuid& my_id) {
    backend::logging::Scope operationLog{"UserRepository", "FetchFriends"};
    const std::string Parameters[] = {boost::uuids::to_string(my_id)};
    ConnectionLease lease{databasePool_};
    auto result = lease.connection().execute(
        R"(
            SELECT u.id, u.display_name, u.username
            FROM app.friend_relation AS fr
            JOIN app.users AS u ON u.id = fr.friend_id
            WHERE fr.user_id = $1
            ORDER BY u.username, u.id
        )",
        Parameters
    );
    std::vector<User> friends;
    friends.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        friends.push_back(User{
            .id = parseId(result.value(i, 0)),
            .display_name = std::string{result.value(i, 1)},
            .username = std::string{result.value(i, 2)}
        });
    }
    return friends;
}

FriendRequestProcessResult UserRepository::processFriendshipRequest(
    const boost::uuids::uuid& my_uuid, const boost::uuids::uuid& request_id,
    FriendRequestAction action) {
    backend::logging::Scope operationLog{"UserRepository", "processFriendshipRequest"};
    std::string targetStatus;
    switch (action) {
        case FriendRequestAction::Accept: targetStatus = "accepted"; break;
        case FriendRequestAction::Reject: targetStatus = "rejected"; break;
        case FriendRequestAction::Cancel: targetStatus = "cancelled"; break;
        default: return FriendRequestProcessResult::InvalidAction;
    }
    const std::string lookup[] = {boost::uuids::to_string(request_id)};
    ConnectionLease lease{databasePool_};
    return lease.connection().withTransaction([&]() -> FriendRequestProcessResult {
        // Serialize accept/reject/cancel for the same request.
        auto record = lease.connection().execute(R"(
            SELECT requester_id, recipient_id, status
            FROM app.friend_request WHERE id = $1 FOR UPDATE
        )", lookup);
        if (record.rowCount() == 0) return FriendRequestProcessResult::NotFound;

        const auto actor = boost::uuids::to_string(my_uuid);
        const std::string requester{record.value(0, 0)};
        const std::string recipient{record.value(0, 1)};
        const std::string status{record.value(0, 2)};
        const auto& authorizedActor = action == FriendRequestAction::Cancel
            ? requester : recipient;
        if (actor != authorizedActor) return FriendRequestProcessResult::Forbidden;
        if (status == targetStatus) return FriendRequestProcessResult::AlreadyProcessed;
        if (status != "pending") return FriendRequestProcessResult::Conflict;

        const std::string update[] = {lookup[0], targetStatus};
        auto updated = lease.connection().execute(R"(
            UPDATE app.friend_request
            SET status = $2, processed_at = CURRENT_TIMESTAMP
            WHERE id = $1 AND status = 'pending'
            RETURNING id
        )",   update);
        if (updated.rowCount() != 1) return FriendRequestProcessResult::Conflict;

        if (action == FriendRequestAction::Accept) {
            const std::string members[] = {requester, recipient};
            auto inserted = lease.connection().execute(R"(
                INSERT INTO app.friend_relation (user_id, friend_id)
                VALUES ($1, $2), ($2, $1)
                ON CONFLICT (user_id, friend_id) DO NOTHING
            )",  members);
        }
        return FriendRequestProcessResult::Success;
    });
}
bool UserRepository::VerifyFriendShip(const boost::uuids::uuid &my_id,const boost::uuids::uuid &friend_id) {
    backend::logging::Scope operationLog{"UserRepository", "VerifyFriendShip"};
    const std::string Parameters[] = {boost::uuids::to_string(my_id),boost::uuids::to_string(friend_id)};
    ConnectionLease lease{databasePool_};
    auto result = lease.connection().execute(
       R"(
            SELECT fr.id
            FROM app.friend_relation AS fr
            WHERE fr.user_id = $1 and fr.friend_id = $2
        )",
       Parameters
   );
    if (result.rowCount() == 0) return false;
    return true;
}

bool UserRepository::VerifyUserDeviceRelation(const boost::uuids::uuid &my_id,const boost::uuids::uuid &device_id) {
    backend::logging::Scope operationLog{"UserRepository", "VerifyUserDeviceRelation"};
    const std::string parameters[] = {boost::uuids::to_string(my_id),boost::uuids::to_string(device_id)};
    ConnectionLease lease{databasePool_};
    auto result = lease.connection().execute(
        R"(
            SELECT
                CASE
                    WHEN ud.user_id = $1::uuid THEN 'true'
                    ELSE 'false'
                END AS belongs_to_user,
                CASE
                    WHEN ud.status = 'active'
                         AND u.is_active = TRUE THEN 'true'
                    ELSE 'false'
                END AS is_active
            FROM app.user_devices AS ud
            LEFT JOIN app.users AS u ON u.id = ud.user_id
            WHERE ud.device_id = $2::uuid
        )",
        parameters
    );

    if (result.rowCount() == 0) {
        throw DeviceRelationException{
            DeviceRelationError::NoSuchDevice
        };
    }

    // 先验证归属，再检查设备和账号状态。
    if (result.value(0, 0) != "true") {
        throw DeviceRelationException{
            DeviceRelationError::NotYourDevice
        };
    }

    if (result.value(0, 1) != "true") {
        throw DeviceRelationException{
            DeviceRelationError::NotActivated
        };
    }

    return true;
}
