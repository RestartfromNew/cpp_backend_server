//
// Created by yangb on 2026/10/7.
//

#include "repository/KeyRepository.h"
namespace {
    //这个命名空间是cpp内部的，只在这里使用
    boost::uuids::uuid parseId(std::string_view text)
    {
        // 将 UUID 文本解析为 boost::uuids::uuid。
        // 格式无效时，string_generator 会抛出异常。
        return boost::uuids::string_generator{}(
            text.begin(), text.end()
        );
    }
    DeviceStatus returnStatus(std::string status) {
        if (status=="active")
            return DeviceStatus::active;
        if (status=="revoked")
            return DeviceStatus::revoked;
        if (status=="pending")
            return DeviceStatus::pending;
    }
    std::vector<std::uint8_t> parseBytea(std::string_view text)
    {
        // PQunescapeBytea 要求以 '\0' 结尾的输入
        const std::string input{text};
        std::size_t length = 0;
        auto* raw = PQunescapeBytea(
            reinterpret_cast<const unsigned char*>(input.c_str()),
            &length
        );
        if (raw == nullptr) {
            throw std::runtime_error{"Failed to decode BYTEA"};
        }
        // 自动释放 libpq 分配的内存
        const std::unique_ptr<unsigned char, decltype(&PQfreemem)>buffer{raw, &PQfreemem};
        return std::vector<std::uint8_t>(raw, raw + length);
    }

} // namespace

 KeyRepository::KeyRepository(DatabasePool &databasePool):databasePool_(databasePool){}
std::optional<Device> KeyRepository::FindDeviceByDeviceId(const boost::uuids::uuid &user_id,const boost::uuids::uuid& device_id) {
  const std::string Parameters[] = {boost::uuids::to_string(user_id),boost::uuids::to_string(device_id)};
     ConnectionLease lease{databasePool_};
     auto result = lease.connection().execute(
    R"(
        SELECT
            ud.device_id,
            ud.user_id,
            ud.device_name,
            ud.status,
            ud.identity_public_key,
            ud.protocol_suite,
            ud.created_at,
            ud.authorized_at,
            ud.revoked_at
        FROM app.user_devices AS ud
        JOIN app.users AS u
            ON u.id = ud.user_id
        WHERE ud.user_id = $1::uuid
          AND ud.device_id = $2::uuid
          AND ud.status = 'active'
    )",Parameters);
     if (result.rowCount() == 0) {return std::nullopt;}
     return Device{
         .device_id = parseId(result.value(0,0)),
         .user_id = parseId(result.value(0,1)),
         .device_name = result.isNull(0, 2)? std::string{}: std::string{result.value(0, 2)},
         .status = returnStatus(std::string{result.value(0,3)}),
         .identity_public_key =parseBytea(result.value(0, 4)),
         .protocol_suite = std::string{result.value(0, 5)},
     };
 }

std::optional<std::vector<Device>> KeyRepository::GetDevicesByUserId(const boost::uuids::uuid &user_id) {
    const std::string Parameters[] = {boost::uuids::to_string(user_id)};
    ConnectionLease lease{databasePool_};
    auto result = lease.connection().execute(
    R"(
        SELECT
            ud.device_id,
            ud.user_id,
            ud.device_name,
            ud.status,
            ud.identity_public_key,
            ud.protocol_suite,
        FROM app.user_devices AS ud
        JOIN app.users AS u
            ON u.id = ud.user_id
        WHERE ud.user_id = $1::uuid
          AND ud.status = 'active'
          AND u.is_active = TRUE
    )",Parameters);
    if (result.rowCount() == 0) {return std::nullopt;}
    std::vector<Device> devices;
    devices.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        devices.push_back(Device{
         .device_id = parseId(result.value(i,0)),
         .user_id = parseId(result.value(i,1)),
         .device_name = result.isNull(i, 2)? std::string{}: std::string{result.value(i, 2)},
         .status = returnStatus(std::string{result.value(i,3)}),
         .identity_public_key =parseBytea(result.value(i, 4)),
         .protocol_suite = std::string{result.value(i, 5)},
     });
    }
    return devices;
}