# 附件控制协议与 iOS 收发约定

日期：2026-11-03。最终讨论方案的协议草案，**尚未实现/注册**；不要把下列 type 和 HTTP 路径当成现有 API。总体流程见 [flows.md](flows.md)。

## 1. 一张图片从 Alice 到 Bob

```text
Alice iOS 生成独立文件密钥 → 加密为本地临时密文文件
    ↓
WebSocket file.upload.request 申请许可
    ↓
业务池校验会话、创建 attachment / file_uploads → 返回许可
    ↓
独立 HTTP PUT /upload_file → UploadReactor → 有界队列 → 磁盘线程池
    ↓
写完、校验、持久化发布 → DB attachment=ready，upload=completed
    ↓
可选 WebSocket file.upload.ready 提示；无连接则重连查询
    ↓
Alice 为各目标设备构造加密附件描述，通过 chat.newTextMessage 提交
    ↓
保存 message_envelopes + envelope_attachments，同事务提交
    ↓
Bob 在线 WebSocket 收到，或离线后通过现有同步取得同一信封
    ↓
Bob 解密描述 → HTTP 申请下载授权 → HTTP 下载文件密文 → 本地认证解密
```

WebSocket 可以传大字节数据是协议能力，但本项目不采用。现有 packet 限 64 KiB，图片/文件字节不得放入 packet，也不作为 file.upload.request 的 Base64 字段。许可和信封是小型控制报文，上传/下载是独立 HTTPS 字节流。

## 2. 与当前认证和线程模型衔接

客户端发送原始 JSON，由现有 Worker 封装后交 Dispatcher；不要求 iOS 构造服务器内部 header/payload_format 包装。新增控制 type 仍带 authorization，延续当前业务报文认证，并核对已握手 Session user/device、账号和 active 状态。

控制请求不接受任意 uploader_id/user_id/device_id 替代会话身份。Worker 只分派；UploadService 的查库/事务在有界业务池执行。回复回投原 Worker 并核对 session_id/代次，防止 socket fd 复用或旧请求回复串到新连接。

file_uploads 不持有 socket fd。合法凭据的 HTTP 上传不依赖 WebSocket 在线；普通断线不取消任务。新上传请求、renew、status、cancel 都需重新校验当前认证。HTTP 请求独立验证身份、任务归属、短期凭据和期限，设备撤销后拒绝新接收/发布。

## 3. 许可请求与回复

```json
{
  "type": "file.upload.request",
  "request_id": "创建请求UUID",
  "authorization": "Bearer <AccessToken>",
  "size_bytes": 182430,
  "sha256_hex": "可选的64位十六进制密文摘要"
}
```

request_id 必须是非零 UUID，持久化为 file_uploads.client_request_id；客户端应保留用于断线重试。size_bytes 是密文长度，服务端按配置执行上限。sha256_hex 不需要时完全省略，不发送上面占位文字。

```json
{
  "type": "file.upload.request.result",
  "request_id": "创建请求UUID",
  "success": true,
  "attachment_id": "附件UUID",
  "upload_id": "任务UUID",
  "upload_state": "authorized",
  "upload_url": "https://部署入口/upload_file",
  "method": "PUT",
  "upload_token": "短期随机凭据",
  "expires_at": "上传截止时间",
  "required_headers": {
    "Content-Type": "application/octet-stream"
  }
}
```

幂等范围是 (session_device_id, request_id)。同 ID 同长度/摘要返回同一个任务，不再次生成对象或预留预算；不同参数返回冲突。

凭据只存哈希，因此重试命中旧任务时不承诺返回旧 token 原文：返回既有 ID/状态，若仍 authorized 可提示 requires_renew=true。已 receiving/verifying/completed 不重新发可启动接收的凭据。任务失败/过期后需新的创建 request_id，新对象不覆盖旧临时文件。

错误使用对应 type.result、request_id、success=false、error:{code,message}；具体 code 在实现时集中定义。至少区分非法输入、身份/设备失败、请求参数冲突、大小/并发超限、任务过期、状态不允许及服务繁忙。reply 不整体记录 Token、文件密钥或文件名。

## 4. status / renew / cancel

每次控制操作有自己的 request_id 做响应关联。status 在已知 upload_id 时查它；许可响应丢失而不知道 upload_id 时，通过 upload_request_id（最初创建 request_id）和当前设备查找。两者同时提供必须指同一任务。

```json
{
  "type": "file.upload.status",
  "request_id": "本次查询UUID",
  "authorization": "Bearer <AccessToken>",
  "upload_request_id": "最初创建请求UUID"
}
```

file.upload.status.result 返回 upload_id、attachment_id、upload_state、attachment_state、expires_at；进度可选且不代表可续传。只有 upload_state=completed 且 attachment_state=ready 才允许开始提交新附件消息。

- file.upload.renew：携带自己的 request_id、upload_id 与 authorization；仅 authorized/有效任务可轮换 token 哈希和 credential_version。与接收认领同一行串行化，旧 token 失效。不延长原截止时间，限制轮换频率；响应再次丢失可在状态允许时再申请。首次响应晚到时客户端按 credential_version 处理，不能覆盖更新凭据。
- file.upload.cancel：携带 request_id、upload_id 与 authorization；authorized/receiving/verifying 可取消，状态持久化后停止接收、协调在途写入再清理。completed 不转 cancelled，返回既有完成状态或明确冲突，不通过取消任务删除已发送附件。取消重试幂等。
- status/renew/cancel 都验证 owner/device；不能用任务 ID 枚举他人的文件。WebSocket 正常断开不是 cancel。

## 5. HTTP 上传和 ready 提示

PUT /upload_file 使用许可返回地址，携带 X-Upload-ID、X-Upload-Token、Content-Length 和要求的头；正常身份头按既有认证规则提供。不要发送 multipart 或把整个文件转成 WebSocket 消息。

接收入口核对 Content-Length=批准大小，只允许 authorized→receiving 原子认领。Token 和版本是许可能力，不等同用户 Access Token，两者不能混用或输出日志。只在开始 HTTP 请求时验证成功，也不能免除发布前设备/取消/到期复查。

HTTP 可以返回 202 verifying，iOS 不应据此发送信封。文件 ready 后，服务端可给当前同一用户/设备会话提示：

```json
{
  "type": "file.upload.ready",
  "upload_id": "任务UUID",
  "attachment_id": "附件UUID",
  "upload_state": "completed",
  "attachment_state": "ready"
}
```

这是尽力而为的事件，可能丢失、重复或先于 HTTP 响应；客户端按 upload_id 幂等处理。数据库状态才权威，上传完成不要求 App 始终在线。完成提示不携带文件密钥，也不是聊天消息推送。未收到提示可查询 status。

## 6. 图片/文件信封与时间

文件 ready 后，iOS 构造并加密类似下面的正文（示意结构，算法字段须与选定文件协议匹配）：

```json
{
  "version": 1,
  "message_id": "逻辑消息UUID",
  "sent_at": "创建消息时固定的时间",
  "type": "image",
  "attachment": {
    "id": "附件UUID",
    "file_key": "文件密钥",
    "encryption_parameters": {}
  }
}
```

完整正文经各目标设备会话分别加密成 packet，继续使用现有 chat.newTextMessage、Online_Sync、chat.sync.envelope、chat.envelopes.ack。当前 type 名称含 Text 也不要求先重构整个协议；实现时给外层加 attachment_ids 并定义兼容默认 []。

server 保存的是信封和外层附件关联，不是明文上述 JSON；附件密钥、原始文件名/MIME 只放加密正文，iOS 验证内外 ID 一致。多设备共用同一个文件密文和文件密钥，各自信封分别加密。重试保持相同 message_id、packet 与附件集合，不能重新推进会话或重新生成密文。

聊天界面按逻辑消息显示，多个信封不变成多条消息。sent_at 来自客户端会有时钟偏差；第一版按该时间和稳定 ID 排序，不使用上传/入库/下载时间。seq 仍是设备对内序号，不是全会话全局顺序。需要严格统一顺序时另设计会话级序号。

## 7. Bob 下载与状态分离

Bob 收到信封后解密描述，先保存消息并显示占位，按设置自动下载或等待点击。附件 ID 不是通行证：HTTP 下载授权检查 active 设备和 envelope_attachments 中的接收授权；头像走独立普通读取接口。

建议 POST /request_file_download 提交 attachment_id 与当前设备身份材料，返回自有 HTTP 下载地址和短期授权，实际下载入口再次校验。原始文件字节不走 WebSocket，使用有界 HTTP 流式读取。当前版本不做 Range 或字节断点续传。

本地分别记录“信封已保存”“附件已下载”“认证解密成功”。信封可靠保存后即可 ACK，不等待附件下载；失败重试文件下载，不要求发送方重发消息。首次下载前附件已到期则保留消息并显示附件过期。

## 8. 第一版与后置功能

- 当前先做 Linux 本地磁盘，不以云对象存储和 Apple 会员为前置。
- 头像独立 HTTP、独立表、active/revoked 与 revoked_at，维护线程延迟清理。
- 聊天附件保留 file_uploads 与 envelope_attachments；后者承担引用和设备下载授权，不能在无替代权限机制时删掉。
- 用户累计空间统计可以后置；不启用时 reservation 为 0，但单文件、每用户未完成任务、全局连接/缓冲/磁盘限制与清理不能省略。
- APNs 后置；未来显示通用系统提醒，消息和文件仍按上述机制恢复。
- 本协议没有实现或联调；开发需同步 Dispatcher、Service、Repository、迁移、iOS 任务状态和回归测试。
