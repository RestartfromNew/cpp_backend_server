//
// Created by yangb on 2026/10/7.
//

#ifndef CPP_BACKEND_SERVER_KEYSERVICE_H
#define CPP_BACKEND_SERVER_KEYSERVICE_H
#include "repository/KeyRepository.h"
#include <domain/Device.h>
#include <unordered_map>
#include <string>
#include <boost/uuid/uuid_io.hpp>

#include "repository/UserRepository.h"

class KeyService {
    public:
    KeyService(KeyRepository &keyRepository, UserRepository &userRepository) ;
    std::optional<bool> verifyDeviceId(const boost::uuids::uuid &user_id,const boost::uuids::uuid &device_id);
    // nullopt: not friends; an empty vector: no effective devices.
    std::optional<std::vector<Device>> fetchFriendDevices(
        const boost::uuids::uuid& my_id, const boost::uuids::uuid& friend_id);
    std::string registerNewDevice(
    const boost::uuids::uuid& user_id,
    const std::string& identity_public_key_hex,
    const std::string& device_name,
    const std::string& protocol_suite,
    const std::string& signed_prekey_id,
    const std::string& signed_prekey_public_hex,
    const std::string& signed_prekey_signature_hex) ;
    // False means the authenticated user has no active device with this ID.
    bool uploadOneTimePrekeys(const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& device_id,
        const std::unordered_map<int, std::string>& prekeys);
    PrekeyClaimResult getOneTimePrekey(const boost::uuids::uuid &my_id,const boost::uuids::uuid& friend_id,const boost::uuids::uuid& device_id);
    std::optional<DeviceKeyBundle> getDeviceKeyBundle(const boost::uuids::uuid& friend_id,const boost::uuids::uuid &my_id,const boost::uuids::uuid &device_id);
private:
    KeyRepository &keyRepository_;
    UserRepository &userRepository_;
};


#endif //CPP_BACKEND_SERVER_KEYSERVICE_H
