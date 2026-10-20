# 注释与日志使用说明

本次只在已有源码中增加注释、include、日志调用和作用域计时对象；原有语句、接口、SQL、路由、返回结构、线程/事务控制及旧输出均保留。没有更改端口、部署配置或数据库结构。新增日志不承担授权判断，不会重试或修改业务结果。

## 日志设施

[Logger.h](../include/common/Logger.h) 是独立的 C++20 头文件，不新增第三方依赖，也不需要调整已有 CMake 源文件清单。Logger 使用 `std::osyncstream` 向标准错误输出一行 JSON；同一进程中经 Logger 输出的记录不会彼此交叉。原有 `cout/cerr` 未经过 Logger，可能与 JSON 日志混合，不能假定 journal 的每一行都是 JSON。

默认级别为 INFO。进程首次使用时读取 `LOG_LEVEL`，更改后须重启：

| LOG_LEVEL | 新增日志范围 |
|---|---|
| DEBUG / debug | 包含调用耗时、业务结果、帧大小、在线投递等高频诊断 |
| INFO / info 或未设置 | 请求路由/HTTP 状态、登录完成状态、启动停止、连接建立清理及警告错误 |
| WARN / warn | 身份拒绝、连接池等待超时、事务失败等警告与错误 |
| ERROR / error | 数据库错误、未处理异常等错误 |
| OFF / off | 关闭新增日志；不会关闭或过滤原有 cout/cerr |

未识别的值按 INFO 处理。不要在正常高并发运行中长期启用 DEBUG。输出是同步的，stderr/journal 变慢会增加调用耗时，尤其不要在压测时把 DEBUG 的 I/O 开销当作业务计算耗时。本次没有增加异步日志队列、远程采集、文件轮转或限流。

```bash
# 在原有启动环境下直接运行；其余既有环境变量保持原配置。
LOG_LEVEL=INFO ./cpp_backend_server
LOG_LEVEL=DEBUG ./cpp_backend_server

# 已由 systemd 启动时查看日志；实际级别需在该服务环境中配置后重启生效。
sudo journalctl -u backend-server -n 100 --no-pager -o cat
sudo journalctl -u backend-server -f -o cat
```

Logger 的输出格式：

```json
{"timestamp":"2026-10-20T19:00:00.000Z","level":"INFO","thread_id":"1234","component":"websocket","event":"session_registered","fields":{"worker_id":0,"fd":25,"session_id":1}}
```

时间为 UTC。`component/event` 为固定操作名；thread_id 是进程内线程标识，不能解释为请求 ID。`operation_id` 仅关联 DEBUG 作用域的 begin/end，非跨层、跨线程或跨服务追踪 ID。WebSocket 生命周期使用 worker_id/fd/session_id 联合排查，不能仅按 fd 判断某个连接。

## 覆盖范围和事件含义

| 层 | 新增观察点 |
|---|---|
| 进程/TCP | process_start、listener_started、ready、shutdown_complete、process_failed |
| HTTP/Router | 已注册路由名（不会打印原始 URL/query）、HTTP 状态、解析拒绝、边界异常 |
| Auth/Service | 无凭据内容的调用计时、ticket_issued、device_completion_result、业务 Task 入队拒绝/异常 |
| Repository/数据库 | 调用计时、借连接超时、SQLSTATE、回滚失败；不打印 SQL、参数和连接串 |
| Dispatcher | Session 拒绝原因、发送业务状态、同步/ACK 数量（DEBUG） |
| WebSocket | 握手拒绝、Session 注册/清理、帧长度、跨 Worker 投递、目标输出缓冲入队 |
| Redis | get/set/exists/remove 的 DEBUG 调用计时；不记录 key/value |

`scope_begin/scope_end` 记录函数耗时（elapsed_us）。`outcome=returned` 仅表示没有异常逃出作用域，**不代表业务成功、事务提交、消息到达或已读**。业务拒绝也可能正常返回；须结合 HTTP status 或专用 result 事件判断。异常在内部捕获时，外层计时也可能显示 returned。异常逃出作用域会发 ERROR；多个调用层可能都记录同一异常，不应按日志行数统计独立故障数。

device_completion_result 的 status 是 [LoginTicketStatus](../include/domain/LoginTicket.h) 数值；send_result/store_result 分别使用 [NewTextMessageStatus](../include/service/ChatService.h) / [NewMessageStatus](../include/repository/EnvelopeRepository.h)。这些诊断值不是新的 API 错误码。

`push_enqueued` 表示已调用目标 Worker 入队接口，`push_buffered` 表示已排进目标 Session 输出缓冲；它们都不表示客户端已接收。消息确认仍以业务 ACK 和数据库状态为准，离线依靠同步恢复。结构化 listener_started 中的 port 来自监听对象；原有启动输出含硬编码端口文本，本次未改动。

## 凭据与输出边界

新增调用只传固定事件、路由登记值、状态码、SQLSTATE、计数和连接标识。不得传入密码、JWT、Refresh Token、login_ticket、哈希、连接串、密钥、消息正文、完整 request/response、JSON dump 或 exception.what()。敏感字段名不在 Field 允许列表中，会输出 `[REDACTED]`；但允许字段的字符串值不会自动识别 Token，调用方仍必须选择安全数据。

字符串长度限制为 192 字节并转义控制字符，非 ASCII 字节按字节转义；当前新增元数据使用 ASCII，不用于展示用户文本。Logger 对异常、格式化和输出失败采用 best effort，吞掉自身异常并保留 errno，不覆盖业务异常，也不提供审计日志必达保证。

**保留的旧日志仍有风险**：[WebsocketWorker.cpp](../src/websocket/WebsocketWorker.cpp) 的 handleReadable 原有 `applicationMessage.dump()` 会输出完整业务 JSON，可能含 authorization；其他原有 `error.what()` 也可能含数据库诊断。已在相应源码补充说明，但遵照“不动已有代码”没有删除或替换。因此本次不能宣称整个进程的历史输出已经脱敏，LOG_LEVEL=OFF 也不能消除这些旧输出。

## 类与线程约定

公开类和关键数据结构上新增了中文职责说明，重点包括：

- RAII 所有权：UniqueFd、Connection、DatabaseConnection、DatabaseResult、ConnectionLease；借用的 string_view 不得超过所属对象生命周期。
- 分层返回：Repository 业务状态和数据库异常、Service 重试/状态映射、Handler/Dispatcher 的 HTTP/JSON 边界。
- 线程封闭：Session map 和输出缓冲只在所属 Worker 操作；跨线程 Reply、跨 Worker 推送通过队列投递。
- 原子性：ticket 消费、登记设备、刷新凭据共用事务；信封幂等识别和设备对序号共用锁；提交后再安排推送。
- 同步语义：nullopt 与空页不同，after_seq=0 是首次同步，ACK 代表接收持久化而非已读。

## 验证方法

日志测试只依赖标准库，可单独编译（在项目根目录执行）：

```bash
g++ -std=c++20 -Wall -Wextra -Werror -pthread -I include tests/logger_test.cpp -o /tmp/backend_logger_test
python3 tests/logger_output_test.py /tmp/backend_logger_test
```

测试覆盖各级别及默认回退、8 线程共 320 条完整记录、JSON 转义和长度、字段脱敏、errno 保留、作用域耗时、异常及输出流失败。通过不代表已有输出已改为 JSON。

本次环境中日志测试通过；登录 ticket 的 21 项、Dispatcher Task 的 29 项、设备绑定的 13 项、同账号多设备信封的 9 项检查均通过（共 72 项），依赖为模拟数据库/加密或网络实现。业务线程池新增日志的源文件也通过独立语法检查。完整 Linux 服务构建、真实 PostgreSQL/Redis 及端到端联调仍需在运行环境执行；这些检查不代替生产压测或部署验证。

已有源码最终通过逐行增量检查：82 个原文件仅出现新增行，原语句及原换行保留；公开类/关键结构新增 76 处职责说明，120 个关键调用增加 DEBUG 作用域计时。新增文件为 Logger.h、日志测试和本文档。此数量不表示已经覆盖所有业务分支或完成完整服务构建。
