#ifndef CPP_BACKEND_SERVER_ENVELOPEREPOSITORY_H
#define CPP_BACKEND_SERVER_ENVELOPEREPOSITORY_H
#include "databases/DatabasePool.h"
#include "domain/NewMessage.h"
#include <optional>
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
private:
    DatabasePool& databasePool_;
};
#endif
