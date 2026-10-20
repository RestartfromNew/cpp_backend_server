//
// Created by yangb on 2026/9/2.
//

#ifndef CPP_BACKEND_SERVER_USERREPOSITORY_H
#define CPP_BACKEND_SERVER_USERREPOSITORY_H
#include <boost/uuid/uuid.hpp>

#include <optional>
#include <string>
#include <vector>
#include <exception>

#include "RefreshTokenRepository.h"
#include "databases/DatabaseConnection.h"
#include "boost/uuid/uuid_io.hpp"
#include "databases/DatabaseError.h"
#include "domain/User.h"
#include "databases/ConnectionLease.h"
#include "databases/DatabasePool.h"
#include "domain/FriendshipRequestRecord.h"
#include "domain/FriendRequestProcess.h"
//负责接受service来的业务数据，包装成数据库需要形式，执行数据库连接和命令，返回service层处理需要的结果
//借用但是不拥有数据库连接，不负责析构
/** @brief 仅供密码验证使用的内部记录，含 password_hash；不能直接序列化成 API 响应或日志。 */
struct PasswordLoginRecord {
    boost::uuids::uuid id;
    std::string display_name;
    bool is_active;
    std::string password_hash;
    std::string username;
};


// 设备验证的可区分拒绝原因；由 DeviceRelationException 传递，避免按异常文本分支。
enum class DeviceRelationError {
    NoSuchDevice,
    NotActivated,
    NotYourDevice
};
enum class RevokeDeviceResult {
    Success,
    DeviceNotFound,
    NotYourDevice,
    AlreadyRevoked,
    DeviceNotActive
};
/** @brief 携带类型化设备关系拒绝原因；Service 通过 error() 映射状态，不能靠匹配 what() 文本判断。 */
class DeviceRelationException : public std::runtime_error {
public:
    explicit DeviceRelationException(DeviceRelationError error)
        : std::runtime_error("Device relation verification failed"),
          error_(error) {}

    DeviceRelationError error() const noexcept {
        return error_;
    }

private:
    DeviceRelationError error_;
};

/** @brief 用户、好友请求及设备关系的持久化入口；参数化 SQL，业务结果交 Service，基础设施失败保留异常。 */
class UserRepository {
public:
    explicit UserRepository(DatabasePool &databasePool);
    std::optional<User> findByEmail(const std::string& email);
    std::optional<User> CreateNewUserByEmail(const std::string &email,const std::string & password_hash, const std::string & display_name,const std::string &username);
    std::optional<PasswordLoginRecord> FindLoginRecordByEmail(const std::string &email);
    std::optional<std::string> FindLoginRecordByUserId(const boost::uuids::uuid &user_id);

    std::optional<User> FindUserByUsername(const std::string &username);
    FriendshipRequestRecord CreateNewFriendship(const boost::uuids::uuid &my_id,const boost::uuids::uuid &friend_id,const std::string &message);
    std::optional<std::vector<FriendshipRequestRecord>> FetchUnprocessedRequest(const boost::uuids::uuid &my_id);
    std::vector<User> FetchFriends(const boost::uuids::uuid& my_id);
    FriendRequestProcessResult processFriendshipRequest(const boost::uuids::uuid& my_id,
        const boost::uuids::uuid& request_id, FriendRequestAction action);
    bool VerifyFriendShip(const boost::uuids::uuid &my_id,const boost::uuids::uuid &friend_id);
    bool VerifyUserDeviceRelation(const boost::uuids::uuid &my_id,const boost::uuids::uuid &device_id);
    RevokeDeviceResult revokeUserDevice(const boost::uuids::uuid& my_id,
        const boost::uuids::uuid& device_id);


private:
    DatabasePool &databasePool_;
};

#endif //CPP_BACKEND_SERVER_USERREPOSITORY_H
