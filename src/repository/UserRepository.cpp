//
// Created by yangb on 2026/9/2.
//

#include "repository/UserRepository.h"

#include <charconv>
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
            p.password_hash
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
        .password_hash = std::string{result.value(0, 3)}
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
std::optional<User> UserRepository::CreateNewUserByEmail(const std::string &email,const std::string & password_hash, const std::string & display_name) {
    ConnectionLease lease(databasePool_);

    return lease.connection().withTransaction([&]() -> User {
        //这是一个lambda函数，捕获this,不接受参数，返回User，他先把结果返回给withTransaction，然后再返回给service
        const std::string userParameters[] = {display_name};
        auto result = lease.connection().execute(
            R"(
                INSERT INTO app.users (display_name)
                VALUES ($1)
                RETURNING id, display_name
            )",
            userParameters
        );
        const std::string userIdText{
            result.value(0, 0)
        };
        User user{
            .id = boost::uuids::string_generator{}(userIdText),
            .display_name = std::string{
                result.value(0, 1)
            }
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
            )",
            credentialParameters
        );

        return user;
    });

}