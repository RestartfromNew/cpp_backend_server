#ifndef CPP_BACKEND_SERVER_CHATSERVICE_H
#define CPP_BACKEND_SERVER_CHATSERVICE_H
#include "repository/EnvelopeRepository.h"
#include <optional>
#include "domain/AckEnvelopeRequest.h"
#include <string>
#include <domain/SyncEnvelopeRequest.h>
#include <domain/Envelope.h>
enum class NewTextMessageStatus {
    Success, Duplicate_request, InvalidRequest, PermissionDenied,
    Conflict, DatabaseUnavailable, RetryLater, InternalError
};
struct NewTextMessageResult {
    NewTextMessageStatus status;
    std::optional<StoredEnvelope> envelope = std::nullopt;
};
class ChatService {
public:
    explicit ChatService(EnvelopeRepository& envelopeRepository);
    void printId(std::string id, std::string message);
    NewTextMessageResult newTextMessage(const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& sender_device_id,
        const NewTextMessageRequest& request);
    std::optional<EnvelopePage> syncEnvelopePage(const boost::uuids::uuid& user_id,
        const SyncEnvelopeRequest& request);
    // nullopt: device permission denied; empty vector: no matching envelopes.
    std::optional<std::vector<boost::uuids::uuid>> ackEnvelopes(
        const boost::uuids::uuid& user_id, const AckEnvelopeRequest& request);
private:
    EnvelopeRepository& envelope_repository_;
};
#endif
