#pragma once
#include <boost/uuid/uuid.hpp>
#include <optional>
#include <string>
/** @brief 新设备登记的公有材料；由持有 ticket 的用户提交，不能包含或生成客户端私钥。 */
struct DeviceRegistrationInput {
    std::string identity_public_key_hex, device_name, protocol_suite;
    std::string signed_prekey_id, signed_prekey_public_hex, signed_prekey_signature_hex;
};
// 完成登录的业务拒绝原因；仅 Success 携带身份，数据库故障通过异常表示。
enum class LoginTicketStatus { Success, InvalidTicket, AccountDisabled, DeviceNotFound, DeviceNotOwned, DeviceNotActive };
/** @brief 完成 ticket 事务后确认的用户和设备；用户身份来自已锁定 ticket 记录，不信任 body.user_id。 */
struct LoginTicketIdentity { boost::uuids::uuid user_id, device_id; };
/** @brief ticket 完成业务状态及可选身份；Success 才允许使用 identity。数据库异常继续向边界传播。 */
struct LoginTicketResult {
    LoginTicketStatus status;
    std::optional<LoginTicketIdentity> identity = std::nullopt;
};
/** @brief 完成登录后的身份、Access/Refresh Token；凭据原文只给客户端，不得打印或写诊断字段。 */
struct CompletedLogin {
    LoginTicketResult result;
    std::string access_token, refresh_token;
};
