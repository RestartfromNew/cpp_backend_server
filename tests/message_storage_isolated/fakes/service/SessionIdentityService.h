#pragma once
#include "boost/uuid/uuid.hpp"
#include "databases/DatabaseError.h"
#include <thread>
#include <stdexcept>

enum IdentityState { Success, NoSuchDevice, NotActivated, NotYourDevice };
class SessionIdentityService {
public:
    IdentityState state = IdentityState::Success;
    int calls = 0;
    std::thread::id last_thread;
    std::string fault_code;
    bool unexpected = false;
    IdentityState verifySessionIdentity(const boost::uuids::uuid&, const boost::uuids::uuid&) {
        ++calls;
        last_thread = std::this_thread::get_id();
        if (unexpected) throw std::runtime_error("private details");
        if (!fault_code.empty()) throw DatabaseError(
            fault_code.starts_with("08") ? DatabaseErrorKind::Connection : DatabaseErrorKind::Query,
            fault_code, "private SQL details");
        return state;
    }
};
