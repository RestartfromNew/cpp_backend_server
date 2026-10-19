#pragma once
#include <boost/uuid/uuid.hpp>
#include <optional>
#include <string>
struct DeviceRegistrationInput {
    std::string identity_public_key_hex, device_name, protocol_suite;
    std::string signed_prekey_id, signed_prekey_public_hex, signed_prekey_signature_hex;
};
enum class LoginTicketStatus { Success, InvalidTicket, AccountDisabled, DeviceNotFound, DeviceNotOwned, DeviceNotActive };
struct LoginTicketIdentity { boost::uuids::uuid user_id, device_id; };
struct LoginTicketResult {
    LoginTicketStatus status;
    std::optional<LoginTicketIdentity> identity = std::nullopt;
};
struct CompletedLogin {
    LoginTicketResult result;
    std::string access_token, refresh_token;
};
