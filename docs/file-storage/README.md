# Linux 文件存储：普通头像与加密附件

日期：2026-11-03。状态：设计草案，未实现、未迁移、未注册新接口。

## 设计结论

**头像走普通 HTTP 上传保存，不进入 file_uploads，也不走附件 UploadReactor。** 聊天文件采用“WebSocket 申请许可 → 独立 HTTP 上传 → Reactor/磁盘池 → 校验发布 → WebSocket 提交信封 → 在线推送/离线拉取 → HTTP 下载”的流程。

两条业务仅共用底层磁盘 I/O 线程池和 FileStorage，不共用上传业务、上传凭据、状态机、保留期或附件配额。

| 项目 | 头像 | 聊天图片/文件 |
|---|---|---|
| 入口 | 普通 HTTP POST /upload_avatar | WebSocket file.upload.request 申请，HTTP PUT /upload_file 传密文 |
| 数据内容 | 普通图片，不做 E2EE | iOS 预先加密的文件 |
| 接收 | 有严格请求体上限的现有 HTTP 路径 | UploadReactor 非阻塞分块接收 |
| 业务记录 | avatars 最终记录 + user_avatars 当前引用 | attachment + file_uploads + envelope_attachments |
| 上传任务 | 无，不创建 upload_id/token，不轮询进度 | 有，凭据、期限、幂等、重连状态查询；累计容量配额可后置 |
| 保存结果 | 同一次 HTTP 请求完成并绑定头像，返回结果 | 文件 ready 后，再通过信封发送 |
| 内容处理 | 校验、缩放、去元数据、重编码 JPEG | 不解密、不解压、不做服务端图片处理 |
| 资源限制 | 请求体、像素、处理内存、并发/频率、临时区全局上限 | 有界网络/写入队列、大小/频率/活动任务上限、期限；可选用户累计额度 |
| 加密 | HTTPS 传输，无头像端到端加密 | HTTPS + 客户端文件加密，密钥只在聊天密文里 |

## 阅读顺序

1. [schema.md](schema.md)：独立头像表、附件表、附件上传任务及引用。
2. [flows.md](flows.md)：头像单请求流程、附件完整流程、磁盘协作与恢复。
3. [attachment-protocol.md](attachment-protocol.md)：WebSocket 许可、状态、续签、取消与 HTTP 上传下载，及 iOS 顺序。
4. [proposed-schema.sql](proposed-schema.sql)：5 张核心表草案；[optional-quota.sql](optional-quota.sql) 是后置累计容量表。均不是旧库升级脚本。

## 已读取的现有设计和实现

- [database.md](../database.md) 的附件表、设备信封、文件加密段落：早期 attachment 按 app.message 引用。
- [prepare-envelope-message-storage.sql](../prepare-envelope-message-storage.sql)：当前 message_id 是客户端重试 ID。
- NewMessage/Envelope、EnvelopeRepository：逐目标设备存储，幂等键为 sender/recipient/message_id；ChatService 将 packet 限为 64 KiB。
- HttpParse/HttpRequest：当前请求产生完整字符串 body。普通头像可以采用此路径，但必须在收完正文前执行严格上限，并限制并发；附件新增流式交接。
- WebsocketWorker 的 epoll、任务队列和 eventfd：仅为附件 Reactor 借鉴模式，头像无需接入。
- [002_login_tickets.sql](../../database/migrations/002_login_tickets.sql)：设备/用户组合唯一索引供附件上传外键使用。

## 目录与命名

```text
FILE_STORAGE_ROOT=/srv/chat-files

staging/avatars/<request_id>.part                 单次头像请求原图
staging/avatars/<request_id>.processed            重编码结果
staging/attachments/<前2位>/<次2位>/<upload_id>.part
objects/avatars/<前2位>/<次2位>/<avatar_id>.jpg
objects/attachments/<前2位>/<次2位>/<attachment_id>.bin
```

dev 环境设置 `FILE_STORAGE_ROOT=/home/developer/.local/share/cpp-backend-dev/files`。头像在 `objects/avatars`，聊天密文在 `objects/attachments`。暂存目录按 `upload_id` 分片，正式目录按 `attachment_id` 分片。原始文件名由客户端放进加密 envelope 的正文，接收端解密后用于显示。

头像 request_id 只是服务器内部临时文件/日志标识，不是数据库上传任务、不发上传凭据。所有 ID 和 key 都由服务器生成；不使用原始文件名拼路径。数据库只存相对正式 key，不存绝对路径或永久下载 URL。暂存与正式目录位于同一文件系统，发布采用不覆盖语义。FileStorage 应限制根目录、拒绝符号链接和路径穿越。

## 表与业务边界

- avatars：保存成功处理的头像元数据，增加 active/revoked 状态与 revoked_at；无上传状态机、文件密钥、上传设备、sha256、宽高和任务外键。
- user_avatars：当前用户头像引用，与 avatars 在上传成功的同一事务创建/更新。
- attachment：客户端密文文件对象及生命周期。
- file_uploads：**只引用 attachment**，无 avatar_id/kind；管理附件批准、凭据、额度和完成状态。
- file_storage_quotas：后置可选，仅统计聊天附件；不启用时 file_uploads.reserved_bytes=0，以并发/大小/临时容量限制先保证资源有界。头像不走这套额度业务。
- envelope_attachments：只连接附件与实际设备信封。

不设置单独 set_avatar 两阶段流程：头像单次请求完成保存和绑定，替换/移除时在同一事务撤销旧头像并记录 revoked_at。当前头像不受聊天附件保留期影响；好友只读取当前且 active 的头像。撤销时间不是物理删除时间，旧头像按撤销宽限期清理。附件仍按设备信封授权，不能把同账号新设备自动授权为历史接收者。

## 最终协议与后台维护

- 上传许可通过 WebSocket，不由 WebSocket 传文件字节。目标用户/设备来自会话，业务池处理数据库，结果回投 Worker。
- file_uploads 是持久化任务，不依赖原 socket；正常断线后已授权 HTTP 上传仍可继续，重连查询恢复。ready 提示不能代替数据库状态。
- 文件 ready 后发送现有 chat.newTextMessage 信封：packet 加密的是附件 ID、密钥、参数和消息时间；完整密文文件留在磁盘。
- Bob 在线接收/离线拉取的是同一信封，另走 HTTP 下载密文、本地解密。下载失败不重发聊天消息，ACK 不等待文件下载。
- 头像与附件共用一个维护调度器/磁盘池，但清理规则分别处理。旧头像逻辑撤销即停止授权读取，维护线程延迟删除，不在头像请求里等待 unlink。
- 图片/文件会话内展示使用逻辑 message_id/sent_at；设备对 seq 不当全会话序号。
- APNs 因会员成本暂缓，先做普通头像、加密图片和文件；不以 APNs 为附件前置。

## 迁移与实施

旧稿的“头像和附件共用 file_uploads”已取消。若已有混合任务/头像状态数据，需要单独制定迁移，不能直接删除行或执行本草案 CREATE。原 database.md 中的逻辑消息附件外键也不能直接当作当前信封模型。

建议先做 FileStorage、磁盘执行器和普通头像请求，再实现 WebSocket 附件许可、Reactor、任务和消息授权；用户累计容量配额后置。文件/图片大小、时间、像素和并发上限是部署配置，讨论中的示例数字不等于已批准规则。

本次仅修改文档和 DDL 草案，没有修改业务代码或操作实际数据库。
