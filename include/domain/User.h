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
/** @brief 跨层用户资料值对象；序列化应显式选择对外字段，不能把内部认证数据混入响应。 */
struct User {
    boost::uuids::uuid id;
    std::string display_name;
    std::string username;
};


#endif //CPP_BACKEND_SERVER_USER_H
