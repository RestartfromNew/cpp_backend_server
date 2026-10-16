# 进度统计

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
