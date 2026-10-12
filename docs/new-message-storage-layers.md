# 新消息存储：从 Dispatcher 到 Repository 的结果与错误

更新：2026-10-12。本次实现只完成一个目标设备的信封存储与发送端结果回复，不实现对方推送、ACK 或离线补拉。

## 1. 这次的数据模型

message_envelopes 是唯一写入对象：message_id 是客户端生成的稳定重试 UUID，envelope_id 是数据库默认生成的信封 UUID。seq 是两个设备（不区分方向）下的递增整数，不是整表 IDENTITY。

```text
A1→B1，message_id=M1 → envelope_id=E1，seq=1
B1→A1，message_id=M2 → envelope_id=E2，seq=2
A1→B1，重试M1且内容相同 → 返回E1、1，不插入
A1→B1，重试M1但内容不同 → Conflict
A1→B2，message_id=M1 → 另一设备对自己的seq
```

重试范围由 UNIQUE(sender_device_id, recipient_device_id, message_id) 定义。客户端想跨目标设备全局唯一绑定 message_id 时，需要另加逻辑消息模型；本实现不声称提供这种绑定。

## 2. 各层只处理自己能解释的问题

| 层 | 输入 | 返回 | 失败处理 |
|---|---|---|---|
| Dispatcher / 私有解析函数 | 内部 frame JSON、Reply | 解析对象或JSON回复 | 字段格式错误返回 invalid_request |
| ChatService | 认证账号、发送设备、请求结构体 | NewTextMessageResult | 将数据库异常映射成稳定业务状态 |
| EnvelopeRepository | 同上，已通过基础格式检查 | NewMessageResult | 预期拒绝用枚举；数据库故障抛 DatabaseError |
| DatabaseConnection | SQL 和文本参数 | DatabaseResult | 保留 SQLSTATE/约束等诊断信息，失败抛异常 |

不要让 Repository 生成 JSON；不要让 Service 调用 Reply；不要让 Dispatcher 理解 SQLSTATE。业务结果返回值与异常分工：权限拒绝、内容冲突和重复请求属于正常可预期分支，使用枚举；数据库断开、SQL失败、反序列化异常属于基础设施失败，使用异常向 Service 汇报。

## 3. C++ 类型与实际类接口

### 3.1 领域请求与保存凭据

定义在 include/domain/NewMessage.h，所有字符串和字节数组都拥有自己的内存，不返回 DatabaseResult 的 string_view。

```cpp
struct NewTextMessageRequest {
    boost::uuids::uuid message_id;
    boost::uuids::uuid recipient_device_id;
    std::string protocol_version;
    std::vector<std::uint8_t> packet;
};
struct StoredEnvelope {
    boost::uuids::uuid envelope_id;
    boost::uuids::uuid message_id;
    std::int64_t seq;
};
```

原来两个普通 enum 都定义 Success 等全局枚举项，会重定义。改为 enum class 后，必须写 NewMessageStatus::Success 或 NewTextMessageStatus::Success。

### 3.2 Repository 结果

```cpp
enum class NewMessageStatus {
    Success, Duplicate_request, PermissionDenied, Conflict
};
struct NewMessageResult {
    NewMessageStatus status;
    std::optional<StoredEnvelope> envelope = std::nullopt;
};
class EnvelopeRepository {
public:
    explicit EnvelopeRepository(DatabasePool&);
    NewMessageResult saveNewMessage(
        const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& sender_device_id,
        const NewTextMessageRequest& request);
private:
    DatabasePool& databasePool_;
};
```

Success/Duplicate_request 必须带 envelope；PermissionDenied/Conflict 没有凭据。例如 `return {NewMessageStatus::PermissionDenied};`，不会伪造空UUID成功记录。optional 不是异常对象，而是表达结果中是否存在保存凭据。Service 检查成功分支确实有值。

### 3.3 Service 结果

```cpp
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
    explicit ChatService(EnvelopeRepository&);
    NewTextMessageResult newTextMessage(
        const boost::uuids::uuid& user_id,
        const boost::uuids::uuid& sender_device_id,
        const NewTextMessageRequest& request);
private:
    EnvelopeRepository& envelope_repository_;
};
```

Service 不再持有 UserRepository 做独立权限预检查，避免使用不同数据库连接检查后再写。权限判断合并到 Repository 同一事务。

Service 再次检查 nil UUID、自发同一设备、空包/超64KiB、协议版本长度/空白，保护其他内部调用入口，不只依赖 Dispatcher。

## 4. Repository 事务顺序与为什么这样写

1. 借用一个 ConnectionLease，所有 SQL 都使用它；withTransaction 执行 BEGIN，回调返回后 COMMIT，异常时 ROLLBACK。
2. 设置本事务为 READ COMMITTED，然后获得设备对的 pg_advisory_xact_lock。键是带用途前缀、固定排序的两个 UUID 的哈希。A→B/B→A 共享一把锁；不同对哈希碰撞最多导致额外等待，不使序号重复。
3. 查询发送/接收设备和账号，加 FOR SHARE 行锁。检查两设备 active、两账号 active、发送设备属于 Token user_id；拒绝同账号设备同步（需另设计接口）。
4. 查询并锁住两个方向的好友记录，要求正好两条；符合项目双向好友模型。
5. 查 `(sender, recipient, message_id)` 的已有信封。比较协议版本和完整 packet；一致返回原凭据，不同返回 Conflict。
6. 新请求按 LEAST/GREATEST 查询 MAX(seq)+1 并插入，RETURNING envelope_id、seq。这个 MAX+1 只因为第2步的同对事务锁才安全。
7. 提交完成后 Repository 才把凭据返回 Service。Dispatcher 才回复 success。

设备行、账号行、好友行的锁防止这些记录在本事务结束前被撤销、停用或删除。所有写信封入口必须使用同一 advisory lock 规则；绕过规则的手工插入虽然有唯一约束兜底，仍会导致请求失败。

账户和好友行按固定排序锁定，降低死锁概率；其他模块的锁顺序仍可能不同，Service 对死锁和序列化失败做最多3次整事务重试。[PostgreSQL 锁机制说明](https://www.postgresql.org/docs/current/explicit-locking.html)

本实现在权限验证后识别重试：若设备已撤销或好友已删除，即使以前保存过也返回 PermissionDenied，记录仍保留。客户端不能据此断定以前没有保存；恢复后查同步/状态需另实现。若产品要允许失去发送权限后仍查询自己的历史提交结果，应增加独立、明确授权的状态查询接口。

注意 MAX(seq)+1 的生命周期限制：本设计不允许删除设备对最高序号的记录，否则可能重用旧序号。需要历史清理时必须保留最高水位/墓碑，或改为持久计数器；客户端不能把未知空洞视为已处理。seq 不参加加密派生，也不由客户端提交。

## 5. 基础设施异常怎样映射

| Repository 故障 | Service 处理 | 客户端错误码 |
|---|---|---|
| SQLSTATE 40001 / 40P01 | 回滚后整操作最多3次；耗尽返回RetryLater | retry_later |
| 23505 且约束为 message_envelopes_pkey | 默认UUID碰撞，回滚后有限重试 | retry_later（耗尽） |
| Connection / SQLSTATE 08 类 | 不盲目自动重发；返回DatabaseUnavailable | database_unavailable |
| 其他SQL错误、转换失败、未知异常 | 记录安全诊断，返回InternalError | internal_server_error |

唯一约束异常不能一律当作重试成功。设备对序号冲突可能说明写入口未遵守锁规则；重试编号冲突也要比较完整内容。主键约束改名后需同步UUID碰撞识别规则。

连接在 COMMIT 时断开有“可能提交成功”的不确定性；database_unavailable 不代表一定回滚。客户端只能重试原 message_id、原目标、原协议版本和原 packet，不重新加密、换UUID。最多3次内部重试只用于已回滚的事务冲突或明确主键碰撞。

DatabaseError 可记录 kind/code/constraint，客户端只得到稳定错误码，不返回 what()、SQL、packet、Token或明文。业务返回值不携带数据库驱动对象或局部引用。

## 6. Dispatcher 的输入与输出

客户端发送的 Text payload：

```json
{
  "type":"chat.newTextMessage",
  "authorization":"Bearer ACCESS_TOKEN",
  "sender_device_id":"发送设备UUID",
  "message_id":"客户端稳定UUID",
  "recipient_device_id":"接收设备UUID",
  "protocol_version":"实际协议版本",
  "packet_base64":"AQID"
}
```

AQID 是测试字节 01 02 03，不是可解密协议包。服务端存储不透明字节，因此可用于链路测试，但客户端加密另行实现。

服务器内部 ApplicationParse 包装为 header/payload_format/payload；客户端不重复发送这个包装。Dispatcher 检查包装、type 和authorization，Token获得userId；在业务Task中调用私有parseNewMessage，再调用Service。当前仍按报文验证Token与发送设备归属，不声称已经实现连接设备绑定。

成功与重复均 success=true，duplicate 区分：

```json
{"type":"newTextMessage.result","success":true,"duplicate":false,"message_id":"客户端UUID","envelope_id":"数据库UUID","seq":1}
```

```json
{"type":"newTextMessage.result","success":true,"duplicate":true,"message_id":"原客户端UUID","envelope_id":"原数据库UUID","seq":1}
```

失败结构：

```json
{"type":"newTextMessage.result","success":false,"error":{"code":"send_permission_denied","message":"Sender or recipient is not permitted"}}
```

业务映射：InvalidRequest→invalid_request；PermissionDenied→send_permission_denied；Conflict→envelope_conflict；DatabaseUnavailable→database_unavailable；RetryLater→retry_later；InternalError→internal_server_error。

解析或路由错误还有 invalid_payload、missing_authorization、invalid_access_token、access_token_expired、unsupported_type；入队失败为 server_busy。Task 保留可调用的 Reply，入队失败仍能报告。Task异常有捕获，ServiceThreadPool再加最后一道异常屏障，避免未捕获异常杀死整个工作线程。

此版本没有新增 request_id 关联字段；如需并行请求的失败响应关联，应给报文加 request_id 并在每个错误回复中原样携带经过类型/长度校验的值。当前客户端必须按实际回复结构处理，不能假定它存在。

## 7. 依赖注入与对象生命周期

main中构造：

```cpp
EnvelopeRepository envelopeRepository{databasePool};
ChatService chatService{envelopeRepository};
ServiceThreadPool service_thread_pool(2, chatService);
WebSocketDispatcher web_socket_dispatcher(service_thread_pool, authMiddleWare);
```

线程池保存 ChatService&，不再试图默认构造需要Repository的ChatService。声明顺序保证引用目标活得更久；仍需要在退出时关闭线程池，使Task不再访问已销毁对象。

## 8. 数据库准备与实际验证边界

配套 [prepare-envelope-message-storage.sql](prepare-envelope-message-storage.sql) 改变 message_id 的语义，移除旧 app.message 外键及旧两列唯一约束，确保seq非空，添加设备对/seq表达式唯一索引和三列重试唯一索引。它未自动执行。

执行前核对备份和现有数据：历史message_id原来指向逻辑消息，迁移只保留数值，不自动恢复客户端重试关系；如果低/高生成列已经存在，可保留但本代码不依赖。混合已分配与NULL seq的数据拒绝自动回填。IF NOT EXISTS不验证同名索引定义，末尾查询用于人工核对。

此版成功只表示信封持久化，尚不向目标设备推送。当前每条请求只写一个目标信封，给多设备分别提交，并非整个多设备批次原子。全应用依赖Linux/WSL和PostgreSQL；隔离测试不能替代真实数据库并发、迁移和网络验收。

建议真实验收：同对双向并发、同包并发重试、同编号不同包、设备撤销/好友删除与发送竞态、COMMIT后断线重试、数据库错误、首次seq=1及第二次seq=2、不同设备对独立seq、失败后无新信封、未授权请求无法写入。


## 9. 本次已运行的检查

隔离测试47项通过：实际编译Dispatcher、Service、Repository和DatabaseError；数据库、UUID、Token和Base64依赖及队列执行被模拟。测试代码保存在 tests/message_storage_isolated。源码差异格式检查通过。未运行真实PostgreSQL迁移、并发锁测试或完整WSL服务；WSL列举被环境拒绝。现有DatabaseError构造函数初始化顺序产生已有reorder警告，未改变其行为。
