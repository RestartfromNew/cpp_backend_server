# 新消息存储链路隔离测试

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
