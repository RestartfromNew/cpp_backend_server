#ifndef CPP_BACKEND_SERVER_ENVELOPEREPOSITORY_H
#define CPP_BACKEND_SERVER_ENVELOPEREPOSITORY_H
#include "databases/DatabasePool.h"
#include "domain/NewMessage.h"
#include <optional>
#include "domain/AckEnvelopeRequest.h"
#include <domain/SyncEnvelopeRequest.h>
#include <domain/Envelope.h>
enum class NewMessageStatus { Success, Duplicate_request, PermissionDenied, Conflict };
/** @brief 存储业务状态及可选信封；成功/重复时携带原信封，拒绝不是数据库异常。 */
struct NewMessageResult {
    NewMessageStatus status;
    std::optional<Envelope> envelope = std::nullopt;
};
/** @brief 信封存储、分页同步与 ACK；事务内验证权限并分配设备对序号，基础设施失败抛 DatabaseError。 */
class EnvelopeRepository {
public:
    explicit EnvelopeRepository(DatabasePool& databasePool);
    // Returns business outcomes; infrastructure failures throw DatabaseError.
    NewMessageResult saveNewMessage(const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& sender_device_id,
        const NewTextMessageRequest& request);
    // nullopt 表示设备权限拒绝；空 envelopes 是成功但无新消息。未知 peer 的游标按 0 处理。
    std::optional<EnvelopePage> syncEnvelopeRecord(const boost::uuids::uuid& user_id, const SyncEnvelopeRequest& request);
    // nullopt: device permission denied; empty vector: no matching envelopes.
    std::optional<std::vector<boost::uuids::uuid>> ackEnvelopes(
        const boost::uuids::uuid& user_id, const AckEnvelopeRequest& request);
private:
    DatabasePool& databasePool_;
};
#endif
