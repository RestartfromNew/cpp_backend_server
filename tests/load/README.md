# 压测账号、设备和 prekey 数据准备

建议把准备工作分开：先离线生成测试数据，批量导入测试数据库，再通过 HTTP 登录和设备验证获得 Token，最后开始计时压测。生成与导入都不计入“发送消息吞吐量”；如果要测注册或登录能力，应另外设计场景。

生成脚本：[generate_load_fixtures.py](generate_load_fixtures.py)。它只写本地文件，不连接数据库，不修改服务端源码，不自动启动压测。

## 生成哪些数据

例如 `--accounts 1000 --devices 1 --prekeys 20`：

| 表/文件 | 数量 | 作用 |
|---|---|---|
| app.users | 1000 | active 测试账号，用户名如 lt_load01_0000001 |
| app.password_credentials | 1000 | 可供现有密码登录流程验证的 Argon2id PHC 哈希 |
| app.user_devices | 1000 | 每个账号一个 active 设备，身份公钥保存在这里 |
| app.device_key_bundles | 1000 | 每设备一个 is_current=true 的 signed prekey bundle，版本 1、key id 42 |
| app.device_one_time_prekeys | 20000 | 每设备 20 个 available 公钥，编号 1001 起 |
| app.friend_relation | 1000 | 500 对好友的双向记录：账号 1↔2、3↔4，避免所有流量集中在同一设备对 |
| accounts.jsonl | 1000 行 | 登录账号、明文测试密码、设备 ID、对端用户和设备 ID |
| public_bundles.jsonl | 1000 行 | 对应公有资料，用于核对或测试客户端装载 |
| manifest.json | 1 | run、规模、密钥模式、目标数据库及完成标志 |
| import.sql | 1 | 数据库名保护、六次 COPY、单个导入事务、ANALYZE |

不预置 Access Token、Refresh Token、login_ticket 或信封。Token 是有时效的登录结果，不应作为长期测试基础数据。需要测试历史同步时，另行通过实际发送流程准备信封。

## 两种密钥模式

### opaque：后端存储、推送、同步、ACK 和预密钥接口压力

默认 `--mode opaque`，生成长度正确的随机占位值，协议标识为 LOADTEST-OPAQUE-v1。**它们不是可验证签名或真实加密密钥，不能交给真实 iOS 加密流程。** 该模式测试服务端对不透明数据的处理，并不验证端到端加密。服务端不解密 packet；压测客户端仍需按 API 构造合法报文。

### xeddsa：独立的真实密码学测试材料

`--mode xeddsa` 需要 `requirements-xeddsa.txt`。为每台设备生成独立 X25519 身份、signed prekey 和 one-time key，并生成和自检 XEdDSA 签名。沿用已有四设备生成器的测试约定：身份公钥使用规范 Edwards sign bit 0，签名消息为 `0x05 || raw_signed_prekey_public`。

额外导出 `private_keys.TEST_ONLY.jsonl`，只供本地测试客户端使用，**不写数据库**。不要提交这些输出文件到代码仓库。

这不是 Signal/PQXDH/Double Ratchet 的完整实现，也不保证与当前 iOS 协议编码一致。若要测真实 iOS 端到端加密，先用少量设备确认签名编码、密钥格式和包格式兼容，再批量生成；协议不同应使用 iOS 实际密钥库生成、导出其测试公有材料，不能直接宣称这些 fixture 兼容。

## Windows 生成和导入

要求 Python 3.10+；PostgreSQL psql 客户端用于导入。

```powershell
Set-Location D:\projects\cpp_backend_server
py -m venv .venv-load
.\.venv-load\Scripts\python.exe -m pip install -r tests\load\requirements.txt

.\.venv-load\Scripts\python.exe tests\load\generate_load_fixtures.py --database backend_test --run load01 --accounts 1000 --devices 1 --prekeys 20 --shared-test-hash --mode opaque --out D:\load-fixtures\load01
```

脚本只生成文件，尚未导入。`backend_test` 必须是**已具有当前系统完整表结构的测试库**，不是只有 `001_create_auth_system.sql` 的初始空库；该历史迁移没有全部现有业务表/字段。用当前数据库的 schema-only 导出建好测试库，并应用现有迁移。不要把生产库 URL 交给测试脚本。

如果 PostgreSQL 在当前 Windows/WSL localhost:5432，进入输出目录后导入：

```powershell
Set-Location D:\load-fixtures\load01
psql.exe -X -h 127.0.0.1 -p 5432 -U postgres -d backend_test -v ON_ERROR_STOP=1 -f import.sql
if ($LASTEXITCODE -ne 0) { throw 'Import failed; read the PostgreSQL error before retrying' }
```

数据库用户可替换为拥有测试表写权限的 owner，密码由 psql 提示输入。若 PostgreSQL 只在 Linux 可访问，把**整个输出目录**放到 Linux 能访问的位置，cd 到该目录后运行对应命令：

```bash
psql -X -h 127.0.0.1 -p 5432 -U postgres -d backend_test -v ON_ERROR_STOP=1 -f import.sql
```

使用 `\copy`，CSV 由运行 psql 的客户端读取，所以必须从输出目录执行，而不是只复制 import.sql。无需让 PostgreSQL 服务端读取 Windows 路径。

导入前检查 `current_database()` 是否与生成时 `--database` 完全相同。设备 CSV 先进入当前会话的临时表，再写入真实设备表；created_at 和 authorized_at 显式使用同一数据库 CURRENT_TIMESTAMP，不使用生成文件时的授权时间。账户、设备、bundle、OPK、好友在一个事务导入，保留原约束和触发器，不禁用外键，不修改 schema。任一 COPY 失败，非交互 psql 退出后事务回滚。COMMIT 后执行 ANALYZE 更新统计信息；如果只是 ANALYZE 失败，数据已经提交，不能认为导入已回滚。

## 规模、密码和重复运行

- `--accounts` 必须为偶数；从 10 或 100 个账号做一次真实登录/握手验证，再扩大到 1000、10000。
- `--devices` 是每账号设备数；多个设备有各自独立密钥和库存，不共用 identity key。
- `--prekeys` 是每设备初始库存。OPK 用于新的加密会话初始化，**不是每条聊天消息消耗一个**；领取压力场景会耗尽库存，应单独规划补充。
- 默认每账号分别生成随机盐的 Argon2id 哈希，成本为 t=2、64 MiB、p=1，因此大量账号的准备时间可能较长。
- `--shared-test-hash` 是显式的测试加速选项：所有测试账号使用同一密码和同一哈希，只计算一次。它不降低服务端每次密码验证的 Argon2 工作量，但不是正常注册时的独立随机盐数据，不可用于真实用户。
- 数据库中不允许把明文密码放进 password_hash。accounts.jsonl 中有明文测试密码，默认 TestPass123@，仅用于驱动测试登录，可用 --password 改为其他测试值。
- run 决定 UUID 命名空间；同一 run 的 ID 稳定，但密钥材料不稳定。输出目录已存在时拒绝覆盖，重复导入遇到唯一约束会失败，不执行 UPSERT、不重置已领取 OPK、不偷偷换钥。
- 每轮可从测试库快照恢复，或选择新的 run 和新的输出目录。新 run 不会清除旧数据，表规模会继续增长，比较结果时须记录初始数据量。脚本不提供自动清库。
- 生成中断时可能留下未完成目录；只使用成功生成、manifest.complete=true 且存在 import.sql 的数据。输出入口在全部 CSV 关闭后才生成。

可选 XEdDSA 数据生成（新目录和 run）：

```powershell
Set-Location D:\projects\cpp_backend_server
.\.venv-load\Scripts\python.exe -m pip install -r tests\load\requirements-xeddsa.txt
.\.venv-load\Scripts\python.exe tests\load\generate_load_fixtures.py --database backend_test --run real01 --accounts 10 --devices 2 --prekeys 20 --mode xeddsa --out D:\load-fixtures\real01
```

## 导入后核对

示例 run=load01，不使用 LIKE 中未转义的下划线匹配：

```sql
WITH fixture_users AS (
    SELECT id FROM app.users WHERE starts_with(username, 'lt_load01_')
), fixture_devices AS (
    SELECT device_id FROM app.user_devices WHERE user_id IN (SELECT id FROM fixture_users)
)
SELECT
    (SELECT count(*) FROM fixture_users) AS accounts,
    (SELECT count(*) FROM fixture_devices) AS devices,
    (SELECT count(*) FROM app.device_key_bundles WHERE device_id IN (SELECT device_id FROM fixture_devices) AND is_current) AS bundles,
    (SELECT count(*) FROM app.device_one_time_prekeys WHERE device_id IN (SELECT device_id FROM fixture_devices) AND state='available') AS available_prekeys,
    (SELECT count(*) FROM app.friend_relation WHERE user_id IN (SELECT id FROM fixture_users) AND friend_id IN (SELECT id FROM fixture_users)) AS directed_friend_rows;
```

1000×1×20 的预期结果依次为 1000、1000、1000、20000、1000。然后从 accounts.jsonl 取首个账号做一次完整 HTTP 登录、验证设备和 WebSocket 测试。生成器的哈希通过本地 PHC 验证检查，但仍需用实际服务的 libsodium 登录验证一次。

## 从测试数据取得 Token

PowerShell 示例：先把后端 DATABASE_URL 指向刚才导入的测试库，确保服务已重新加载配置。这里使用你指定的 localhost:8081，不能让服务仍连接另一个数据库。

```powershell
$ErrorActionPreference = 'Stop'
$account = Get-Content D:\load-fixtures\load01\accounts.jsonl -TotalCount 1 | ConvertFrom-Json
$baseUrl = 'http://localhost:8081'
$loginBody = @{ email=$account.email; password=$account.password } | ConvertTo-Json -Compress
$login = Invoke-RestMethod -Method Post -Uri "$baseUrl/login_by_email" -ContentType 'application/json' -Body $loginBody
if ($login.code -ne 'login_success' -or -not $login.login_ticket) { throw 'Login ticket missing' }
$verifyBody = @{ login_ticket=$login.login_ticket; device_id=$account.device_ids[0] } | ConvertTo-Json -Compress
$verified = Invoke-RestMethod -Method Post -Uri "$baseUrl/device_verification" -ContentType 'application/json' -Body $verifyBody
if (-not $verified.access_token -or -not $verified.refresh_token) { throw 'Device login failed' }
Write-Host "Device verified: $($verified.device_id); tokens kept in `$verified"
# 握手头：Authorization = "Bearer $($verified.access_token)"，deviceID = $verified.device_id
```

大规模持续连接/发送场景，在计时前分批完成登录，避免准备过程突然耗尽密码哈希内存。Access Token 约 15 分钟，需要规划长时间测试的刷新；login_ticket 一次性，不能复用或直接当 JWT。

## 验证范围

```powershell
Set-Location D:\projects\cpp_backend_server
.\.venv-load\Scripts\python.exe -m unittest discover -s tests\load -p test_generate_load_fixtures.py -v
```

本次基础模式及修复工具 7 项测试通过：包括行数/关联/双向关系、字节长度、PHC 正误密码验证、参数检查、不覆盖、零库存及独立盐；可选 XEdDSA 检查因环境无依赖跳过。没有连接真实 PostgreSQL、导入数据或执行真实服务登录；数据库实际额外约束或 schema 差异仍可能导致导入被拒绝，应先小规模验证。

参考：[PostgreSQL COPY](https://www.postgresql.org/docs/current/sql-copy.html)、[cryptography Argon2id / PHC](https://cryptography.io/en/latest/hazmat/primitives/key-derivation-functions/#argon2id)。

## 已生成数据包出现 authorization_time_valid 错误

旧版脚本的 authorized_at 是生成文件的时间，created_at 使用导入时数据库默认时间，导致授权时间早于创建时间。无需删除约束、清库或重新生成密钥。先在 WSL 修复已有 import.sql，再重新导入：

```bash
cd /mnt/d/load-fixtures/load01
python3 /mnt/d/projects/cpp_backend_server/tests/load/fix_fixture_device_times.py import.sql
sudo -u postgres psql -X -d backend_test -v ON_ERROR_STOP=1 -f import.sql
```

修复工具仅依赖 Python 标准库，备份原文件为 import.sql.before_device_time_fix，CSV、账号 ID 和密钥不变；重复执行识别已修复文件，不重复修改。此前 COPY 失败发生在 COMMIT 之前，按上述非交互 psql 命令退出后，之前 COPY 的账号和密码凭据也会回滚，无需手动删除。修复后的导入使用临时表，不改动正式表结构。
