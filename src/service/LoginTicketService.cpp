#include "service/LoginTicketService.h"
#include <boost/uuid/uuid_io.hpp>
#include <algorithm>
#include <stdexcept>
#include <sodium.h>
bool LoginTicketService::validTicket(const std::string& ticket) {
    return ticket.size() == 64 && std::all_of(ticket.begin(), ticket.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
std::string LoginTicketService::hashTicket(const std::string& ticket) {
    return refresh_.RefreshTokenHash("login-ticket-v1:" + ticket);
}
std::optional<std::string> LoginTicketService::issue(const boost::uuids::uuid& user_id) {
    unsigned char bytes[32];
    randombytes_buf(bytes, sizeof bytes);
    char encoded[65];
    sodium_bin2hex(encoded, sizeof encoded, bytes, sizeof bytes);
    std::string ticket{encoded};
    if (!repository_.issue(user_id, hashTicket(ticket))) return std::nullopt;
    return ticket;
}
CompletedLogin LoginTicketService::verifyDevice(const std::string& ticket, const boost::uuids::uuid& device) {
    if (device.is_nil()) return {{LoginTicketStatus::DeviceNotFound}, {}, {}};
    return complete(ticket, &device, nullptr);
}
CompletedLogin LoginTicketService::registerDevice(const std::string& ticket, const DeviceRegistrationInput& input) {
    return complete(ticket, nullptr, &input);
}
CompletedLogin LoginTicketService::complete(const std::string& ticket, const boost::uuids::uuid* device,
    const DeviceRegistrationInput* input) {
    if (!validTicket(ticket)) return {{LoginTicketStatus::InvalidTicket}, {}, {}};
    const auto refreshToken = refresh_.generateRefreshToken();
    const auto result = repository_.complete(hashTicket(ticket), refresh_.RefreshTokenHash(refreshToken), device, input);
    if (result.status != LoginTicketStatus::Success) return {result, {}, {}};
    if (!result.identity) throw std::logic_error("Missing login identity");
    const auto accessToken = access_.generateAccessToken(boost::uuids::to_string(result.identity->user_id));
    return {result, accessToken, refreshToken};
}
