#ifndef CPP_BACKEND_SERVER_CHATSERVICE_H
#define CPP_BACKEND_SERVER_CHATSERVICE_H
#include "repository/EnvelopeRepository.h"
#include <optional>
#include "domain/AckEnvelopeRequest.h"
#include <string>
#include <domain/SyncEnvelopeRequest.h>
#include <domain/Envelope.h>
// 业务返回状态；重试保持同一 message_id/packet，Success 和重复请求才携带已保存信封。
enum class NewTextMessageStatus {
    Success, Duplicate_request, InvalidRequest, PermissionDenied,
    Conflict, DatabaseUnavailable, RetryLater, InternalError
};
/** @brief 发送业务状态及可选完整信封；Success 代表持久化，不能解释为接收、解密或已读。 */
struct NewTextMessageResult {
    NewTextMessageStatus status;
    std::optional<Envelope> envelope = std::nullopt;
};
/** @brief 信封业务与有限事务重试；返回状态/数据，不构造 WebSocket JSON、不操作在线 Session。 */
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
