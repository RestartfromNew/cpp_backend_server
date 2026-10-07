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

class KeyRepository {
public:
    explicit KeyRepository(DatabasePool &databasePool);
    std::optional<Device> FindDeviceByDeviceId(const boost::uuids::uuid &user_id,const boost::uuids::uuid& device_id);
    // bool InsertNewDevice(const boost::uuids::uuid &user_id,std::string device_name,std::vector<std::uint8_t> identity_public_key,std::string protocol_suite);
    std::optional<std::vector<Device>> GetDevicesByUserId(const boost::uuids::uuid &user_id);
private:
    DatabasePool &databasePool_;
};


#endif //CPP_BACKEND_SERVER_KEYREPOSITORY_H
