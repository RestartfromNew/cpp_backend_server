//
// Created by yangb on 2026/10/7.
//

#ifndef CPP_BACKEND_SERVER_KEYSERVICE_H
#define CPP_BACKEND_SERVER_KEYSERVICE_H
#include "repository/KeyRepository.h"
#include <domain/Device.h>

class KeyService {
    public:
    KeyService(KeyRepository &keyRepository);
    bool verifyDeviceId(const boost::uuids::uuid &user_id,const boost::uuids::uuid &device_id);
    std::optional<std::vector<Device>> fetchFriendDevices(const boost::uuids::uuid &user_id);
private:
    KeyRepository &keyRepository_;
};


#endif //CPP_BACKEND_SERVER_KEYSERVICE_H
