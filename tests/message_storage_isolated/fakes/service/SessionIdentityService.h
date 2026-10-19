#pragma once
#include "boost/uuid/uuid.hpp"

enum IdentityState { Success, NoSuchDevice, NotActivated, NotYourDevice };
class SessionIdentityService {
public:
    IdentityState state = IdentityState::Success;
    IdentityState verifySessionIdentity(const boost::uuids::uuid&, const boost::uuids::uuid&) {
        return state;
    }
};
