//
// Created by yangb on 2026/10/7.
//

#include "repository/KeyRepository.h"

#include <boost/uuid/random_generator.hpp>

namespace {
    int parseInt(std::string_view text) {
        int value = 0;

        const auto [end, error] = std::from_chars(
            text.data(),
            text.data() + text.size(),
            value
        );

        if (error != std::errc{} ||
            end != text.data() + text.size()) {
            throw std::invalid_argument("Invalid integer or out of range");
            }

        return value;
    }
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
            ud.protocol_suite
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
Device KeyRepository::RegisterNewDevice(const boost::uuids::uuid &user_id,std::string identity_public_key_hex,std::string device_name,std::string protocol_suite,
    std::string signed_prekey_id,std::string signed_prekey_public_hex,std::string signed_prekey_signature_hex) {
    ConnectionLease lease(databasePool_);
    return lease.connection().withTransaction([&]()->Device {
        const std::string parameters[] = {boost::uuids::to_string(user_id), device_name,identity_public_key_hex,protocol_suite};
        auto result = lease.connection().execute(
        R"(
            INSERT INTO app.user_devices (
                user_id,
                device_name,
                status,
                identity_public_key,
                protocol_suite,
                authorized_at
            )
            VALUES (
                $1::uuid,
                $2::text,
                'active',
                decode($3::text, 'hex'),
                $4::text,
                CURRENT_TIMESTAMP
            )
            RETURNING
                device_id,
                user_id,
                device_name,
                status,
                identity_public_key,
                protocol_suite
        )",
        parameters
    );
        const std::string device_id{result.value(0, 0)};
        const std::string keyParameters[] = {device_id, signed_prekey_id,signed_prekey_public_hex, signed_prekey_signature_hex };
        const auto key_result=lease.connection().execute(
            R"(
                INSERT INTO app.device_key_bundles (
                    device_id,
                    key_version,
                    signed_prekey_id,
                    signed_prekey_public,
                    signed_prekey_signature,
                    is_current
                )
                VALUES (
                    $1::uuid,
                    1,
                    $2::bigint,
                    decode($3::text, 'hex'),
                    decode($4::text, 'hex'),
                    TRUE
                )
            )",
            keyParameters
        );
        Device device{
            .device_id = parseId(result.value(0,0)),
         .user_id = parseId(result.value(0,1)),
         .device_name = result.isNull(0, 2)? std::string{}: std::string{result.value(0, 2)},
         .status = returnStatus(std::string{result.value(0,3)}),
         .identity_public_key =parseBytea(result.value(0, 4)),
         .protocol_suite = std::string{result.value(0, 5)},
        };
        return device;
    });
}

void KeyRepository::createNewOnetimeKey(const boost::uuids::uuid &device_id,std::unordered_map<int,std::string> prekey_record) {
    ConnectionLease lease(databasePool_);
    return lease.connection().withTransaction([&]()->void {
        for (auto i: prekey_record) {
            int index=i.first;
            std::string prekey=i.second;
            const std::string parameters[] = {boost::uuids::to_string(device_id), std::to_string(index),prekey};
            const auto key_result=lease.connection().execute(
            R"(
                INSERT INTO app.device_one_time_prekeys (
                    device_id,
                    prekey_id,
                    public_key,
                    state,
                    created_at
                )
                VALUES (
                    $1::uuid,
                    $2,
                    decode($3::text, 'hex'),
                    'available',
                    CURRENT_TIMESTAMP
                )
            )",
            parameters
        );
        }
    });
}

PrekeyClaimResult KeyRepository::getOneTimeKey(
    const boost::uuids::uuid& my_id, const boost::uuids::uuid& friend_id,
    const boost::uuids::uuid& device_id) {
    ConnectionLease lease(databasePool_);
    const std::string parameters[] = {
        boost::uuids::to_string(my_id), boost::uuids::to_string(friend_id),
        boost::uuids::to_string(device_id)
    };
    const std::string friendshipParameters[] = {parameters[0], parameters[1]};
    const std::string keyParameters[] = {parameters[2]};
    return lease.connection().withTransaction([&]() -> PrekeyClaimResult {
        // Hold authorization rows until the claim commits, so deletion/revocation
        // cannot race between authorization and consuming the prekey.
        auto friendship = lease.connection().execute(R"(
            SELECT fr.id
            FROM app.friend_relation AS fr
            WHERE fr.user_id = $1::uuid AND fr.friend_id = $2::uuid
            FOR SHARE OF fr
        )", friendshipParameters);
        if (friendship.rowCount() == 0) {
            return {PrekeyClaimStatus::NotFriends};
        }
        auto device = lease.connection().execute(R"(
            SELECT ud.device_id
            FROM app.user_devices AS ud
            JOIN app.users AS friend_user ON friend_user.id = ud.user_id
            JOIN app.users AS current_user_record ON current_user_record.id = $1::uuid
            WHERE ud.user_id = $2::uuid AND ud.device_id = $3::uuid
              AND ud.status = 'active'
              AND friend_user.is_active = TRUE
              AND current_user_record.is_active = TRUE
            FOR SHARE OF ud, friend_user, current_user_record
        )", parameters);
        if (device.rowCount() == 0) {
            return {PrekeyClaimStatus::DeviceUnavailable};
        }
        auto result = lease.connection().execute(R"(
            WITH selected_prekey AS (
                SELECT device_id, prekey_id
                FROM app.device_one_time_prekeys
                WHERE device_id = $1::uuid AND state = 'available'
                ORDER BY prekey_id
                LIMIT 1
                FOR UPDATE SKIP LOCKED
            )
            UPDATE app.device_one_time_prekeys AS pk
            SET state = 'claimed', claimed_at = CURRENT_TIMESTAMP
            FROM selected_prekey AS selected
            WHERE pk.device_id = selected.device_id
              AND pk.prekey_id = selected.prekey_id
              AND pk.state = 'available'
            RETURNING pk.prekey_id, encode(pk.public_key, 'hex')
        )", keyParameters);
        if (result.rowCount() == 0) {
            return {PrekeyClaimStatus::NoPrekey};
        }
        return {PrekeyClaimStatus::Success, OneTimePrekey{
            .device_id = device_id,
            .prekey_id = std::stoll(std::string{result.value(0, 0)}),
            .public_key_hex = std::string{result.value(0, 1)}
        }};
    });
}

std::vector<Device> KeyRepository::GetFriendDevicesByUserId(
    const boost::uuids::uuid& my_id, const boost::uuids::uuid& friend_id) {
    const std::string parameters[] = {
        boost::uuids::to_string(my_id), boost::uuids::to_string(friend_id)
    };
    ConnectionLease lease(databasePool_);
    // Recheck friendship in the same query that reads devices.
    auto result = lease.connection().execute(R"(
        SELECT ud.device_id, ud.user_id, ud.device_name, ud.status,
               ud.identity_public_key, ud.protocol_suite
        FROM app.user_devices AS ud
        JOIN app.users AS friend_user ON friend_user.id = ud.user_id
        JOIN app.users AS current_user_record ON current_user_record.id = $1::uuid
        WHERE ud.user_id = $2::uuid
          AND ud.status = 'active'
          AND friend_user.is_active = TRUE
          AND current_user_record.is_active = TRUE
          AND EXISTS (
              SELECT 1 FROM app.friend_relation AS fr
              WHERE fr.user_id = $1::uuid AND fr.friend_id = $2::uuid
          )
        ORDER BY ud.created_at, ud.device_id
    )", parameters);
    std::vector<Device> devices;
    devices.reserve(result.rowCount());
    for (std::size_t i = 0; i < result.rowCount(); ++i) {
        devices.push_back(Device{
            .device_id = parseId(result.value(i, 0)),
            .user_id = parseId(result.value(i, 1)),
            .device_name = result.isNull(i, 2) ? std::string{} : std::string{result.value(i, 2)},
            .status = returnStatus(std::string{result.value(i, 3)}),
            .identity_public_key = parseBytea(result.value(i, 4)),
            .protocol_suite = std::string{result.value(i, 5)}
        });
    }
    return devices;
}

std::optional<DeviceKeyBundle> KeyRepository::getDeviceKeyBundle(const boost::uuids::uuid &friend_id,const boost::uuids::uuid &my_id,const boost::uuids::uuid &device_id) {
    const std::string parameters[] = {boost::uuids::to_string(my_id), boost::uuids::to_string(friend_id), boost::uuids::to_string(device_id)};
    ConnectionLease lease(databasePool_);
    auto result = lease.connection().execute(
    R"(
        SELECT
            kb.device_id,
            kb.key_version,
            ud.identity_public_key,
            ud.protocol_suite,
            kb.signed_prekey_id,
            kb.signed_prekey_public,
            kb.signed_prekey_signature,
            kb.published_at,
            kb.expires_at
        FROM app.device_key_bundles AS kb
        JOIN app.user_devices AS ud
            ON ud.device_id = kb.device_id
        JOIN app.users AS friend_user
            ON friend_user.id = ud.user_id
        JOIN app.users AS my_user
            ON my_user.id = $1::uuid
        WHERE ud.user_id = $2::uuid
          AND ud.device_id = $3::uuid
          AND ud.status = 'active'
          AND friend_user.is_active = TRUE
          AND my_user.is_active = TRUE
          AND kb.is_current = TRUE
          AND (
              kb.expires_at IS NULL
              OR kb.expires_at > CURRENT_TIMESTAMP
          )
          AND EXISTS (
              SELECT 1
              FROM app.friend_relation AS fr
              WHERE fr.user_id = $1::uuid
                AND fr.friend_id = $2::uuid
          )
    )",
    parameters
);

    if (result.rowCount() == 0) {
        return std::nullopt;
    }
    DeviceKeyBundle deviceKeyBundle{
        .device_id = parseId(result.value(0, 0)),
        .signed_preKey_id = parseInt(result.value(0,4)),
        .preKey_public = parseBytea(result.value(0,5)),
        .preKey_signature = parseBytea(result.value(0,6)),
        .is_current = true
    };
    return deviceKeyBundle;
}
