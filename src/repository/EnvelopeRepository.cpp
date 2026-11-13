#include "common/Logger.h"
#include "repository/EnvelopeRepository.h"
#include "databases/ConnectionLease.h"
#include "databases/DatabaseError.h"
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/string_generator.hpp>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <nlohmann/json.hpp>
#include <set>
#include <algorithm>
namespace {
std::int64_t parseSeq(std::string_view text) {
    std::int64_t seq = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seq);
    if (error != std::errc{} || end != text.data() + text.size() || seq <= 0) {
        throw DatabaseError(DatabaseErrorKind::DataConversion, "", "Invalid stored sequence");
    }
    return seq;
}

std::vector<std::uint8_t> decodePacketHex(std::string_view hex) {
    if (hex.empty() || hex.size() % 2 != 0 || hex.size() > 2 * 65536) {
        throw DatabaseError(DatabaseErrorKind::DataConversion, "", "Invalid stored packet length");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        unsigned int byte = 0;
        const auto [end, error] = std::from_chars(hex.data() + i, hex.data() + i + 2, byte, 16);
        if (error != std::errc{} || end != hex.data() + i + 2) {
            throw DatabaseError(DatabaseErrorKind::DataConversion, "", "Invalid stored packet encoding");
        }
        bytes.push_back(static_cast<std::uint8_t>(byte));
    }
    return bytes;
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
    backend::logging::Scope operationLog{"EnvelopeRepository", "saveNewMessage"};
    const std::string userId = boost::uuids::to_string(user_id);
    const std::string senderId = boost::uuids::to_string(sender_device_id);
    const std::string recipientId = boost::uuids::to_string(request.recipient_device_id);
    const std::string messageId = boost::uuids::to_string(request.message_id);
    const std::string packetHex = encodeHex(request.packet);
    auto attachmentIds = request.attachment_ids;
    std::sort(attachmentIds.begin(), attachmentIds.end(), [](const auto& lhs, const auto& rhs) {
        return boost::uuids::to_string(lhs) < boost::uuids::to_string(rhs);
    });
    if (attachmentIds.size() > 16 || std::adjacent_find(attachmentIds.begin(), attachmentIds.end()) != attachmentIds.end() ||
        (!attachmentIds.empty() && attachmentIds.front().is_nil())) {
        throw std::invalid_argument("Invalid attachment IDs");
    }
    ConnectionLease lease(databasePool_);
    return lease.connection().withTransaction([&]() -> NewMessageResult {
        // 同一设备对的序号、幂等判定与 INSERT 必须共享事务锁；提交前不得发送在线通知。
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
        if (recipientUserId.empty()) {
            return {NewMessageStatus::PermissionDenied};
        }
        // Different active devices of the same account need no friendship row.
        // Device ownership and account activity remain checked under row locks above.
        if (recipientUserId != userId) {
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
            const std::string envelopeParameters[] = {std::string{existing.value(0, 0)}};
            const auto links = lease.connection().execute(R"(
                SELECT attachment_id FROM app.envelope_attachments
                WHERE envelope_id = $1::uuid ORDER BY attachment_id
            )", envelopeParameters);
            if (links.rowCount() != attachmentIds.size()) return {NewMessageStatus::Conflict};
            for (std::size_t i = 0; i < attachmentIds.size(); ++i) {
                if (links.value(i, 0) != boost::uuids::to_string(attachmentIds[i])) return {NewMessageStatus::Conflict};
            }
            return {NewMessageStatus::Duplicate_request, Envelope{
                .envelope_id = boost::uuids::string_generator{}(std::string{existing.value(0, 0)}),
                .message_id = request.message_id,
                .sender_device_id = sender_device_id,
                .recipient_device_id = request.recipient_device_id,
                .protocol_version = request.protocol_version,
                .packet = request.packet,
                .seq = parseSeq(existing.value(0, 1)),
                .attachment_ids = attachmentIds
            }};
        }
        // 按 UUID 顺序加锁，避免多附件发送和清理并发时交叉锁定。
        for (const auto& id : attachmentIds) {
            const std::string parameters[] = {boost::uuids::to_string(id), userId, senderId};
            const auto allowed = lease.connection().execute(R"(
                SELECT id FROM app.attachment
                WHERE id = $1::uuid AND uploader_id = $2::uuid AND uploader_device_id = $3::uuid
                  AND status = 'ready'
                  AND (expires_at IS NULL OR expires_at > clock_timestamp())
                  AND (unreferenced_expires_at IS NULL OR unreferenced_expires_at > clock_timestamp())
                FOR UPDATE
            )", parameters);
            if (allowed.rowCount() != 1) return {NewMessageStatus::PermissionDenied};
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
        for (const auto& id : attachmentIds) {
            const std::string parameters[] = {std::string{inserted.value(0, 0)}, boost::uuids::to_string(id)};
            (void)lease.connection().execute(R"(
                INSERT INTO app.envelope_attachments (envelope_id, attachment_id)
                VALUES ($1::uuid, $2::uuid)
            )", parameters);
            (void)lease.connection().execute(R"(
                UPDATE app.attachment SET unreferenced_expires_at = NULL,
                    expires_at = COALESCE(expires_at, clock_timestamp() + INTERVAL '7 days'),
                    updated_at = clock_timestamp()
                WHERE id = $1::uuid AND unreferenced_expires_at IS NOT NULL
            )", std::span{parameters}.last<1>());
        }
        return {NewMessageStatus::Success, Envelope{
            .envelope_id = boost::uuids::string_generator{}(std::string{inserted.value(0, 0)}),
            .message_id = request.message_id,
            .sender_device_id = sender_device_id,
            .recipient_device_id = request.recipient_device_id,
            .protocol_version = request.protocol_version,
            .packet = request.packet,
            .seq = parseSeq(inserted.value(0, 1)),
            .attachment_ids = attachmentIds
        }};
    }); // withTransaction commits before returning the result to Service.
}

std::optional<EnvelopePage> EnvelopeRepository::syncEnvelopeRecord(
    const boost::uuids::uuid& user_id, const SyncEnvelopeRequest& request) {
    backend::logging::Scope operationLog{"EnvelopeRepository", "syncEnvelopeRecord"};
    if (user_id.is_nil() || request.device_id.is_nil() || request.cursors.size() > 1000) {
        throw std::invalid_argument("Invalid synchronization request");
    }
    auto cursors = nlohmann::json::array();
    std::set<std::string> seen;
    for (const auto& cursor : request.cursors) {
        const auto peer = boost::uuids::to_string(cursor.peer_device_id);
        if (cursor.peer_device_id.is_nil() || cursor.peer_device_id == request.device_id ||
            cursor.after_seq < 0 || !seen.insert(peer).second) {
            throw std::invalid_argument("Invalid or duplicate synchronization cursor");
        }
        cursors.push_back({{"peer_device_id", peer}, {"after_seq", cursor.after_seq}});
    }
    const auto deviceId = boost::uuids::to_string(request.device_id);
    ConnectionLease lease{databasePool_};
    return lease.connection().withTransaction([&]() -> std::optional<EnvelopePage> {
        const std::string deviceParameters[] = {boost::uuids::to_string(user_id), deviceId};
        auto device = lease.connection().execute(R"(
            SELECT ud.device_id
            FROM app.user_devices AS ud
            JOIN app.users AS u ON u.id = ud.user_id
            WHERE ud.user_id = $1::uuid AND ud.device_id = $2::uuid
              AND ud.status = 'active' AND u.is_active = TRUE
            FOR SHARE OF ud, u
        )", deviceParameters);
        if (device.rowCount() == 0) {
            return std::nullopt; // Authorization denied, distinct from an empty page.
        }
        // 游标为对端设备水位；无游标设备从 0 开始。分页还受编码后响应大小限制。
        constexpr std::size_t pageSize = 100;
        // Bound the eventual Base64 JSON reply, not just the number of records.
        constexpr std::size_t pageWireBudget = 512 * 1024;
        const std::string parameters[] = {deviceId, cursors.dump(), std::to_string(pageSize + 1)};
        auto result = lease.connection().execute(R"(
            WITH client_cursors AS (
                SELECT peer_device_id, after_seq
                FROM jsonb_to_recordset($2::jsonb)
                    AS cursor_record(peer_device_id uuid, after_seq bigint)
            )
            SELECT env.envelope_id, env.message_id, env.sender_device_id,
                   env.recipient_device_id, env.protocol_version,
                   encode(env.packet, 'hex') AS packet_hex, env.seq,
                   COALESCE((SELECT json_agg(ea.attachment_id ORDER BY ea.attachment_id)
                             FROM app.envelope_attachments ea WHERE ea.envelope_id = env.envelope_id), '[]'::json)
            FROM app.message_envelopes AS env
            LEFT JOIN client_cursors AS cursor
                ON cursor.peer_device_id = env.sender_device_id
            -- Same-account and friend envelopes share this device-scoped inbox.
            -- Never broaden this to a user-wide query: other devices have other keys.
            WHERE env.recipient_device_id = $1::uuid
              AND env.seq > COALESCE(cursor.after_seq, 0::bigint)
            ORDER BY env.sender_device_id, env.seq, env.envelope_id
            LIMIT $3::bigint
        )", parameters);
        EnvelopePage page;
        page.envelopes.reserve(std::min(result.rowCount(), pageSize));
        std::size_t estimatedWireBytes = 0;
        for (std::size_t row = 0; row < result.rowCount(); ++row) {
            if (page.envelopes.size() == pageSize) {
                page.has_more = true;
                break;
            }
            auto packet = decodePacketHex(result.value(row, 5));
            const std::size_t estimatedRecordBytes = 4 * ((packet.size() + 2) / 3) + 2048;
            if (estimatedRecordBytes > pageWireBudget - estimatedWireBytes) {
                page.has_more = true;
                break;
            }
            std::vector<boost::uuids::uuid> attachmentIds;
            const auto ids = nlohmann::json::parse(result.value(row, 7));
            for (const auto& id : ids) attachmentIds.push_back(boost::uuids::string_generator{}(id.get<std::string>()));
            page.envelopes.push_back(Envelope{
                .envelope_id = boost::uuids::string_generator{}(std::string{result.value(row, 0)}),
                .message_id = boost::uuids::string_generator{}(std::string{result.value(row, 1)}),
                .sender_device_id = boost::uuids::string_generator{}(std::string{result.value(row, 2)}),
                .recipient_device_id = boost::uuids::string_generator{}(std::string{result.value(row, 3)}),
                .protocol_version = std::string{result.value(row, 4)},
                .packet = std::move(packet),
                .seq = parseSeq(result.value(row, 6)),
                .attachment_ids = std::move(attachmentIds)
            });
            estimatedWireBytes += estimatedRecordBytes;
        }
        return page;
    });
}

std::optional<std::vector<boost::uuids::uuid>> EnvelopeRepository::ackEnvelopes(
    const boost::uuids::uuid& user_id, const AckEnvelopeRequest& request) {
    backend::logging::Scope operationLog{"EnvelopeRepository", "ackEnvelopes"};
    if (user_id.is_nil() || request.device_id.is_nil() ||
        request.envelope_ids.empty() || request.envelope_ids.size() > 100) {
        throw std::invalid_argument("Invalid acknowledgment request");
    }
    std::set<std::string> seen;
    std::string ids = "{";
    for (const auto& id : request.envelope_ids) {
        const auto text = boost::uuids::to_string(id);
        if (id.is_nil() || !seen.insert(text).second)
            throw std::invalid_argument("Invalid or duplicate envelope identifier");
        if (ids.size() > 1) ids += ',';
        ids += text;
    }
    ids += '}';
    ConnectionLease lease{databasePool_};
    return lease.connection().withTransaction([&]() -> std::optional<std::vector<boost::uuids::uuid>> {
        const std::string deviceParameters[] = {
            boost::uuids::to_string(user_id), boost::uuids::to_string(request.device_id)};
        auto device = lease.connection().execute(R"(
            SELECT ud.device_id
            FROM app.user_devices AS ud
            JOIN app.users AS u ON u.id = ud.user_id
            WHERE ud.user_id = $1::uuid AND ud.device_id = $2::uuid
              AND ud.status = 'active' AND u.is_active = TRUE
            FOR SHARE OF ud, u
        )", deviceParameters);
        if (device.rowCount() == 0) return std::nullopt;
        const std::string parameters[] = {deviceParameters[1], ids};
        auto result = lease.connection().execute(R"(
            UPDATE app.message_envelopes
            SET received_at = COALESCE(received_at, CURRENT_TIMESTAMP)
            WHERE recipient_device_id = $1::uuid
              AND envelope_id = ANY($2::uuid[])
            RETURNING envelope_id
        )", parameters);
        std::vector<boost::uuids::uuid> acknowledged;
        acknowledged.reserve(result.rowCount());
        for (std::size_t row = 0; row < result.rowCount(); ++row)
            acknowledged.push_back(boost::uuids::string_generator{}(std::string{result.value(row, 0)}));
        return acknowledged;
    }); // Reply only after commit; retries preserve the original received_at.
}
