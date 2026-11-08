# 进度统计

## 2026-11-03：头像与附件方案定稿（待实现）

本条是设计状态，替代下方 11 月 1 日排期中的 APNs 优先；不代表新增功能已运行。

- 先做普通 HTTP 头像，再加密图片/文件，APNs 因会员成本后置；暂用 Linux 磁盘，PostgreSQL 管元数据。
- 头像独立 avatars 表，status=active/revoked 与 revoked_at；单请求保存/绑定，替换时原子撤销旧头像，维护线程延迟清理。不 E2EE，不进 file_uploads，不走附件 Reactor。
- 聊天附件通过 WebSocket 申请许可，HTTP 单独上传密文，UploadReactor + 有界队列 + 共用磁盘池写临时文件；校验持久化后 ready。
- file_uploads 任务独立于原 WebSocket，断线后已授权上传可继续，重连查询状态；完成事件只提示，不代替数据库。
- packet 是加密附件描述和密钥，完整文件不走 WebSocket；复用 message_envelopes 的推送/补拉/ACK，Bob 另走 HTTP 下载解密。保留 envelope_attachments 设备授权。
- 累计用户容量表可后置，保留大小/并发/磁盘上限；头像与附件只共用磁盘设施，不共用上传业务。

入口：[最终方案](file-storage/README.md)、[协议与 iOS 流程](file-storage/attachment-protocol.md)、[新排期](feature-roadmap.md)。本次仅更新 docs/SQL 草案，未迁移或改业务代码。

## 2026-11-01：下一阶段开发排期

用户已报告 iOS 本人设备/好友设备在线推送和离线拉取成功；完整去重、崩溃恢复与撤销验收仍需记录。当前源码已增加设备登录 ticket、Refresh Token 绑定设备，以及撤销设备时撤销对应刷新凭据。此前“Refresh Token 未绑定设备”的判断已过时。

开发顺序：文字聊天回归基线 → APNs 通用系统通知 → 头像与对象存储 → 加密图片 → 加密普通文件。预计 16–29 个专注工作日，另留约 20% 联调缓冲；不是固定交付承诺。

详见 [功能路线与验收计划](feature-roadmap.md)。目前仅完成规划，APNs、头像和附件收发尚未实施。

## 2026-10-16：本人设备 HTTP 接口实测

依据用户提供的 `tests/user_devices_test.ps1` 运行输出：localhost:8082，Alice 登录成功，**87 项断言通过、0 项失败、3 项跳过**，不是 87 个请求。

- 已通过：默认/显式本人设备查询、旧入口兼容、目标字段一致性、返回的 4 台设备及 is_own_device 标记、参数错误映射、未知目标拒绝、无 Token 拒绝。
- 跳过：好友列表（缺 FriendUserId）、第一台设备的 bundle（key_bundle_unavailable）、WebSocket 设备绑定（缺 SessionDeviceId）。
- 未验收：同账号设备加密会话、预密钥领取、在线投递、离线同步、ACK 持久化、撤销竞争；不能将此次结果视为完整 E2EE 流程通过。
- 下一步：准备有效 bundle 和两台 active 测试设备，补齐好友与 WebSocket 检查，再做成功收发及离线恢复验收。

详细结果和复测命令见 [API 文档 20.1](backend-api.md#201-设备接口实测记录2026-10-16)。以下保留原阶段规划和笔记，不表示所有阶段已完成。

## 原阶段规划

阶段 0：整理工程和建立可验证基线

阶段 1：完成底层资源管理

阶段 2：实现 Connection

阶段 3：实现最小 HTTP 模型和解析

阶段 4：实现 Router 和 Handler

阶段 5：打通 User 查询的完整请求

阶段 6：加入测试和错误映射

阶段 7：加入 Redis 缓存

阶段 8：加入线程池

阶段 9：再学习 epoll/Reactor


## Connection类
职责：负责数据库的具体执行，包括连接、执行、关闭、释放
所有权：不可以复制，但是可以转移所有权
功能：1.启动连接 2. 执行sql语句 3.isAlive()  4.reset（回滚未未完成事务，清除结果集合，） 5.获得错误信息 6. 最后活动时间

## ConnectionLease类
职责：从Database Pool中借用一个Connection连接，调用连接执行sql代码，在业务代码执行完毕，或者业务范围的错误时确保归还代码到连接池

1. 从Connection Pool中借用一个连接，类型为 Unique_ptr<DatabaseConnection>
2. 析构函数调用 releaseReturnedConnection函数，
``
   "header": {
   "fin": true,
   "opcode": 1,
   "opcode_name": "text",
   "payload_length": 100
   },
   "payload_format": "json",
   "payload": {
   "type": "chat.send",
   "request_id": "req-1001",
   "data": {
   "conversation_id": "C1",
   "content": "hello"
   }
   }
   }``
