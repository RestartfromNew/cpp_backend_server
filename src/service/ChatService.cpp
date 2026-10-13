#include "service/ChatService.h"
#include "databases/DatabaseError.h"
#include <exception>
#include <iostream>

ChatService::ChatService(EnvelopeRepository& repository) : envelope_repository_(repository) {}
void ChatService::printId(std::string id, std::string message) {
    (void)message;
    std::cout << "printId:" << id << '\n';
}
NewTextMessageResult ChatService::newTextMessage(const boost::uuids::uuid& user_id,
    const boost::uuids::uuid& sender_device_id, const NewTextMessageRequest& request) {
    if (user_id.is_nil() || sender_device_id.is_nil() || request.message_id.is_nil() ||
        request.recipient_device_id.is_nil() || sender_device_id == request.recipient_device_id ||
        request.packet.empty() || request.packet.size() > 64 * 1024 ||
        request.protocol_version.size() > 128 ||
        request.protocol_version.find_first_not_of(" \t\r\n") == std::string::npos) {
        return {NewTextMessageStatus::InvalidRequest};
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            const auto result = envelope_repository_.saveNewMessage(user_id, sender_device_id, request);
            switch (result.status) {
                case NewMessageStatus::Success:
                case NewMessageStatus::Duplicate_request:
                    if (!result.envelope) return {NewTextMessageStatus::InternalError};
                    return {result.status == NewMessageStatus::Success
                        ? NewTextMessageStatus::Success : NewTextMessageStatus::Duplicate_request,
                        result.envelope};
                case NewMessageStatus::PermissionDenied:
                    return {NewTextMessageStatus::PermissionDenied};
                case NewMessageStatus::Conflict:
                    return {NewTextMessageStatus::Conflict};
            }
            return {NewTextMessageStatus::InternalError};
        } catch (const DatabaseError& error) {
            // Log diagnostic codes, never SQL, token, packet or exception text.
            std::cerr << "[newTextMessage] SQLSTATE=" << error.code() << '\n';
            const bool transactionRetry = error.code() == "40001" || error.code() == "40P01";
            const bool uuidCollision = error.code() == "23505" &&
                error.constraint() == std::optional<std::string>{"message_envelopes_pkey"};
            if (transactionRetry || uuidCollision) {
                if (attempt < 2) continue; // Retry whole operation after rollback.
                return {NewTextMessageStatus::RetryLater};
            }
            if (error.kind() == DatabaseErrorKind::Connection || error.code().starts_with("08")) {
                // Commit may have succeeded: caller must retry the SAME identifier and bytes.
                return {NewTextMessageStatus::DatabaseUnavailable};
            }
            return {NewTextMessageStatus::InternalError};
        } catch (const std::exception&) {
            std::cerr << "[newTextMessage] unexpected failure\n";
            return {NewTextMessageStatus::InternalError};
        }
    }
    return {NewTextMessageStatus::RetryLater};
}
std::optional<EnvelopePage> ChatService::syncEnvelopePage(
    const boost::uuids::uuid& user_id, const SyncEnvelopeRequest& request) {
    // nullopt means permission denied; an empty page is successful synchronization.
    // Database exceptions remain available to the existing Task error boundary.
    return envelope_repository_.syncEnvelopeRecord(user_id, request);
}

std::optional<std::vector<boost::uuids::uuid>> ChatService::ackEnvelopes(
    const boost::uuids::uuid& user_id, const AckEnvelopeRequest& request) {
    return envelope_repository_.ackEnvelopes(user_id, request);
}
