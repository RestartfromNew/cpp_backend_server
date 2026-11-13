# 普通 HTTP 头像与加密附件上传流程

日期：2026-11-03。本文是建议设计，新接口未实现。配套 [表结构](schema.md) 与 [DDL 草案](proposed-schema.sql)。

## 1. 两条业务入口

```text
头像：
iOS 裁剪并编码为 512×512 JPEG → 普通 HTTP /upload_avatar → AvatarHandler → AvatarService
                                                  ├─ 校验 JPEG 格式、尺寸和完整性
                                                  ├─ 共用磁盘 I/O 池 → FileStorage
                                                  └─ AvatarRepository → avatars/user_avatars

聊天附件：
iOS → WebSocket file.upload.request → Dispatcher → 业务池 UploadService → attachment/file_uploads
iOS → /upload_file → UploadReactor → 有界块/写入队列
                                           ↓
                                  共用磁盘 I/O 池 → FileStorage
                                           ↓
                                  完成通知 → 校验发布
```

头像不走 UploadReactor、不创建 file_uploads、不申请短期上传 token、不轮询任务、不做端到端加密。底层线程池和安全磁盘操作是服务器执行设施，不等于共用业务流程。

## 2. 头像：一个普通 HTTP 请求完成

建议接口：

```http
POST /upload_avatar
Authorization: Bearer <AccessToken>
Content-Type: image/jpeg
Content-Length: 处理后的 JPEG 字节数

<512×512 JPEG 二进制>
```

第一版直接接收 JPEG 字节。iOS 负责校正方向、裁剪成正方形、缩放至 512×512、处理透明背景、清理原图的 EXIF/GPS 等元数据，并压缩到 256 KiB 以内。用户身份来自现有认证，活动设备要求沿用现有认证策略。

步骤：

1. HTTP 接收阶段对头像路由执行 256 KiB 正文上限，检查声明长度和实际接收量。拒绝重复或冲突的长度、非预期的内容编码和传输方式。代理与后端采用同一上限，并设置接收超时和并发限制。
2. AvatarService 校验账号和实际正文长度，空正文返回 400，超限返回 413。检查 Content-Type 和图片库识别的实际格式，两者都应为 JPEG，格式不符返回 415。
3. 图片库先读取头部尺寸，确认宽和高都等于 512，再完整解码验证数据。尺寸不符返回 422，图片截断或损坏返回 400。如果产品要求所有头像都去除敏感元数据，服务端应检查并拒绝仍携带禁止字段的输入。校验通过后保留客户端提交的 JPEG 字节。
4. 将通过校验的 JPEG 缓冲交给磁盘任务。在 staging/avatars 使用内部 request_id 独占创建临时文件，写入并按持久性要求同步，再以不覆盖方式发布为 objects/avatars/<前2位>/<次2位>/<avatar_id>.jpg。数据库 size_bytes 记录实际保存的 JPEG 字节数，content_type 固定为 image/jpeg。
5. 等磁盘任务明确完成后，短事务锁用户行、复查账号和当前认证资格，插入最终 app.avatars 元数据（status=active、revoked_at=NULL），原子替换 user_avatars.avatar_id，并将旧头像改为 revoked、写入首次 revoked_at。此时才保存新头像数据库记录；不建 uploading/verifying 行。
6. 提交成功返回 200，包含新的 avatar.id 与 size_bytes。没有单独 /set_avatar，客户端无需再发绑定请求。旧头像在新引用提交成功后安排清理。
7. 任一校验/保存/数据库失败，保留原头像。数据库失败且确认未提交时删除本次新文件；提交结果不确定时先查询，不能误删已绑定对象。

```json
{
  "avatar": {
    "id": "新头像UUID",
    "size_bytes": 80231
  }
}
```

**线程行为明确：** 这是普通同步业务响应。共享磁盘池避免 HTTP worker 自己执行磁盘 I/O，但等待结果期间 HTTP worker 可能仍被占用；不宣称整条头像请求零线程等待。可以接受小体积、低频头像的有界等待，不在网络 Reactor 内调用 future.get。队列满直接返回繁忙，不无限等。HTTP 超时后协调任务结果和补偿，不能让迟到任务不受控地切换头像。

图片解码仅用于校验，由普通 HTTP worker 受限执行。磁盘线程负责保存通过校验的 JPEG；异步任务持有自己的正文缓冲，避免引用已结束请求的内存。

## 3. 头像读取、替换和简单清理

- 用户资料 JOIN user_avatars 返回 avatar:{id,updated_at}，无头像 null。客户端按新 ID 更新缓存。
- 建议受保护普通读取接口 POST /fetch_avatar，body 为目标 user_id（默认本人）；服务器只返回当前且 status=active 的头像，验证本人/当前好友关系与账号状态，输出 Content-Type:image/jpeg。无需上传任务查询或额外头像下载票据。
- POST /remove_avatar 在锁用户的短事务将当前头像 status 改为 revoked、首次写 revoked_at，再删除当前引用并返回。重复移除/撤销幂等，不刷新旧撤销时间；随后按宽限期清理。不允许客户端重新绑定任意旧 avatar_id，因此撤销头像不会再因旧请求恢复为当前头像。
- 每次替换新建 key，两个并发请求以短事务提交顺序决定当前头像。前一个响应晚到时 iOS 应避免用旧结果覆盖新资料，可重新查询当前头像或以请求代次过滤回调。
- 清理筛选 status=revoked 且 revoked_at 超过宽限期的头像，重新确认无 user_avatars 引用，unlink 成功后删旧 avatars 行；失败保留元数据以便重试。revoked_at 只表示逻辑撤销，不代表已经删除磁盘文件。读取只授权当前 active 引用，安全打开 fd 后再传输，清理与读取需协调；已打开/下载的内容不能追回。
- 请求进行中的 staging 路径受内部任务注册表保护；崩溃后用宽限扫描清理。正式目录中“有文件无记录”可能是未提交请求，需等待宽限、确认无在途任务后清理，不能立即递归删除。
- avatars 行与当前引用在同一事务创建/切换，扫描看不到仅插入但尚未绑定的已提交中间态。当前设计不接受将既有孤立 avatar 行重新绑定。
- 头像无附件 expires_at 或 quota 结算。严格限制旧对象积压和全局临时容量，不能以“不计附件 quota”为由无限保留。
- 普通头像不 E2EE，服务器可读内容，但传输仍使用 HTTPS，磁盘目录私有；不公开服务器路径。

## 4. 聊天附件通过 WebSocket 申请许可

**最终选择 WebSocket 控制上传、HTTP 传输文件字节。** 不再把 POST /request_file_upload 作为第一版附件许可入口。头像仍只走第 2 节普通 HTTP。

建议新增 file.upload.request / status / renew / cancel 和完成提示 file.upload.ready；完整字段、错误与断线规则见 [附件协议](attachment-protocol.md)。这些 type 尚未实现。复用当前报文 authorization 字段和会话身份校验，用户/设备取已认证 Session，不由请求任意指定。

```json
{
  "type": "file.upload.request",
  "request_id": "本次创建请求UUID",
  "authorization": "Bearer <AccessToken>",
  "size_bytes": 182430
}
```

1. WebSocket Worker 只解析/分派，把可能查库的许可业务放到有界业务线程池。校验 Token、Session user/device 一致、设备/账号 active、大小/频率与未完成上传上限。
2. request_id 映射到 file_uploads.client_request_id，幂等范围为上传设备。先查原任务再决定是否创建；同参数重试不重复创建或占额，大小/摘要变化返回冲突。
3. 短事务创建 attachment(uploading) 和唯一 file_uploads(authorized)，生成 upload_id 与高熵 token，仅存哈希。第一版可以不启用用户累计容量表，用单文件、活动上传、暂存空间与任务到期限制；启用完整 quota 后才在此事务预留额度。
4. 提交后回投原 Worker，核对 Session 代次，再返回 file.upload.request.result：request_id、attachment_id、upload_id、HTTP 上传地址、凭据、截止时间。回复丢失不回滚已创建任务。
5. 尚处 authorized 的任务丢失 token 时，通过 status 找回任务，再以 renew 原子轮换并递增版本；receiving/verifying 不能并行重新认领。详情见附件协议。

上传任务的归属是 user/device/upload_id，不是某条 socket。**普通 WebSocket 断开不会取消已批准的 HTTP 上传。** HTTP 上传入口独立验证认证身份、任务凭据、期限和活动设备；不要求原 WebSocket 在线。设备撤销、主动取消或任务到期才使任务失效。

## 5. 聊天附件 Reactor 与磁盘池

PUT /upload_file 带正常身份、X-Upload-ID、X-Upload-Token、Content-Length，正文为 iOS 已加密的字节。只支持明确长度的原始正文，不支持首版 multipart、压缩或 chunked；解析器/代理一致拒绝歧义请求头。

- 请求头有界读取，资格查询与文件创建交给有界执行器，不能在 Reactor 查库或阻塞磁盘。authorized→receiving 条件更新仅一个连接成功。
- 临时文件在 `staging/attachments/<upload_id 前两位>/<接下来两位>/<upload_id>.part` 独占创建。头部已读取的正文前缀先交给同一计数管线。
- 非阻塞 recv → 有界块 → pwrite；单文件一个在途写入，任务持有 RAII 文件/块，不拿可能复用的裸 fd。部分写入/EINTR 循环处理。
- 写入队列满就暂停对应 EPOLLIN，完成消息经 eventfd 回投所属 Reactor 后公平恢复；没有数据的连接不占专属线程。
- 检查实际累计大小、提前 EOF、无进展和总期限；磁盘 ENOSPC/EIO 失败。正文按 Content-Length 边界处理，关闭连接，不把额外字节发布到文件或当作流水线请求。
- 等最后一块写完再 verifying，增量摘要与可选预期摘要比较。服务器不解密、不解压、不作图片重编码。
- HTTP 接收成功可返回 202 verifying；它不等于 ready。持久化完成后可经 WebSocket 提示 file.upload.ready；断线重连后用 file.upload.status 查询权威状态，只有 ready 才能提交引用它的消息。

网络接收、内存块、队列、并发、任务期限和磁盘低水位均有上限。关闭先停接入，再协调在途写入并排空完成事件，最后销毁 Reactor；取消不能提前关闭磁盘任务仍持有的 fd。

## 6. 附件发布与消息授权

1. 短事务保存待发布密文的 size/sha256，状态仍 verifying；任务发布前后核对 upload_id、状态、设备与期限。
2. 事务外同步文件，以同文件系统、不覆盖的发布方式写入最终 key。若目标存在需核对同一对象元数据，不盲目覆盖；仅 write 返回不等于掉电持久，目录同步按持久性承诺处理。
3. 清理临时残留，短事务确认资格、将 attachment ready、file_uploads completed 一起提交；启用 quota 时同事务结算 reserved→used，失败残留未清理前不释放预算。未启用时 reserved_bytes=0，不操作 quota 表。
4. iOS 将 attachment_id、原文件名/类型、文件密钥/认证参数放入设备加密正文；多设备分别加密描述，文件密文上传一次。
5. 发送外层增加 attachment_ids（默认 []、有个数上限），在现有 saveNewMessage 同事务校验附件 owner/ready/期限并保存 envelope_attachments；第一次绑定设置保留期，后续不延长。
6. 幂等同时比较 packet/protocol 和排序后的 attachment_ids；相同返回原信封，不追加授权，不同冲突。过期后的原请求重试也可返回原存储结果。
7. 在线推送/离线拉取同步返回关联 ID，接收端解密后核对；服务器不能证明密文描述与外层一致，外层关系会泄露部分元数据。
8. 附件下载仍用请求授权与流式下载：当前 active 设备是信封 recipient，或符合上传者访问规则。仅同账号不自动授权历史，新好友状态变化不撤回已投递引用，到期拒绝。

头像不参与上述步骤，也不能出现在 envelope_attachments。头像读取在第 3 节。

### iOS 消息展示与文件下载

- WebSocket/离线同步传的是图片或文件的消息信封，**不传完整文件密文**。外层 attachment_ids 用于权限关联，文件密钥只在 packet 的加密正文中。
- Bob 解密后取得 attachment_id 和文件密钥，聊天列表先保存消息/显示占位。按偏好自动下载或用户点击后，经独立 HTTP 下载入口取得密文，再本地认证解密展示；失败只重试下载，不让 Alice 重发消息。
- 消息按客户端逻辑消息展示，多个设备信封共享稳定 message_id。sent_at 放加密正文，创建后重试不变；不按上传时间或文件创建时间排序。客户端时钟可偏移，需稳定平局规则。
- 当前 seq 是设备对内序号，用于既有同步；不是整个会话的全局序号。信封按 envelope_id 去重，逻辑消息可按 (sender_user_id,message_id) 去重。
- ACK 仍表示信封已可靠保存，不能等整张图片下载/解密才 ACK。图片下载、解密状态由客户端独立记录。
- APNs 暂缓；未来只负责系统提醒，点击后沿用信封同步和 HTTP 下载，不把图片或文件密钥放进通知载荷。

## 7. 维护线程与清理职责

头像替换请求只提交新引用和旧头像撤销，不同步删除旧文件。一个维护线程定期、分批扫描 revoked_at 超过配置宽限期、无当前引用的头像；通过有界“正在清理 ID 集合”避免重复入队，提交磁盘池执行 unlink，再由完成处理提交元数据删除。

维护线程不持数据库 lease 等磁盘操作，网络 Reactor 不做目录扫描/删除。文件已不存在可视为幂等删除成功；其他错误保留 revoked 记录，下次重试，重复撤销不修改首次时间。重启从数据库重扫，清理前重查状态/引用，撤销时间不是删除时间。单进程可先用内存认领集合，多进程需持久化 lease 或其他跨进程认领机制。

同一维护执行器可分类型调度附件过期、失败上传、孤儿文件核查，但不共用两类业务状态；限制每轮数量和磁盘队列份额，避免影响在线上传。额度表未启用时不减账；启用时成功物理清理后再结算。

## 8. 清理、恢复与验证

| 场景 | 处理 |
|---|---|
| 头像校验失败 | 直接 HTTP 错误，旧头像不变，清理本次临时数据 |
| 头像文件已保存，数据库未提交 | 不开放读取；补偿删除或宽限扫描 |
| 头像事务提交后响应丢失 | 查询当前头像；原设计不承诺请求幂等，重新上传会产生新对象，旧对象随后清理 |
| 旧头像清理失败 | 保留 revoked 状态和首次 revoked_at，后续重试；不访问 file_uploads 或扣附件 quota |
| WebSocket 许可响应丢失 | 用原创建 request_id 定位已有任务；authorized 可 renew，不重复创建 |
| WebSocket 在上传中断开 | HTTP 继续，完成状态持久化；重连查询，不依赖 ready 通知必达 |
| 附件 authorized 到期 | 失效凭据，确认无任务后清理释放预留 |
| 附件 receiving 崩溃/超时 | 首版不续传，停止任务后清理，用户重新申请 |
| 附件 verifying 已有最终文件 | 核对持久化 size/hash 与资格，再幂等完成，否则清理 |
| ready 附件未引用/到期 | 对象行锁与绑定协调，先标 deleting，再删文件、结算 |
| 删除文件成功、DB 未结算 | 重试确认文件不存在，再条件结算，防止重复减账 |

附件清理锁 attachment 后检查 envelope_attachments；到期可保留信封引用并显示文件过期。未引用文件清理不能误删刚绑定对象。所有磁盘恢复操作限制在受控根目录，不能用启动时删除整个 staging 替代恢复。

必验：普通头像一次请求完成且无 file_uploads 行；非法图片/超大像素/输出超限；并发替换；HTTP 超时后的任务收尾；磁盘队列公平；头像 DB 回滚不丢旧头像；附件申请幂等、部分写入、慢连接、fd 复用、取消/撤销、越权下载、过期和各崩溃窗口。还需验证 WebSocket 响应关联、会话代次、许可幂等、断线后继续上传、renew/认领竞争、ready 通知丢失恢复，以及消息已 ACK 但附件未下载。真实 SQL/性能/图片库测试尚未执行。
