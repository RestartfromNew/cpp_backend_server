//
// Created by yangb on 2026/9/2.
//

#include "repository/UserRepository.h"
#include <string>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include "databases/DatabaseError.h"
//知道数据库结果，把数据库转换成领域里的对象
namespace {
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

std::optional<User> UserRepository::findByEmail(const std::string& email)
{
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
void UserRepository::InsertRefreshToken(const boost::uuids::uuid &id,std::string &refreshToken_hash) {
    const std::string refresh_Parameters[] = {boost::uuids::to_string(id),refreshToken_hash};
    ConnectionLease lease(databasePool_);
    auto result=lease.connection().execute(
    R"(
        INSERT INTO app.refresh_tokens (
            user_id,
            token_hash,
            expires_at
        )
        VALUES (
            $1,
            $2,
            CURRENT_TIMESTAMP + INTERVAL '7 days'
        )
    )",
    refresh_Parameters);

}
std::optional<User> UserRepository::CreateNewUserByEmail(const std::string &email,const std::string & password_hash, const std::string & display_name,const std::string &username) {
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
            )", credentialParameters
        );

        return user;
    });

}
std::optional<User> UserRepository::FindUserByUsername(const std::string &username) {
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
void UserRepository::CreateNewFriendship(const boost::uuids::uuid &my_id,const boost::uuids::uuid &friend_id,const std::string &message) {
    const std::string Parameters[] = {boost::uuids::to_string(my_id),boost::uuids::to_string(friend_id),"pending",message};
    ConnectionLease lease(databasePool_);
    auto result=lease.connection().execute(
        R"(
            INSERT INTO app.friend_request (
                requester_id,
                recipient_id,
                status,
                message
            )
            VALUES ($1,$2,$3,$4)
        )",Parameters);
}

std::optional<std::vector<FriendshipRequestRecord>> UserRepository::FetchUnprocessedRequest(const boost::uuids::uuid &my_id) {
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
            fr.created_at
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
        const auto seconds = std::stoll(std::string{result.value(i, 5)});
        const auto duration =std::chrono::duration_cast<TimePoint::duration>(std::chrono::seconds{seconds});
        request_summary.push_back(FriendshipRequestRecord{
            .request_id = boost::uuids::string_generator{}(std::string{result.value(i, 0)}),
            .id = boost::uuids::string_generator{}(std::string{result.value(i, 1)}),
            .username = std::string{result.value(i, 2)},
            .display_name = std::string{result.value(i, 3)},
            .message = result.isNull(i, 4)? std::string{}: std::string{result.value(i, 4)},
            .request_time = TimePoint{duration}
        });
    }
    return request_summary;
}
std::vector<User> UserRepository::FetchFriends(const boost::uuids::uuid& my_id) {
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
        )",  update);
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