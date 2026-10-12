#include "repository/EnvelopeRepository.h"
#include "databases/ConnectionLease.h"
#include "databases/DatabaseError.h"
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/string_generator.hpp>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <system_error>
namespace {
std::int64_t parseSeq(std::string_view text) {
    std::int64_t seq = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seq);
    if (error != std::errc{} || end != text.data() + text.size() || seq <= 0) {
        throw DatabaseError(DatabaseErrorKind::DataConversion, "", "Invalid stored sequence");
    }
    return seq;
}
std::string encodeHex(const std::vector<std::uint8_t>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 15]);
    }
    return hex;
}
}
EnvelopeRepository::EnvelopeRepository(DatabasePool& databasePool) : databasePool_(databasePool) {}
NewMessageResult EnvelopeRepository::saveNewMessage(
    const boost::uuids::uuid& user_id, const boost::uuids::uuid& sender_device_id,
    const NewTextMessageRequest& request) {
    const std::string userId = boost::uuids::to_string(user_id);
    const std::string senderId = boost::uuids::to_string(sender_device_id);
    const std::string recipientId = boost::uuids::to_string(request.recipient_device_id);
    const std::string messageId = boost::uuids::to_string(request.message_id);
    const std::string packetHex = encodeHex(request.packet);
    ConnectionLease lease(databasePool_);
    return lease.connection().withTransaction([&]() -> NewMessageResult {
        auto isolation = lease.connection().execute("SET TRANSACTION ISOLATION LEVEL READ COMMITTED");
        // Same canonical device pair, including reversed direction, uses one lock.
        // Hash collisions merely serialize unrelated pairs; constraints remain authoritative.
        const std::string pairParameters[] = {senderId, recipientId};
        auto pairLock = lease.connection().execute(R"(
            SELECT pg_advisory_xact_lock(hashtextextended(
                'envelope-pair-v1:' || LEAST($1::uuid, $2::uuid)::text || ':' ||
                GREATEST($1::uuid, $2::uuid)::text, 0))
        )", pairParameters);
        const std::string deviceParameters[] = {userId, senderId, recipientId};
        // Row locks hold account/device authorization valid until commit.
        auto devices = lease.connection().execute(R"(
            SELECT ud.device_id, ud.user_id
            FROM app.user_devices AS ud
            JOIN app.users AS u ON u.id = ud.user_id
            WHERE ud.device_id IN ($2::uuid, $3::uuid)
              AND ud.status = 'active' AND u.is_active = TRUE
              AND (ud.device_id <> $2::uuid OR ud.user_id = $1::uuid)
            ORDER BY ud.device_id
            FOR SHARE OF ud, u
        )", deviceParameters);
        if (devices.rowCount() != 2) {
            return {NewMessageStatus::PermissionDenied};
        }
        std::string recipientUserId;
        for (std::size_t i = 0; i < devices.rowCount(); ++i) {
            if (devices.value(i, 0) == recipientId) {
                recipientUserId = std::string{devices.value(i, 1)};
            }
        }
        if (recipientUserId.empty() || recipientUserId == userId) {
            // This endpoint is friend messaging, not same-account device synchronization.
            return {NewMessageStatus::PermissionDenied};
        }
        const std::string friendshipParameters[] = {userId, recipientUserId};
        auto friendships = lease.connection().execute(R"(
            SELECT fr.user_id
            FROM app.friend_relation AS fr
            WHERE (fr.user_id = $1::uuid AND fr.friend_id = $2::uuid)
               OR (fr.user_id = $2::uuid AND fr.friend_id = $1::uuid)
            ORDER BY fr.user_id, fr.friend_id
            FOR SHARE OF fr
        )", friendshipParameters);
        if (friendships.rowCount() != 2) {
            return {NewMessageStatus::PermissionDenied};
        }
        const std::string lookupParameters[] = {senderId, recipientId, messageId};
        auto existing = lease.connection().execute(R"(
            SELECT envelope_id, seq, protocol_version, encode(packet, 'hex')
            FROM app.message_envelopes
            WHERE sender_device_id = $1::uuid
              AND recipient_device_id = $2::uuid AND message_id = $3::uuid
        )", lookupParameters);
        if (existing.rowCount() != 0) {
            if (existing.value(0, 2) != request.protocol_version ||
                existing.value(0, 3) != packetHex) {
                return {NewMessageStatus::Conflict};
            }
            return {NewMessageStatus::Duplicate_request, StoredEnvelope{
                .envelope_id = boost::uuids::string_generator{}(std::string{existing.value(0, 0)}),
                .message_id = request.message_id,
                .seq = parseSeq(existing.value(0, 1))
            }};
        }
        const std::string insertParameters[] = {
            senderId, recipientId, messageId, request.protocol_version, packetHex
        };
        auto inserted = lease.connection().execute(R"(
            INSERT INTO app.message_envelopes (
                message_id, sender_device_id, recipient_device_id,
                protocol_version, packet, seq
            )
            SELECT $3::uuid, $1::uuid, $2::uuid, $4::text,
                   decode($5::text, 'hex'), COALESCE(MAX(seq), 0) + 1
            FROM app.message_envelopes
            WHERE LEAST(sender_device_id, recipient_device_id) = LEAST($1::uuid, $2::uuid)
              AND GREATEST(sender_device_id, recipient_device_id) = GREATEST($1::uuid, $2::uuid)
            RETURNING envelope_id, seq
        )", insertParameters);
        if (inserted.rowCount() != 1) {
            throw std::runtime_error("Insert returned unexpected row count");
        }
        return {NewMessageStatus::Success, StoredEnvelope{
            .envelope_id = boost::uuids::string_generator{}(std::string{inserted.value(0, 0)}),
            .message_id = request.message_id,
            .seq = parseSeq(inserted.value(0, 1))
        }};
    }); // withTransaction commits before returning the result to Service.
}
