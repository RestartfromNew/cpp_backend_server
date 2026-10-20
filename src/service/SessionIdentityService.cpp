#include "common/Logger.h"
//
// Created by yangb on 2026/10/14.
//

#include "service/SessionIdentityService.h"
#include <stdexcept>
SessionIdentityService::SessionIdentityService(UserRepository &userRepository):user_repository_(userRepository){}
IdentityState SessionIdentityService::verifySessionIdentity(const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id) {
    backend::logging::Scope operationLog{"SessionIdentityService", "verifySessionIdentity"};
    try {
        auto result=user_repository_.VerifyUserDeviceRelation(user_id, device_id);
        if (result)
            return IdentityState::Success;
    }catch (const DeviceRelationException &e) {
        if (e.error()==DeviceRelationError::NoSuchDevice)
            return IdentityState::NoSuchDevice;
        if (e.error()==DeviceRelationError::NotActivated)
            return IdentityState::NotActivated;
        if (e.error()==DeviceRelationError::NotYourDevice)
            return IdentityState::NotYourDevice;

    }
    throw std::logic_error("Unexpected device relation result");
}