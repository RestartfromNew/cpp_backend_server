//
// Created by yangb on 2026/9/2.
//

#ifndef CPP_BACKEND_SERVER_USERREPOSITORY_H
#define CPP_BACKEND_SERVER_USERREPOSITORY_H

#include <optional>
#include <string>

#include "databases/DatabaseConnection.h"
#include "domain/User.h"

class UserRepository {
public:
    explicit UserRepository(
        DatabaseConnection& database
    );

    [[nodiscard]]
    std::optional<User> findByEmail(
        const std::string& email
    );

private:
    DatabaseConnection& database_;
};

#endif //CPP_BACKEND_SERVER_USERREPOSITORY_H
