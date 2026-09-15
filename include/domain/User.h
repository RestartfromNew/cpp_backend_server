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
#include <boost/uuid/uuid.hpp>
//在系统中的实体，表示一个User
struct User {
    boost::uuids::uuid id;
    std::string display_name;
};


#endif //CPP_BACKEND_SERVER_USER_H
