//
// Created by yangb on 2026/9/2.
//

#ifndef CPP_BACKEND_SERVER_USERSERVICE_H
#define CPP_BACKEND_SERVER_USERSERVICE_H
#include <variant>

#include "repository/UserRepository.h"
enum class UserServiceErrorCode {
    UserNotFound,
    CannotAddSelf,
    InvalidPassword,
    RevokedDevice,
    NotYourDevice,
    DeviceNotFound,
    DeviceNotActive
};

class UserService {
public:
    explicit UserService(
        UserRepository& repository,
        RefreshTokenRepository& refresh_token_repository
    );

    std::optional<User> findUserByEmail(const std::string& email);
    std::optional<User> findUserByUserName(const std::string& username);
    bool requestFriendship(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& friend_uuid,const std::string &message);
    std::optional<std::vector<FriendshipRequestRecord>> fetchFriendshipRequest(const boost::uuids::uuid& my_uuid);
    std::vector<User> fetchFriends(const boost::uuids::uuid& my_uuid);
    FriendRequestProcessResult processFriendshipRequest(const boost::uuids::uuid& my_uuid,
        const boost::uuids::uuid& request_id, const std::string& process);
    std::variant<bool,UserServiceErrorCode> revokeUserDevice(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& device_id,const std::string &password);

private:
    bool verifyPassword( const std::string &password_hash, const std::string &password );
    UserRepository& repository_;
    RefreshTokenRepository refresh_token_repository_;
};


#endif //CPP_BACKEND_SERVER_USERSERVICE_H
