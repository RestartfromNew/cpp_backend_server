#pragma once
#include "domain/LoginTicket.h"
#include "databases/DatabasePool.h"
#include "repository/KeyRepository.h"
class LoginTicketRepository {
public:
    LoginTicketRepository(DatabasePool& pool, KeyRepository& keys) : pool_(pool), keys_(keys) {}
    bool issue(const boost::uuids::uuid& user_id, const std::string& hash);
    LoginTicketResult complete(const std::string& hash, const std::string& refresh_hash,
        const boost::uuids::uuid* device_id, const DeviceRegistrationInput* registration);
private:
    DatabasePool& pool_;
    KeyRepository& keys_;
};
