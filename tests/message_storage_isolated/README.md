# 新消息存储链路隔离测试

## 同账号设备与会话隔离回归

以下两个独立入口针对本次改动，不依赖旧 test.cpp 的 Dispatcher 调用方式。
尚未在当前环境编译执行：WSL 返回 E_ACCESSDENIED。请在 Linux/WSL 的项目根目录运行：

```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic \
  -I tests/message_storage_isolated/fakes -I include \
  src/service/ChatService.cpp src/repository/EnvelopeRepository.cpp \
  src/databases/DatabaseError.cpp \
  tests/message_storage_isolated/own_account_test.cpp -pthread -o /tmp/own_account_test
/tmp/own_account_test

c++ -std=c++20 -Wall -Wextra -Wpedantic \
  -I tests/message_storage_isolated/fakes -I include \
  src/websocket/WebSocketDispatcher.cpp src/service/ChatService.cpp \
  src/repository/EnvelopeRepository.cpp src/databases/DatabaseError.cpp \
  tests/message_storage_isolated/pool_fake.cpp \
  tests/message_storage_isolated/session_device_test.cpp -pthread -o /tmp/session_device_test
/tmp/session_device_test
```

第一项验证：同账号无好友关系时可发送、重试幂等、密文冲突、不能给自己投递、设备不可用/发送者归属不符时拒绝、陌生人拒绝、好友路径兼容。
第二项验证：发送、同步、ACK 的设备必须等于会话设备；缺失、非字符串、非法 UUID 拒绝；会话设备非 active 时拒绝。不调用数据库。
模拟依赖不验证真实 SQL、锁、设备状态或密码学；不能替代下列真实集成验收。

### 真实集成验收（使用测试账号/测试设备）

1. 准备同账号 A1/A2、好友 B1、陌生人 C1，以及 pending/revoked 设备。
2. POST /user_devices + {}：仅返回本人 active 设备，顶层和逐项 is_own_device=true，可能包含 A1。
3. 显式 target_user_id=A、旧 friend_id=A 与默认查询一致；B 返回 false；C 拒绝；两个目标字段冲突返回 400。
4. A2 的 bundle、一次性预密钥可领取；pending/revoked/设备与目标用户不符拒绝。预密钥测试会消耗库存，需准备专用测试密钥。
5. A1 WebSocket 发送 A1→A2 信封：在线 A2 收到 Online_Sync；A2 离线时存储成功，上线后 sync 拉到同一 envelope_id。
6. A1 的 sync 不应包含发给 A2 的信封；A2 ACK 后 received_at 被记录，重复 ACK 幂等。
7. A1 连接伪造 sender_device_id=A2，或 sync/ACK device_id=A2，均返回 session_device_mismatch。
8. A1→A1 拒绝；A1→陌生人拒绝；A1→B1 仍可用；重复相同 envelope 返回 duplicate、不多存一条。
9. 撤销 A2 后列表不再返回它、密钥不可获取、新投递拒绝、原连接关闭；另行验证并发撤销与投递的事务边界。

## 历史全链路测试

下面的 test.cpp 仍使用旧版 Dispatcher 构造/dispatch 签名，尚需单独迁移后才能运行；不能把它当作本次已通过的测试。

实际编译 Dispatcher、ChatService、EnvelopeRepository、DatabaseError。数据库事务、UUID解析、Token校验、Base64解码和线程池执行由 fakes 替代。本测试不验证 PostgreSQL SQL语法、真实锁竞争、libsodium或JWT正确性。

在项目根目录、已安装 nlohmann-json 的 C++20 环境运行：

```bash
c++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  -I tests/message_storage_isolated/fakes -I include \
  src/websocket/WebSocketDispatcher.cpp src/service/ChatService.cpp \
  src/repository/EnvelopeRepository.cpp src/databases/DatabaseError.cpp \
  tests/message_storage_isolated/pool_fake.cpp \
  tests/message_storage_isolated/test.cpp -pthread -o /tmp/message_storage_test
/tmp/message_storage_test
```

覆盖重复请求、内容冲突、反向设备对序号、不同设备对、权限拒绝、内部输入校验、事务异常与有界重试、提交失败回滚、JSON格式、错误映射和入队失败。真实数据库并发验收还需另行运行。
