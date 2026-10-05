//
// Created by yangb on 2026/9/2.
//

#ifndef CPP_BACKEND_SERVER_USERSERVICE_H
#define CPP_BACKEND_SERVER_USERSERVICE_H
#include "repository/UserRepository.h"
enum class UserServiceErrorCode {
    UserNotFound,
    CannotAddSelf
};

class UserService {
public:
    explicit UserService(
        UserRepository& repository
    );

    std::optional<User> findUserByEmail(const std::string& email);
    std::optional<User> findUserByUserName(const std::string& username);
    bool requestFriendship(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& friend_uuid,const std::string &message);
    std::optional<std::vector<FriendshipRequestRecord>> fetchFriendshipRequest(const boost::uuids::uuid& my_uuid);
    std::vector<User> fetchFriends(const boost::uuids::uuid& my_uuid);
    FriendRequestProcessResult processFriendshipRequest(const boost::uuids::uuid& my_uuid,
        const boost::uuids::uuid& request_id, const std::string& process);

private:
    UserRepository& repository_;
};


#endif //CPP_BACKEND_SERVER_USERSERVICE_H
