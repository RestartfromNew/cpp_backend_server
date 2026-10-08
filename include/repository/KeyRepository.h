//
// Created by yangb on 2026/10/7.
//

#ifndef CPP_BACKEND_SERVER_KEYREPOSITORY_H
#define CPP_BACKEND_SERVER_KEYREPOSITORY_H
#include "RefreshTokenRepository.h"
#include "databases/DatabaseConnection.h"
#include "boost/uuid/uuid_io.hpp"
#include "databases/DatabaseError.h"
#include "domain/User.h"
#include "databases/ConnectionLease.h"
#include "databases/DatabasePool.h"
#include "domain/Device.h"
#include "domain/DeviceKeyBundle.h"
#include <unordered_map>
#include "domain/OneTimePrekey.h"

class KeyRepository {
public:
    explicit KeyRepository(DatabasePool &databasePool);
    std::optional<Device> FindDeviceByDeviceId(const boost::uuids::uuid &user_id,const boost::uuids::uuid& device_id);
    // bool InsertNewDevice(const boost::uuids::uuid &user_id,std::string device_name,std::vector<std::uint8_t> identity_public_key,std::string protocol_suite);
    std::optional<std::vector<Device>> GetDevicesByUserId(const boost::uuids::uuid &user_id);
    std::vector<Device> GetFriendDevicesByUserId(
        const boost::uuids::uuid& my_id, const boost::uuids::uuid& friend_id);
    Device RegisterNewDevice(const boost::uuids::uuid &user_id,std::string identity_public_key_hex,std::string device_name,std::string protocol_suite,
    std::string signed_prekey_id,std::string signed_prekey_public_hex,std::string signed_prekey_signature_hex);
    void createNewOnetimeKey(const boost::uuids::uuid &device_id,std::unordered_map<int,std::string> prekey_record);
    PrekeyClaimResult getOneTimeKey(const boost::uuids::uuid& my_id,
        const boost::uuids::uuid& friend_id, const boost::uuids::uuid& device_id);
private:
    DatabasePool &databasePool_;
};


#endif //CPP_BACKEND_SERVER_KEYREPOSITORY_H
