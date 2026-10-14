//
// Created by yangb on 2026/10/14.
//

#ifndef CPP_BACKEND_SERVER_SESSIONIDENTITYSERVICE_H
#define CPP_BACKEND_SERVER_SESSIONIDENTITYSERVICE_H
#include "repository/UserRepository.h"
enum IdentityState {
    Success,NoSuchDevice,NotActivated,NotYourDevice
};

class SessionIdentityService {
    public:
    SessionIdentityService(UserRepository &userRepository);
    IdentityState verifySessionIdentity(const boost::uuids::uuid& user_id, const boost::uuids::uuid& device_id);
private:
    UserRepository &user_repository_;

};


#endif //CPP_BACKEND_SERVER_SESSIONIDENTITYSERVICE_H
