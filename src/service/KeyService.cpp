//
// Created by yangb on 2026/10/7.
//

#include "service/KeyService.h"
KeyService::KeyService(KeyRepository &keyRepository) : keyRepository_(keyRepository) {

}
bool KeyService::verifyDeviceId(const boost::uuids::uuid &user_id,const boost::uuids::uuid &device_id) {
    auto result=keyRepository_.FindDeviceByDeviceId(user_id,device_id);
    if (result==std::nullopt) {
        return false;
    }
    return true;
}
std::optional<std::vector<Device>> KeyService::fetchFriendDevices(const boost::uuids::uuid &user_id) {
    auto result=keyRepository_.GetDevicesByUserId(user_id);
    return result;
}