#ifndef CPP_BACKEND_SERVER_ENVELOPEREPOSITORY_H
#define CPP_BACKEND_SERVER_ENVELOPEREPOSITORY_H
#include "databases/DatabasePool.h"
#include "domain/NewMessage.h"
#include <optional>
#include "domain/AckEnvelopeRequest.h"
#include <domain/SyncEnvelopeRequest.h>
#include <domain/Envelope.h>
enum class NewMessageStatus { Success, Duplicate_request, PermissionDenied, Conflict };
struct NewMessageResult {
    NewMessageStatus status;
    std::optional<StoredEnvelope> envelope = std::nullopt;
};
class EnvelopeRepository {
public:
    explicit EnvelopeRepository(DatabasePool& databasePool);
    // Returns business outcomes; infrastructure failures throw DatabaseError.
    NewMessageResult saveNewMessage(const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& sender_device_id,
        const NewTextMessageRequest& request);
    std::optional<EnvelopePage> syncEnvelopeRecord(const boost::uuids::uuid& user_id, const SyncEnvelopeRequest& request);
    // nullopt: device permission denied; empty vector: no matching envelopes.
    std::optional<std::vector<boost::uuids::uuid>> ackEnvelopes(
        const boost::uuids::uuid& user_id, const AckEnvelopeRequest& request);
private:
    DatabasePool& databasePool_;
};
#endif
