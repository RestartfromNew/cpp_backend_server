#include "common/Logger.h"
//
// Created by yangb on 2026/9/2.
//

#include "service/UserService.h"

#include <memory>
#include <sodium/crypto_pwhash.h>
#include <system_error>
#include <turbojpeg.h>
#include <vector>

#include "service/SessionIdentityService.h"

UserService::UserService(
    UserRepository& repository,
    RefreshTokenRepository& refresh_token_repository,
    AccessTokenService& access_token_service,
        RefreshTokenService& refresh_token_service
)
    : repository_(repository), refresh_token_repository_(refresh_token_repository),
accessTokenService_(access_token_service), refreshTokenService_(refresh_token_service)
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
    backend::logging::Scope operationLog{"UserService", "findUserByEmail"};
    return repository_.findByEmail(email);
    //后面可能加上其它业务逻辑
}

std::optional<User> UserService::findUserByUserName(const std::string& username) {
    backend::logging::Scope operationLog{"UserService", "findUserByUserName"};
    auto result=repository_.FindUserByUsername(username);
    return result;
}
std::optional<FriendshipRequestRecord> UserService::requestFriendship(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& friend_uuid,const std::string& message)
{
    backend::logging::Scope operationLog{"UserService", "requestFriendship"};
    try {
        auto result= repository_.CreateNewFriendship(my_uuid,friend_uuid,message);
        return result;
    }
    catch (const DatabaseError& error) {
        if (error.kind() == DatabaseErrorKind::Query &&
            error.code() == "23505" &&
            error.constraint() == "friend_request_pending_pair_unique") {
            return std::nullopt;
            } else {
                throw; // 其他数据库错误继续交给上层处理。
            }
    }

}
std::optional<std::vector<FriendshipRequestRecord>> UserService::fetchFriendshipRequest(const boost::uuids::uuid& my_uuid) {
    backend::logging::Scope operationLog{"UserService", "fetchFriendshipRequest"};
    auto result=repository_.FetchUnprocessedRequest(my_uuid);
    return result;

}
std::vector<FriendRecord> UserService::fetchFriends(const boost::uuids::uuid& my_uuid) {
    backend::logging::Scope operationLog{"UserService", "fetchFriends"};
    return repository_.FetchFriends(my_uuid);
}

FriendRequestProcessResult UserService::processFriendshipRequest(
    const boost::uuids::uuid& my_uuid, const boost::uuids::uuid& request_id,
    const std::string& process) {
    backend::logging::Scope operationLog{"UserService", "processFriendshipRequest"};
    FriendRequestAction action;
    if (process == "accepted") action = FriendRequestAction::Accept;
    else if (process == "rejected") action = FriendRequestAction::Reject;
    else if (process == "cancelled") action = FriendRequestAction::Cancel;
    else return FriendRequestProcessResult::InvalidAction;

    // Database failures must propagate; never silently report success.
    return repository_.processFriendshipRequest(my_uuid, request_id, action);
}
std::variant<bool,UserServiceErrorCode> UserService::revokeUserDevice(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& device_id,const std::string &password) {
    backend::logging::Scope operationLog{"UserService", "revokeUserDevice"};

    auto passwordHashRecord=repository_.FindLoginRecordByUserId(my_uuid);
    if (passwordHashRecord==std::nullopt)
        return UserServiceErrorCode::UserNotFound;
    if (!verifyPassword(*passwordHashRecord, password))
        return UserServiceErrorCode::InvalidPassword;
    switch (repository_.revokeUserDevice(my_uuid, device_id)) {
        case RevokeDeviceResult::Success: {
            // Repository revokes only this device's bound refresh tokens in the same transaction.
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
std::variant<bool,UserServiceErrorCode> UserService::verifyUserDevice(const boost::uuids::uuid& my_uuid,const boost::uuids::uuid& device_id) {
    backend::logging::Scope operationLog{"UserService", "verifyUserDevice"};
    try {
        auto result=repository_.VerifyUserDeviceRelation(my_uuid,device_id);
        return result;
    }catch (const DeviceRelationException& error) {
        switch (error.error()) {
            case DeviceRelationError::NoSuchDevice:
                return UserServiceErrorCode::NoSuchDevice;

            case DeviceRelationError::NotActivated:
                return UserServiceErrorCode::NotActivated;

            case DeviceRelationError::NotYourDevice:
                return UserServiceErrorCode::NotYourDevice;
        }

        throw; // 未识别的分类，继续向上抛。
    }
}

namespace {
std::optional<AvatarErrorCode> validateAvatarJpeg(std::string_view jpeg_bytes) {
    constexpr std::size_t max_avatar_bytes = 256 * 1024;
    constexpr int avatar_side = 512;

    if (jpeg_bytes.empty() || jpeg_bytes.size() > max_avatar_bytes) {
        return AvatarErrorCode::InvalidSize;
    }

    // 任何提前返回都由 unique_ptr 释放 TurboJPEG 解码器。
    const std::unique_ptr<void, decltype(&tjDestroy)> decoder{tjInitDecompress(), &tjDestroy};
    if (!decoder) {
        return AvatarErrorCode::DecoderUnavailable;
    }

    const auto* data = reinterpret_cast<const unsigned char*>(jpeg_bytes.data());
    const auto size = static_cast<unsigned long>(jpeg_bytes.size()); // 已限制在 256 KiB 以内。
    int width = 0;
    int height = 0;
    int subsampling = 0;
    int colorspace = 0;
    if (tjDecompressHeader3(decoder.get(), data, size, &width, &height, &subsampling, &colorspace) != 0) {
        return AvatarErrorCode::ParseFailed;
    }
    if (width != avatar_side || height != avatar_side) {
        return AvatarErrorCode::InvalidSize;
    }

    // 尺寸通过后才分配像素缓冲。CMYK/YCCK JPEG 使用对应的解码输出格式。
    const int pixel_format = (colorspace == TJCS_CMYK || colorspace == TJCS_YCCK) ? TJPF_CMYK : TJPF_RGB;
    const auto pixel_bytes = static_cast<std::size_t>(avatar_side * avatar_side * tjPixelSize[pixel_format]);
    std::vector<unsigned char> pixels(pixel_bytes);
    const int flags = TJFLAG_STOPONWARNING | TJFLAG_LIMITSCANS;
    if (tjDecompress2(decoder.get(), data, size, pixels.data(), width, 0, height, pixel_format, flags) != 0) {
        return AvatarErrorCode::ParseFailed;
    }
    return std::nullopt;
}
}

std::variant<boost::uuids::uuid, AvatarErrorCode> UserService::uploadAvatar(const boost::uuids::uuid& my_uuid, std::string_view jpeg_bytes) {
    backend::logging::Scope operationLog{"UserService", "uploadAvatar"};
    if (const auto error = validateAvatarJpeg(jpeg_bytes)) return *error;

    // 校验结束后解码器和像素缓冲已释放，同步保存只需要原始 JPEG。
    try {
        const auto avatar_id = repository_.saveAvatar(my_uuid, jpeg_bytes);
        if (!avatar_id) return AvatarErrorCode::UserUnavailable;
        return *avatar_id;
    } catch (const DatabaseError& error) {
        backend::logging::Logger::write(backend::logging::Level::Error, "UserService", "avatar_database_failed", {{"code", error.code()}});
        if (error.kind() == DatabaseErrorKind::Connection || error.code().starts_with("08")) {
            return AvatarErrorCode::DatabaseUnavailable;
        }
        return AvatarErrorCode::DataBaseError;
    } catch (const std::system_error& error) {
        backend::logging::Logger::write(backend::logging::Level::Error, "UserService", "avatar_storage_failed",
            {{"code", error.code().value()}, {"reason", error.what()}});
        return AvatarErrorCode::StorageFailed;
    }
}

std::optional<std::string> UserService::getAvatarByID(const boost::uuids::uuid& avatar_id, const boost::uuids::uuid& my_uuid) {
    return repository_.getAvatarById(avatar_id, my_uuid);
}
