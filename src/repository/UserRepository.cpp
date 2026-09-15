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

UserRepository::UserRepository(DatabaseConnection& database): database_(database){}

std::optional<User>
UserRepository::findByEmail(const std::string& email)
{
    //构造一个字符串数组
    const std::string parameters[] = {email};
    //表示第一个参数，就是email
    auto result = database_.execute(
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
std::optional<User> UserRepository::CreateNewUserByEmail(const std::string &email,const std::string & password_hash, const std::string & display_name) {
    return database_.withTransaction([&]() -> User {
        //这是一个lambda函数，捕获this,不接受参数，返回User，他先把结果返回给withTransaction，然后再返回给service
        const std::string userParameters[] = {display_name};
        auto result = database_.execute(
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
        auto tempResult=database_.execute(
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