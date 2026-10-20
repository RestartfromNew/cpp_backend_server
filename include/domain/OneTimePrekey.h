#ifndef CPP_BACKEND_SERVER_ONETIMEPREKEY_H
#define CPP_BACKEND_SERVER_ONETIMEPREKEY_H

#include <boost/uuid/uuid.hpp>
#include <cstdint>
#include <optional>
#include <string>

/** @brief 一次性预密钥公有资料；领取/消费由 Repository 原子完成，不存储客户端私钥。 */
struct OneTimePrekey {
    boost::uuids::uuid device_id;
    std::int64_t prekey_id;
    std::string public_key_hex;
};

// 区分权限拒绝、设备不可用与没有预密钥；不能把库存为空当作数据库连接失败。
enum class PrekeyClaimStatus { Success, NotFriends, DeviceUnavailable, NoPrekey };
/** @brief 一次性预密钥领取状态与可选数据；区分权限拒绝、设备不可用及库存耗尽。 */
struct PrekeyClaimResult {
    PrekeyClaimStatus status;
    std::optional<OneTimePrekey> prekey = std::nullopt;
};

#endif
