#include "repository/LoginTicketRepository.h"
#include "repository/RefreshTokenRepository.h"
#include "databases/ConnectionLease.h"
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <stdexcept>
bool LoginTicketRepository::issue(const boost::uuids::uuid& user_id, const std::string& hash) {
    ConnectionLease lease{pool_};
    return lease.connection().withTransaction([&] {
        const std::string accountParameters[] = {boost::uuids::to_string(user_id)};
        auto account = lease.connection().execute(R"(
            SELECT id FROM app.users WHERE id = $1::uuid AND is_active = TRUE FOR SHARE
        )", accountParameters);
        if (account.rowCount() == 0) return false;
        const std::string parameters[] = {accountParameters[0], hash};
        auto inserted = lease.connection().execute(R"(
            INSERT INTO app.login_tickets (user_id, ticket_hash, expires_at)
            VALUES ($1::uuid, $2::text, clock_timestamp() + INTERVAL '5 minutes')
            RETURNING ticket_hash
        )", parameters);
        if (inserted.rowCount() != 1) throw std::runtime_error("Ticket insert failed");
        return true;
    });
}
LoginTicketResult LoginTicketRepository::complete(const std::string& hash,
    const std::string& refresh_hash, const boost::uuids::uuid* device_id,
    const DeviceRegistrationInput* registration) {
    if ((device_id == nullptr) == (registration == nullptr))
        throw std::invalid_argument("Choose exactly one login completion path");
    ConnectionLease lease{pool_};
    return lease.connection().withTransaction([&]() -> LoginTicketResult {
        const std::string ticketParameters[] = {hash};
        auto ticket = lease.connection().execute(R"(
            SELECT user_id FROM app.login_tickets
            WHERE ticket_hash = $1::text AND consumed_at IS NULL
              AND expires_at > clock_timestamp()
            FOR UPDATE
        )", ticketParameters);
        if (ticket.rowCount() == 0) return {LoginTicketStatus::InvalidTicket};
        const auto userId = boost::uuids::string_generator{}(std::string{ticket.value(0, 0)});
        const std::string accountParameters[] = {boost::uuids::to_string(userId)};
        auto account = lease.connection().execute(R"(
            SELECT id FROM app.users WHERE id = $1::uuid AND is_active = TRUE FOR SHARE
        )", accountParameters);
        if (account.rowCount() == 0) return {LoginTicketStatus::AccountDisabled};
        boost::uuids::uuid targetDevice{};
        if (device_id) {
            const std::string parameters[] = {boost::uuids::to_string(*device_id)};
            auto device = lease.connection().execute(R"(
                SELECT user_id, status FROM app.user_devices
                WHERE device_id = $1::uuid FOR SHARE
            )", parameters);
            if (device.rowCount() == 0) return {LoginTicketStatus::DeviceNotFound};
            if (device.value(0, 0) != accountParameters[0]) return {LoginTicketStatus::DeviceNotOwned};
            if (device.value(0, 1) != "active") return {LoginTicketStatus::DeviceNotActive};
            targetDevice = *device_id;
        }
        // Recheck wall-clock expiry after acquiring all locks. The row lock makes use single-shot.
        auto consumed = lease.connection().execute(R"(
            UPDATE app.login_tickets SET consumed_at = clock_timestamp()
            WHERE ticket_hash = $1::text AND consumed_at IS NULL
              AND expires_at > clock_timestamp()
            RETURNING ticket_hash
        )", ticketParameters);
        if (consumed.rowCount() == 0) return {LoginTicketStatus::InvalidTicket};
        if (registration) {
            const auto& input = *registration;
            targetDevice = keys_.RegisterNewDevice(lease.connection(), userId,
                input.identity_public_key_hex, input.device_name, input.protocol_suite,
                input.signed_prekey_id, input.signed_prekey_public_hex,
                input.signed_prekey_signature_hex).device_id;
        }
        RefreshTokenRepository::insertRefreshToken(
            lease.connection(), userId, targetDevice, refresh_hash);
        return {LoginTicketStatus::Success, LoginTicketIdentity{userId, targetDevice}};
    }); // Any insert/commit failure rolls back consumption and device registration too.
}
