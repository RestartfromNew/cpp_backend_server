//
// Created by yangb on 2026/9/2.
//

#ifndef CPP_BACKEND_SERVER_USERSERVICE_H
#define CPP_BACKEND_SERVER_USERSERVICE_H
#include <string_view>
#include <variant>
#include <functional>


#include "Auth/RefreshTokenService.h"
#include "Auth/AccessTokenService.h"
#include "domain/AvatarErrorCode.h"
#include "repository/UserRepository.h"
class RefreshTokenService;

enum class UserServiceErrorCode {
    UserNotFound,
    CannotAddSelf,
    InvalidPassword,
    RevokedDevice,
    NotYourDevice,
    DeviceNotFound,
    DeviceNotActive,
    NoSuchDevice,
    NotActivated,

};
/** @brief 内部凭据组合；不可写日志，也不能作为用户和设备已经通过验证的替代证据。 */
struct TokenPairs {
    std::string access_token;
    std::string refresh_token;
};

/** @brief 用户查询、好友操作及设备撤销业务；协调 Repository 并映射类型化结果，不操作 socket。 */
class UserService {
public:
    using AvatarChanged = std::function<void(const boost::uuids::uuid&, const boost::uuids::uuid&)>;

    explicit UserService(
        UserRepository& repository,
        RefreshTokenRepository& refresh_token_repository,
        AccessTokenService& access_token_service,
        RefreshTokenService& refresh_token_service
    );

    std::optional<User> findUserByEmail(const std::string& email);
    std::optional<User> findUserByUserName(const std::string& username);
    std::optional<FriendshipRequestRecord> requestFriendship(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& friend_uuid,const std::string &message);
    std::optional<std::vector<FriendshipRequestRecord>> fetchFriendshipRequest(const boost::uuids::uuid& my_uuid);
    std::vector<FriendRecord> fetchFriends(const boost::uuids::uuid& my_uuid);
    FriendRequestProcessResult processFriendshipRequest(const boost::uuids::uuid& my_uuid,
        const boost::uuids::uuid& request_id, const std::string& process);
    std::variant<bool,UserServiceErrorCode> revokeUserDevice(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& device_id,const std::string &password);
    std::variant<bool,UserServiceErrorCode> verifyUserDevice(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& device_id);

    std::variant<boost::uuids::uuid, AvatarErrorCode> uploadAvatar(const boost::uuids::uuid& my_uuid,const std::string_view jpeg_bytes);
    // 在启动 HTTP 和 WebSocket 工作线程之前配置。
    void setAvatarChangedHandler(AvatarChanged handler);

    std::optional<std::string> getAvatarByID(const boost::uuids::uuid& avatar_id, const boost::uuids::uuid& my_uuid);

private:
    bool verifyPassword( const std::string &password_hash, const std::string &password );
    UserRepository& repository_;
    AccessTokenService &accessTokenService_;
    RefreshTokenService &refreshTokenService_;
    RefreshTokenRepository refresh_token_repository_;
    AvatarChanged avatarChanged_;
};


#endif //CPP_BACKEND_SERVER_USERSERVICE_H
