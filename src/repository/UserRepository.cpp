//
// Created by yangb on 2026/9/2.
//

#include "repository/UserRepository.h"

#include <charconv>
#include <string>
#include "databases/DatabaseError.h"
//知道数据库结果，把数据库转换成领域里的对象
namespace {
    //这个命名空间是cpp内部的，只在这里使用

    std::int64_t parseId(std::string_view text)
    {
        std::int64_t value{};

        const auto [end, error] = std::from_chars(
            //从text的开头读到text的结尾，然后把他转换成value
            text.data(),
            text.data() + text.size(),
            value
        );

        if (
            error != std::errc{} ||
            end != text.data() + text.size()
        ) {
            throw DatabaseError(
                "PostgreSQL returned an invalid user id"
            );
        }

        return value;
    }

} // namespace

UserRepository::UserRepository(
    DatabaseConnection& database
)
    : database_(database)
{
}

std::optional<User>
UserRepository::findByEmail(
    const std::string& email
)
{
    const std::string parameters[] = {
        email
        //构造一个字符串数组
    };
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

    if (result.rowCount() == 0) {
        //如果没结果返回nullopt
        return std::nullopt;
    }

    return User{
        .id = parseId(
            //将返回来的字符数据转化为目标需要的类型
            result.value(0, 0)

        ),
        .username = std::string{
            //为什么要复制一份返回，因为是DatabaseResult内部的引用，他会被析构
            result.value(0, 1)
        },
        .email = std::string{
            result.value(0, 2)
        }
    };
}