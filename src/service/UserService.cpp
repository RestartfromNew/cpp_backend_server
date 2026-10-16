//
// Created by yangb on 2026/9/2.
//

#include "service/UserService.h"

#include <sodium/crypto_pwhash.h>

UserService::UserService(
    UserRepository& repository,
    RefreshTokenRepository& refresh_token_repository
)
    : repository_(repository), refresh_token_repository_(refresh_token_repository)
{
}
bool UserService::verifyPassword( const std::string &password_hash, const std::string &password ) {
    //password的hash会生成随机盐
    return crypto_pwhash_str_verify(password_hash.c_str(), // 数据库存储的哈希字符串
    password.data(),       // 用户本次输入的原始密码
    password.size()        // 密码字节数
    ) == 0;
}

std::optional<User>
UserService::findUserByEmail(const std::string& email)
{
    return repository_.findByEmail(email);
    //后面可能加上其它业务逻辑
}

std::optional<User> UserService::findUserByUserName(const std::string& username) {
    auto result=repository_.FindUserByUsername(username);
    return result;
}
bool UserService::requestFriendship(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& friend_uuid,const std::string& message)
{
    try {
        repository_.CreateNewFriendship(my_uuid,friend_uuid,message);
        return true;
    }
    catch (const DatabaseError& error) {
        if (error.kind() == DatabaseErrorKind::Query &&
            error.code() == "23505" &&
            error.constraint() == "friend_request_pending_pair_unique") {
            return false;
            } else {
                throw; // 其他数据库错误继续交给上层处理。
            }
    }

}
std::optional<std::vector<FriendshipRequestRecord>> UserService::fetchFriendshipRequest(const boost::uuids::uuid& my_uuid) {
    auto result=repository_.FetchUnprocessedRequest(my_uuid);
    return result;

}
std::vector<User> UserService::fetchFriends(const boost::uuids::uuid& my_uuid) {
    return repository_.FetchFriends(my_uuid);
}

FriendRequestProcessResult UserService::processFriendshipRequest(
    const boost::uuids::uuid& my_uuid, const boost::uuids::uuid& request_id,
    const std::string& process) {
    FriendRequestAction action;
    if (process == "accepted") action = FriendRequestAction::Accept;
    else if (process == "rejected") action = FriendRequestAction::Reject;
    else if (process == "cancelled") action = FriendRequestAction::Cancel;
    else return FriendRequestProcessResult::InvalidAction;

    // Database failures must propagate; never silently report success.
    return repository_.processFriendshipRequest(my_uuid, request_id, action);
}
std::variant<bool,UserServiceErrorCode> UserService::revokeUserDevice(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& device_id,const std::string &password) {

    auto passwordHashRecord=repository_.FindLoginRecordByUserId(my_uuid);
    if (passwordHashRecord==std::nullopt)
        return UserServiceErrorCode::UserNotFound;
    if (!verifyPassword(*passwordHashRecord, password))
        return UserServiceErrorCode::InvalidPassword;
    switch (repository_.revokeUserDevice(my_uuid, device_id)) {
        case RevokeDeviceResult::Success: {
            // Tokens are not device-bound yet; do not revoke other devices' tokens.
            return true;
        }

        case RevokeDeviceResult::DeviceNotFound:
            return UserServiceErrorCode::DeviceNotFound;
        case RevokeDeviceResult::NotYourDevice:
            return UserServiceErrorCode::NotYourDevice;
        case RevokeDeviceResult::AlreadyRevoked:
            return UserServiceErrorCode::RevokedDevice;
        case RevokeDeviceResult::DeviceNotActive:
            return UserServiceErrorCode::DeviceNotActive;
    }
    throw std::logic_error("Unhandled RevokeDeviceResult");
}
