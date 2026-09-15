//
// Created by yangb on 2026/9/2.
//

#ifndef CPP_BACKEND_SERVER_USERREPOSITORY_H
#define CPP_BACKEND_SERVER_USERREPOSITORY_H

#include <optional>
#include <string>
#include <exception>
#include "databases/DatabaseConnection.h"
#include "databases/DatabaseError.h"
#include "domain/User.h"
//负责接受service来的业务数据，包装成数据库需要形式，执行数据库连接和命令，返回service层处理需要的结果
//借用但是不拥有数据库连接，不负责析构
class UserRepository {
public:
    explicit UserRepository(DatabaseConnection& database);
    std::optional<User> findByEmail(const std::string& email);
    std::optional<User> CreateNewUserByEmail(const std::string &email,const std::string & password_hash, const std::string & display_name);
private:
    DatabaseConnection& database_;
};

#endif //CPP_BACKEND_SERVER_USERREPOSITORY_H
