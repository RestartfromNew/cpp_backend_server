#pragma once
#include "repository/LoginTicketRepository.h"
#include "Auth/AccessTokenService.h"
#include "Auth/RefreshTokenService.h"
class LoginTicketService {
public:
    LoginTicketService(LoginTicketRepository& repository, AccessTokenService& access,
        RefreshTokenService& refresh) : repository_(repository), access_(access), refresh_(refresh) {}
    std::optional<std::string> issue(const boost::uuids::uuid& user_id);
    CompletedLogin verifyDevice(const std::string& ticket, const boost::uuids::uuid& device);
    CompletedLogin registerDevice(const std::string& ticket, const DeviceRegistrationInput& input);
private:
    CompletedLogin complete(const std::string& ticket, const boost::uuids::uuid* device,
        const DeviceRegistrationInput* input);
    static bool validTicket(const std::string& ticket);
    std::string hashTicket(const std::string& ticket);
    LoginTicketRepository& repository_;
    AccessTokenService& access_;
    RefreshTokenService& refresh_;
};
