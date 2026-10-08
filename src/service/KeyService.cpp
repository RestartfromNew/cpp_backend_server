//
// Created by yangb on 2026/10/7.
//

#include "service/KeyService.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>
KeyService::KeyService(KeyRepository &keyRepository, UserRepository &userRepository) : keyRepository_(keyRepository),userRepository_(userRepository) {

}
std::optional<bool> KeyService::verifyDeviceId(const boost::uuids::uuid &user_id,const boost::uuids::uuid &device_id) {
    auto result=keyRepository_.FindDeviceByDeviceId(user_id,device_id);
    if (result==std::nullopt) {
        return std::nullopt;
    }
    if (result->status==DeviceStatus::active) {
        return true;
    }
    return false;
}
std::optional<std::vector<Device>> KeyService::fetchFriendDevices(
    const boost::uuids::uuid& my_id, const boost::uuids::uuid& friend_id) {
    if (!userRepository_.VerifyFriendShip(my_id, friend_id)) {
        return std::nullopt;
    }
    return keyRepository_.GetFriendDevicesByUserId(my_id, friend_id);
}
std::string KeyService::registerNewDevice(
    const boost::uuids::uuid& user_id,
    const std::string& identity_public_key_hex,
    const std::string& device_name,
    const std::string& protocol_suite,
    const std::string& signed_prekey_id,
    const std::string& signed_prekey_public_hex,
    const std::string& signed_prekey_signature_hex
) {
    try {
        const auto device = keyRepository_.RegisterNewDevice(
       user_id,
       identity_public_key_hex,
       device_name,
       protocol_suite,
       signed_prekey_id,
       signed_prekey_public_hex,
       signed_prekey_signature_hex
   );
        return boost::uuids::to_string(device.device_id);
    }
    catch (const DatabaseError& error) {
        std::cerr
            << "[registerNewDevice] database failure; SQLSTATE="
            << error.code()
            << '\n';
        throw; // 原样向上抛出，由 Handler 或 HttpSession 处理
    }




}
bool KeyService::uploadOneTimePrekeys(
    const boost::uuids::uuid& user_id,
    const boost::uuids::uuid& device_id,
    const std::unordered_map<int, std::string>& prekeys) {
    if (prekeys.empty() || prekeys.size() > 10) {
        throw std::invalid_argument("Expected 1 to 10 one-time prekeys");
    }
    for (const auto& [id, key] : prekeys) {
        if (id < 0 || key.size() != 64 ||
            !std::all_of(key.begin(), key.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                       (c >= 'A' && c <= 'F');
            })) {
            throw std::invalid_argument("Invalid one-time prekey");
        }
    }
    const auto device = keyRepository_.FindDeviceByDeviceId(user_id, device_id);
    if (!device || device->status != DeviceStatus::active) {
        return false;
    }
    keyRepository_.createNewOnetimeKey(device_id, prekeys);
    return true;
}


PrekeyClaimResult KeyService::getOneTimePrekey(
    const boost::uuids::uuid& my_id, const boost::uuids::uuid& friend_id,
    const boost::uuids::uuid& device_id) {
    if (!userRepository_.VerifyFriendShip(my_id, friend_id)) {
        return {PrekeyClaimStatus::NotFriends};
    }
    const auto device = keyRepository_.FindDeviceByDeviceId(friend_id, device_id);
    if (!device || device->status != DeviceStatus::active) {
        return {PrekeyClaimStatus::DeviceUnavailable};
    }
    // The Repository repeats authorization under row locks before consuming a key.
    return keyRepository_.getOneTimeKey(my_id, friend_id, device_id);
}
