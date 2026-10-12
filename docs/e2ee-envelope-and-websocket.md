# 端到端加密：设备信封、密文包与 WebSocket 投递设计

更新日期：2026-10-09。适用项目：cpp_backend_server。

## 1. 文档范围与当前实现状态

本文连接三个层次：数据库 message_envelopes 保存什么、客户端 packet 如何编码、WebSocket 应用消息如何提交和投递。三者不能混为一谈。

已在项目中实现的基础能力包括：账号登录、设备登记、一次性预密钥上传、好友列表、好友设备目录、带签名预密钥查询及一次性预密钥领取。具体路径见 [backend-api.md](backend-api.md)。

本次检查中，EnvelopeRepository.h/.cpp 尚没有持久化实现；WebSocketDispatcher 只处理 chat.newTextMessage 测试消息，调用 ChatService.printId，再回复 newTextMessage.result。本文中的 chat.envelopes.submit、push、ack、sync 等是建议新增的应用协议，不是现有接口。本文没有执行数据库迁移或实现客户端加密。

下文的 E2EE-DEMO-V1 是教学用自定义封装，不是 Signal 线格式，不与 Signal 客户端互通，不是经过审计的密码协议。接入成熟协议库后，应优先存储其完整序列化结果；不得只照着字节布局自行拼出“完整安全协议”。现有 TEST-X25519-XEdDSA-HKDF-SHA256-v1 测试资料也不等于 Double Ratchet 已经实现。

## 2. 三层结构与职责

```text
客户端明文
  → 客户端协议库加密 → packet 原始字节
  → Base64 编码 → WebSocket Text 帧中的 JSON
  → 服务器解析外层、Base64 解码 → message_envelopes.packet BYTEA
  → 提交成功 → 路由至目标设备的 WebSocket Worker
  → 推送 JSON → 接收端还原 packet、认证解密、本地持久化
```

| 层次 | 内容 | 负责方 |
|---|---|---|
| 逻辑消息 | 一次发送的 message_id、会话归属 | 客户端生成稳定编号，服务器验证并保存 |
| 设备信封 envelope | 发送设备、目标设备、完整 packet | 服务器验证投递权限并持久化 |
| 加密协议包 packet | 协商头、棘轮头、密文和认证资料 | 客户端协议库生成和解释 |
| WebSocket Frame | opcode、长度、mask 等传输字段 | 网络层编码和解析 |
| WebSocket 应用 JSON | type、request_id、信封列表等 | 客户端与业务层约定 |

同一条逻辑消息发给 B1、B2，需要两份各自加密的信封。A1 与 B1 的会话状态不同于 A1 与 B2，不能把一个设备密文复制给另一台设备。发送端其他设备的同步也需要独立加密投递和明确的授权规则。

## 3. 数据库 message_envelopes 表

### 3.1 字段含义

沿用 [database.md](database.md) 第 6.5 节的目标设计；实际数据库约束和默认值须通过正式迁移核对。

| 字段 | PostgreSQL 类型 | 必填 | 含义和来源 |
|---|---|---|---|
| envelope_id | UUID | 是 | 客户端在加密前生成；网络重试保持不变 |
| message_id | UUID | 是 | 关联 app.message(id)，多个目标设备共用逻辑消息编号 |
| sender_device_id | UUID | 是 | 服务端从已验证、绑定设备的认证上下文确定 |
| recipient_device_id | UUID | 是 | 本信封唯一目标设备，服务端验证归属和有效性 |
| protocol_version | TEXT | 是 | 精确的协议与封装版本，不是模糊的“使用加密” |
| packet | BYTEA | 是 | 原始二进制协议包，不存 Base64 字符串的 UTF-8 字节 |
| created_at | TIMESTAMPTZ | 是 | 服务端接受时间，不是客户端排序声明 |
| received_at | TIMESTAMPTZ | 否 | 接收端报告已可靠保存信封的时间 |
| decrypted_at | TIMESTAMPTZ | 否 | 接收端报告已成功解密并保存结果的时间 |

BYTEA 适合包含 0x00 等任意字节的内容。C++ 可以使用 std::vector<std::uint8_t>；不可用 strlen 计算长度。采用文本参数时可用 decode(hex, 'hex')，也可扩展数据库封装支持真正的二进制绑定。Base64 只是传输编码，没有保密作用。

数据库不保存身份私钥、预密钥私钥、根密钥、链密钥、消息密钥或 S。服务器能看见账号/设备关系、时间、包大小和投递状态，因此此设计不能称为“元数据完全隐藏”。

### 3.2 建议约束、索引和不可变规则

以下是迁移设计示意，不是本次已执行的命令：

```sql
-- 主键：PRIMARY KEY (envelope_id)
-- 外键：message_id → app.message(id)
-- 外键：sender_device_id / recipient_device_id → app.user_devices(device_id)
-- 唯一约束：UNIQUE (message_id, recipient_device_id)
-- 内容约束：CHECK (octet_length(packet) > 0)
-- 同步索引：
CREATE INDEX message_envelopes_recipient_sync_idx
    ON app.message_envelopes (recipient_device_id, created_at, envelope_id);
```

已接受的 packet、message_id、发送/接收设备及协议版本不可偷偷覆盖。重试命中相同 envelope_id 时比较不可变字段和原始 packet：完全相同返回已接受；任何不同返回 envelope_conflict。不能简单 ON CONFLICT DO NOTHING 后一律声称成功。

外键只证明设备记录存在，不能证明它 active、属于该好友或者此次允许接收。权限必须由发送业务验证。设备宜软撤销，不应级联删除对方的历史信封。received_at/decrypted_at 的重复 ACK 不应反复改变第一次确认时间。

### 3.3 与现有 app.message 的迁移关系

现有 payload_ciphertext NOT NULL 是“一条逻辑消息一份密文”的模型；逐设备投递应将密文权威来源迁至 message_envelopes.packet，message 保留元数据。不能给原非空列塞空字节来绕过迁移，也不能让两份密文都成为含糊的权威来源。

实施时需另写迁移：检查现有字段、定义读写版本、安排旧数据读取策略，再调整约束。本设计文档不会自动改变数据库。

## 4. 一次发送的完整业务过程

1. A1 已登录、登记并持有本地私钥；连接经过身份验证并绑定 A1。
2. 获取 B 的有效设备目录。对于没有会话的目标设备，获取身份公钥、带签名预密钥和必要的一次性预密钥；验证签名和身份信任规则。
3. 为 B1、B2 分别建立/恢复会话。生成稳定 message_id 和各 envelope_id，再加密，不能加密后再任意更换被认证的编号。
4. 客户端将更新后的会话状态和完整待发送信封写入同一个本地事务。崩溃重试发送已保存的原包，不再次推进棘轮。
5. 提交 envelopes。服务端从认证上下文确定发送账号及设备；验证目标好友关系、设备归属/状态、会话权限、版本、长度和重复编号。
6. 同一事务保存逻辑消息及全部目标信封。本文建议整批原子接受；任一目标不符合规则则整批失败。重试前先识别已成功提交的相同批次，不能因之后好友撤销把以前成功的提交误报成未保存。
7. 数据库提交成功后回复 accepted，再安排推送。提交之前不得推送或确认成功。
8. 接收端可靠保存信封、去重、认证解密；原子保存新会话状态和解密结果，再报告确认。

目标设备变化时，若尚未提交可重新获取目录；若已经提交则保持原包，新增设备的补发用新信封和明确的版本化业务流程。新设备没有旧设备私钥和会话状态，不能仅凭登录解密旧信封。

## 5. 密码算法的角色和客户端状态

本文讨论经典 X3DH + Double Ratchet 的概念，并假设测试封装使用 X25519、HKDF/HMAC-SHA256 和 ChaCha20-Poly1305。实际协议 suite 必须由选定库固定，包括签名、字节编码和派生上下文，不能混用 XEdDSA 和 Ed25519。

| 名称 | 用途 | 是否传给服务器 |
|---|---|---|
| 身份公钥 | 核验设备密码身份 | 可以 |
| 预密钥公钥和签名 | 离线建立会话的公开资料 | 可以 |
| 棘轮公钥 | 会话中更新密钥链的公开材料 | 随消息包传输 |
| S / RK | 初始秘密 / 会话根密钥 | 不传 |
| CKs / CKr | 当前发送链 / 接收链密钥 | 不传 |
| MK | 某一条消息的消息密钥 | 不传 |
| 私钥 | 计算共享秘密、生成签名 | 不传 |

X25519 的配对计算示意：

```text
DH(a_private, b_public) == DH(b_private, a_public)
```

经典 X3DH 对应计算（符号均指密钥，不是数据库编号）：

```text
DH1 = DH(IK_A_private, SPK_B_public)
DH2 = DH(EK_A_private, IK_B_public)
DH3 = DH(EK_A_private, SPK_B_public)
DH4 = DH(EK_A_private, OPK_B_public)  # 领取并使用 OPK 时才有
S   = 协议规定的 KDF(DH1 || DH2 || DH3 [|| DH4])
```

接收端用对应私钥计算相同材料；必须遵守协议的编码和 KDF 域分离要求，不是直接把拼接结果当作 AEAD 密钥。S 只用于初始化，预密钥编号让 B 找到正确的本地私钥。[X3DH 原始规范](https://signal.org/docs/specifications/x3dh/)

后续密钥派生的抽象接口：

```text
(RK_next, CK)    = KDF_RK(RK, DH_result)
(CK_next, MK)    = KDF_CK(CK)
(ciphertext,tag) = AEAD_Encrypt(MK, nonce, plaintext, AAD)
plaintext        = AEAD_Decrypt(MK, nonce, ciphertext, tag, AAD)
```

棘轮公钥不是直接解密正文的钥匙：它参与 DH 和链更新。每条消息派生一个 MK；同一发送链上的多条消息可携带同一个棘轮公钥。收到新的对端棘轮公钥时更新链，N 是当前链的消息编号，PN 是上一发送链的消息总数。跳过密钥用于乱序收信，缓存需设上限。相关规则由协议库实现。[Double Ratchet 原始规范](https://signal.org/docs/specifications/doubleratchet/)

客户端至少保存：身份和预密钥私钥、每个设备会话的 RK/CK、当前棘轮私钥及对方棘轮公钥、计数器、受限的跳过密钥、信封去重记录和待发队列。AEAD 验证失败不提交试算后的会话状态。认证成功证明密钥及上下文匹配，不自动证明对方身份可信。

## 6. packet 二进制结构：教学封装 E2EE-DEMO-V1

### 6.1 固定编码规则

以下定义用于理解字节排列，是未来协议封装候选：

- 版本 format_version = 1；suite_id = 1 仅表示本文教学组合，不能冒充现有测试 suite。
- uint16/uint32/uint64 全部无符号、大端序。预密钥编号限制 0～2^63-1，与 BIGINT 对齐；现有 int 参数需要先升级才能覆盖此范围。
- X25519 公钥为协议规定的原始 32 字节，不能把 hex 字符串的 64 个字节放进去。
- flags bit0 表示初始化消息，bit1 表示携带 OPK 编号；其他位为 0。bit1 只允许在 bit0 为 1 时设置。
- 无填充、无隐式字符串终止符；拒绝未知版本、未知 suite、非法 flags、截断、超长和尾随字节。

### 6.2 字段排列

| 顺序 | 字段 | 字节数 | 解释 |
|---|---|---:|---|
| 1 | magic | 4 | ASCII E2EE，即 45 32 45 45 |
| 2 | format_version | 1 | 包格式版本 |
| 3 | suite_id | 2 | 协议实例和算法组合编号 |
| 4 | flags | 1 | 初始化/OPK 标志 |
| 5 | 初始化区 | 0 / 72 / 80 | 见下一小节 |
| 6 | ratchet_public_key | 32 | 当前发送端棘轮公钥 |
| 7 | PN | 4 | 上一发送链消息总数 |
| 8 | N | 4 | 当前链的消息编号，从 0 开始 |
| 9 | nonce | 12 | 本教学封装显式传输 |
| 10 | ciphertext_length | 4 | 正文字节数，不含 tag |
| 11 | ciphertext | L | 加密后的正文 |
| 12 | authentication_tag | 16 | 完整认证标签，不截短 |

初始化区依次为：sender_identity_public_key(32)、sender_ephemeral_public_key(32)、recipient_signed_prekey_id(8)，使用 OPK 时再加 recipient_one_time_prekey_id(8)。初始化临时公钥与棘轮公钥是不同角色，不自行合并。

普通包长度 = 80 + L；初始化无 OPK = 152 + L；初始化有 OPK = 160 + L。N/PN 溢出前必须按协议处理，不能回绕导致密钥或消息编号重用。

示例：普通消息 L=5，总长度 85。偏移均从 0 开始：

```text
00..03  45 32 45 45       magic
04      01                format_version
05..06  00 01             suite_id
07      00                flags
08..39  [32字节公钥]
40..43  00 00 00 03       PN=3
44..47  00 00 00 02       N=2
48..59  [12字节nonce]
60..63  00 00 00 05       L=5
64..68  [5字节密文]
69..84  [16字节tag]
```

中括号是占位说明，不能直接作为有效报文发送。nonce、ciphertext、tag 必须真正来自加密调用；随机填充不是可解密测试用例。

### 6.3 认证上下文 AAD

仅加密正文还不够，必须防止信封编号、目标设备和协议头被替换。候选构造如下：

```text
AAD = ASCII("cpp-backend-envelope-v1")
   || message_id 的16字节UUID
   || envelope_id 的16字节UUID
   || sender_device_id 的16字节UUID
   || recipient_device_id 的16字节UUID
   || packet_header（从 magic 到 ciphertext_length，包含初始化区）
```

UUID 按规范文本每两位 hex 转一个字节的顺序编码，不使用 Windows GUID 内存布局，也不使用带连字符的字符串。根协议要求的身份绑定信息应作为协议 AAD 的一部分保留；接入库时使用其规定的组合方法，不能丢掉。外层 protocol_version 必须与已登记的 suite_id/format_version 映射一致。

AAD 可从外层和包头重建，不一定在 packet 重复保存。两边必须逐字节一致；不要直接对 JSON dump 结果签认，因为字段顺序和空格可能不同。服务器时间、投递状态和 request_id 不加入 AAD，它们在重试或投递时可能变化。

AEAD 的 nonce 对同一密钥不能重用；重试传输原包是同一密文的重复发送，不是用旧 nonce 再加密新正文。生产实现由协议库管理 nonce 和消息密钥。[ChaCha20-Poly1305 定义](https://www.rfc-editor.org/rfc/rfc8439)

### 6.4 解析规则和大小限制

先检查最低长度、magic、版本、flags，确定初始化区长度，再读取头和 L。以防溢出的方式检查剩余字节数是否恰好为 L+16。根据 suite 验证公钥合法性，认证解密失败不返回任何明文。服务器只做安全的外形/大小检查，不参与密钥派生。

建议业务上限另设配置，例如一个 packet ≤ 64 KiB、一个批次 ≤ 32 个信封，总 WebSocket JSON ≤ 1 MiB；这些是拟定限制，不代表当前 FrameParser 已支持。Base64 长度约为 4*ceil(packet_size/3)，实际总量还需计入 JSON。未知或冲突的版本不得悄悄降级。

## 7. WebSocket 应用报文结构（拟定）

### 7.1 发送端提交：chat.envelopes.submit

客户端直接发送下列 JSON 作为 Text 帧 payload，不额外包一层 header/payload_format。

```json
{
  "type": "chat.envelopes.submit",
  "request_id": "请求UUID",
  "message_id": "逻辑消息UUID",
  "conversation_id": "会话UUID",
  "envelopes": [
    {
      "envelope_id": "B1信封UUID",
      "recipient_device_id": "B1设备UUID",
      "protocol_version": "E2EE-DEMO-V1",
      "packet_base64": "完整packet的Base64"
    },
    {
      "envelope_id": "B2信封UUID",
      "recipient_device_id": "B2设备UUID",
      "protocol_version": "E2EE-DEMO-V1",
      "packet_base64": "另一份独立packet的Base64"
    }
  ]
}
```

request_id 仅关联一次请求和结果；message_id/envelope_id 用于持久化去重。conversation_id 的权限由服务器验证；若它决定客户端的明文归属，也应加入固定 AAD 定义，不能由攻击者改了仍有效。

发送设备来自已绑定连接上下文，不接受 JSON 中任意 sender_device_id。客户端在加密时使用自己已确认的设备编号加入 AAD；服务器验证该编号与连接一致，不擅自换成另一设备。严格 Base64 解码后存原字节。

### 7.2 服务端持久化结果

```json
{
  "type": "chat.envelopes.accepted",
  "request_id": "请求UUID",
  "message_id": "逻辑消息UUID",
  "envelope_ids": ["B1信封UUID", "B2信封UUID"],
  "accepted_at": "2026-10-09T12:00:00Z"
}
```

accepted 只代表服务器已提交，不能代表对方收到、解密或读过。已提交的相同重试返回相同已接受信封；连接在提交后断开时，客户端仍用原编号和原字节重试。

失败报文示意：

```json
{
  "type": "chat.envelopes.error",
  "request_id": "请求UUID",
  "error": {"code": "recipient_device_unavailable", "message": "Target device is unavailable"}
}
```

其他建议错误码：invalid_request、invalid_packet、unsupported_protocol、not_friends、envelope_conflict、database_unavailable、server_busy。WebSocket 应用错误不等同于 HTTP 状态码。没有合法 request_id 时允许省略它；错误消息不包含 SQL 或密钥内容。

### 7.3 推送至目标设备

```json
{
  "type": "chat.envelope.push",
  "envelope": {
    "envelope_id": "B1信封UUID",
    "message_id": "逻辑消息UUID",
    "conversation_id": "会话UUID",
    "sender_device_id": "A1设备UUID",
    "recipient_device_id": "B1设备UUID",
    "protocol_version": "E2EE-DEMO-V1",
    "packet_base64": "与数据库原包完全对应的Base64",
    "created_at": "2026-10-09T12:00:00Z"
  }
}
```

只投递到 recipient_device_id 的授权连接，不能把 B1 包广播给 B 的所有设备。重连或多个同设备连接可能造成重复，接收端始终按 envelope_id 去重。

### 7.4 接收确认与解密确认

```json
{"type":"chat.envelope.ack","request_id":"请求UUID","envelope_id":"B1信封UUID","stage":"received"}
```

```json
{"type":"chat.envelope.ack","request_id":"请求UUID","envelope_id":"B1信封UUID","stage":"decrypted"}
```

received 表示客户端已可靠保存，decrypted 表示认证解密和本地事务成功，不表示用户已读。服务端从认证连接验证 ACK 属于目标设备，只写入允许更新的确认字段，记录的是报告被服务端接受的时间。恶意客户端可以谎报，服务器不能独立证明它解密了。需要向发送端提供密码确认时，应另发会话内加密确认消息。

## 8. 离线补拉、断线和可靠投递

推荐至少一次投递：服务端可能重复发送，但不能依赖单次在线推送作为唯一交付方式。

```json
{"type":"chat.envelopes.sync","request_id":"请求UUID","limit":100,"page_token":null}
```

返回示意：

```json
{"type":"chat.envelopes.sync.result","request_id":"请求UUID","envelopes":[],"next_page_token":null,"has_more":false}
```

设备来自连接上下文，不能通过提交另一个 device_id 补拉别人信封。简单可靠的第一版查询当前设备未确认 received 的信封，用有界分页；ACK 丢失则重发。查询页令牌应由服务器签发/校验。

不要仅以“最新 created_at/envelope_id”或 BIGSERIAL 当永久水位：事务的分配/时间顺序可能不同于提交顺序，较早编号的事务晚提交会被跳过。分页结束后应重新扫描未确认集合；若需要永久增量水位，应单独设计按设备串行分配的提交安全序号。created_at/envelope_id 索引可用于分页，不自动解决该问题。

服务端崩溃发生在提交后、推送前时，数据库信封仍存在。可同事务写投递 outbox，由后台重试；或用持久化信封加补拉提供恢复基础。只把推送任务放进内存队列并不能承诺可靠实时送达。

| 故障位置 | 行为 |
|---|---|
| 提交前失败 | 不回复 accepted，不推送 |
| 提交成功、accepted 丢失 | 发送端重试原包，服务端去重 |
| 提交成功、目标离线 | 保留信封，在线后补拉 |
| 客户端保存成功、ACK 丢失 | 再次收到后去重并补 ACK |
| 客户端解密失败 | 保留故障资料，不报 decrypted，不提交损坏会话状态 |
| 新设备上线 | 获取新设备会话所需材料；旧包不会自动变成可解密 |

## 9. 与当前 WebSocket 线程结构衔接

当前 ApplicationParse 把 Text JSON 包装成内部结构：

```json
{"header":{"fin":true,"opcode":1,"opcode_name":"text","payload_length":123},"payload_format":"json","payload":{"type":"chat.envelopes.submit"}}
```

这是服务器内部包装，不是客户端要发送的应用报文。未来 Dispatcher 读取 frame.payload 的 type；必须先验证 payload_format、对象类型和字段类型，不能直接 payload["type"] 转 string 导致未捕获异常。

```text
WebSocket Worker：读帧、校验、解析JSON、附加认证账号/设备上下文
  → ServiceThreadPool：授权检查、数据库事务、提交结果
  → Reply：将结果投回发送连接所属Worker
  → 设备连接注册表：查目标设备的Worker/会话
  → 目标Worker任务队列：编码推送帧、写发送缓冲区
```

Reply 当前主要返回原发送连接；投递对方需要新增按设备定位连接的能力。业务线程不能直接调用对方 socket send 或操作 WebSocketSession。跨 Worker 任务需验证连接代次，防止 fd 重用后误投；队列满时返回 server_busy 或保留待投递任务，不阻塞网络线程。

在升级握手或受限的连接认证阶段验证 Token 和设备归属；未认证连接不能提交、领取或补拉。当前源码还没有可据以声称“连接已绑定设备”的完整消息投递实现，必须实现这一前置条件。使用 WSS；WebSocket mask 是传输协议机制，不是端到端加密。

更多现有线程流程见 [websocket-worker-message-flow.md](websocket-worker-message-flow.md)。

## 10. 为什么采用这些设计

| 决策 | 原因 |
|---|---|
| 一逻辑消息、多设备信封 | 各设备私钥和会话独立 |
| packet 保存 BYTEA | 原样保留所有字节，服务器不需要解析密文内部 |
| JSON 使用 Base64 | 任意字节不能直接放入 JSON 字符串；容易接入现有 Text 帧解析 |
| 外层投递字段与内层包分开 | 服务器可以鉴权、路由和同步，同时不接触秘密 |
| 关键外层字段加入 AAD | 防止消息被移到另一个身份、设备或编号下仍有效 |
| 客户端原子保存会话状态和待发包 | 避免崩溃后重复用密钥加密不同消息 |
| 先提交再推送 | 不让接收端看见服务器没有可靠保存的消息 |
| 稳定信封编号与严格内容比较 | 网络重试安全去重，冲突不会覆盖历史 |
| 独立 received/decrypted 确认 | 区分保存、解密、已读，避免成功语义混淆 |
| 在线推送加离线补拉 | 连接中断或进程重启不丢失已提交信封 |

## 11. 实施顺序与验收

1. 选定客户端协议库、suite、初始化和序列化格式；创建双方可互解的测试向量。
2. 完成 Token 与设备绑定、发送权限和设备目录版本规则。
3. 制定 message 到 message_envelopes 的迁移，实现事务持久化和严格重试去重。
4. 新增 submit/accepted、目标设备推送、ACK 和补拉，保持 Worker 线程边界。
5. 客户端实现原子状态、待发队列、乱序密钥缓存、身份变更提示和设备撤销处理。

验收至少覆盖：多设备独立解密、错误预密钥编号、篡改头/AAD/密文/tag、同包重试、同编号不同内容、提交后断线、乱序/重复消息、设备撤销、非好友发送、未授权补拉/ACK，以及分页期间晚提交事务不会遗漏。日志不得输出私钥、S、MK 或明文。

本文是设计与教学说明；实际数据库、客户端协议、WebSocket 消息接口和迁移均须分别落实，不能把文档中的报文示例当成已经上线的实现。
