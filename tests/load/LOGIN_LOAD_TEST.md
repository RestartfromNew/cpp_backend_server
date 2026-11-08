# 小规模登录并发测试与折线图

脚本：[login_load_test.py](login_load_test.py)。只测试 `POST /login_by_email`，不注册账号、不验证设备、不建立 WebSocket，也不自动重试。先用已有账号探测登录成功，再逐档增加闭环并发，每个客户端收到结果后才发下一次请求。

## Windows 运行

先确认公网服务器连接的测试库里已有这些账号。在项目根目录执行：

```powershell
Set-Location D:\projects\cpp_backend_server
# 已有 .venv-load 时直接使用；没有时先执行 py -m venv .venv-load
.\.venv-load\Scripts\python.exe -m pip install -r tests\load\requirements-load.txt

.\.venv-load\Scripts\python.exe tests\load\login_load_test.py `
    --accounts D:\load-fixtures\load01\accounts.jsonl `
    --base-url https://aittelegramapi.uk `
    --concurrency 1,2,4,8 `
    --requests 60 `
    --out tests\load\results\login01
```

`--out` 必须是新目录，避免覆盖上次结果。重复运行可改为 login02。Linux 也能运行，改用 python3 和对应文件路径即可。

默认每档最多 60 次计量请求、另外 4 次预热，开始还有最多 3 次账号探测；四档最多合计 259 次登录。默认最高并发是 8，可用 `--concurrency` 指定 1..64 范围内严格递增且不重复的档位，例如 `8,16,24,32`。每阶段到达 30 秒后不再发起新请求，但会等正在执行的请求结束；单个 socket 操作超时默认 10 秒，不是请求总耗时的硬上限。快速服务器可能在 30 秒前完成该档，这属于短批次初测，不是稳定负载持续 30 秒的测试。

每次成功登录会写一条 login_ticket；它过期后不会由本脚本清理。不要把长时间重复执行后的库规模变化误认为代码变化。所有探测和预热也会调用真实登录接口。

## 输出文件

| 文件 | 内容 |
|---|---|
| performance.png | 三张折线图：吞吐、成功请求 P50/P95、错误率 |
| performance.svg | 可缩放的同一张图 |
| summary.csv | 每个并发档位的汇总，Excel 可读 |
| summary.json | 配置、档位结果、错误分类及停止原因 |
| requests.csv | 每次完成的操作：阶段、请求序号、账号行号、耗时、HTTP 状态等 |

探测和预热记录保存在 requests.csv 的 probe/warmup 行，但不参与各计量档位的汇总；计量行 phase=measured。不会把密码、邮箱、响应正文、ticket 或 Token 写到结果中。账号行号只用于回查输入文件。

只重画已保存结果，不重新请求服务器：

```powershell
.\.venv-load\Scripts\python.exe tests\load\login_load_test.py --out tests\load\results\login01 --plot-only
```

## 指标定义

- 成功条件：HTTP 200、JSON code=login_success、login_ticket 为 64 位小写 hex。只看 HTTP 200 不算业务成功；本脚本不进一步消费 ticket。
- successful RPS：成功完成数 / 该档实际耗时；total RPS 包含错误完成。分母包含最后在途请求的排空时间，不把尚未完成的请求假装成成功吞吐。
- success_p50/p95/p99：只统计成功请求的延迟，用线性插值计算。all_p95_ms 另行包含失败耗时；错误率和错误分类须一起看，避免大量快速失败显得延迟很低。
- 单次延迟从 HTTP 请求调用开始到响应体读取/检查完成，包括客户端观察到的连接、TLS、网络、代理和后端开销。响应最多读取 64 KiB。
- 默认每次请求使用新的 TCP/TLS 连接，与长连接复用测试不同。可加 --keep-alive 尝试每个客户端复用一个连接；服务器关闭连接时仍需重连。不同连接模式的结果不能混作同一基线。
- client_cpu_s 是本机压测进程的 CPU 时间，**不是服务器 CPU**。本脚本没有 SSH 到服务端，也不采集服务器内存、磁盘、数据库等待，需另外观察。
- 固定并发与有限请求预算不是固定 QPS；服务器变慢时，闭环发送速度会下降。几十个样本的 P95/P99 只是探索性结果，不作为容量承诺或产品 SLO。

如果有缺失指标（例如没有成功请求），summary.json 使用 null，成功延迟图会留空，不会伪造为 0 ms。

## 停止和错误

- 探测登录失败时，不进入加压阶段。先看 requests.csv 的 HTTP status、error 和 business_code；403 可能来自代理/WAF，不能直接认定后端账号错误。
- 遇到 401、403、429 时，停止继续派发并排空已发出的请求，不重复轰击无效凭据或限流入口。
- 一档结束后错误率达到 10%，或成功请求 P95 达到 5000 ms，不再进入下一档。这是本脚本的小规模保护阈值，不是业务验收标准；可用 --stop-error-rate / --stop-p95-ms 调整。
- --max-seconds 控制不再派发的时刻，不会强行中断 socket 读写。--requests 是单档预算；到时间时实际完成数量可能少于预算，见 stop_reason。
- Ctrl+C 会停止新增请求，等待在途请求，尽量保存已完成数据；强制终止进程不保证保存。
- 返回码 0 表示所有设置档位完成，2 表示探测失败、保护停止或中断。检查 outcome，不要只看是否有 PNG。

采集数据使用标准库 http.client，验证 HTTPS 证书，不跟随重定向、不读取系统 HTTP_PROXY 环境变量。公网域名的请求会经过当前 DNS/代理路径，测到的是这条完整路径。不要用禁用证书校验来处理部署错误。

## 如何读三张折线图

1. 并发增加，成功 RPS 也增加、延迟变化不大：当前范围内可能还有余量。
2. RPS 增长很少，P95 明显上升：优先调查排队、密码计算、连接池和数据库等待。
3. 总 RPS 很高但成功 RPS 低：很多操作在快速失败，不能算容量提升。
4. 公网耗时上涨，但服务器资源不忙：同时检查本机压测进程、网络和代理。单凭曲线不能确认具体瓶颈。

默认参数只是第一轮探索，先固定服务器配置和日志级别，不在同一轮中调线程数、改连接池或改数据集。确认初测结果后，再设计持续时间更长、固定到达速率、服务器监控齐全的测试。

## 本地验证

```powershell
.\.venv-load\Scripts\python.exe -m unittest discover -s tests\load -p test_login_load_test.py -v
```

测试使用本机 HTTP 模拟服务器验证并发上限、超时/截止、业务检查、预热排除、停止阈值、结果脱敏和绘图；不把模拟服务器的速度当作后端性能。真实测试结果必须另看对应输出目录中的 target 和 outcome。

重复指定已存在的输出路径时，自动使用 _001、_002 等后缀目录，保留原结果。可配置最高并发为 64，默认档位保持不变；例如使用 --concurrency 16,24,32,48,64。--plot-only 仍读取原指定目录。
