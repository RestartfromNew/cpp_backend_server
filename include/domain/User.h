//
// Created by yangb on 2026/9/4.
//

#ifndef CPP_BACKEND_SERVER_USER_H
#define CPP_BACKEND_SERVER_USER_H
/**
 * @brief Represents a user in the application domain.
 *
 * Stores the basic user information retrieved from or persisted
 * to the database.
 */

#include <string>
struct User {
    std::int64_t id;
    std::string username;
    std::string email;
};


#endif //CPP_BACKEND_SERVER_USER_H
