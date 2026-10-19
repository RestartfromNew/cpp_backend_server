# 服务器后端接口文档

更新时间：2026-10-16。

English version: [Backend API Documentation](backend-api.en.md)。

本文描述当前源代码已经实现的 HTTP 与 WebSocket 接口，而不是计划中的最终设计。根据 `main`、Router、Handler、认证服务、Repository 和 HTTP 解析/响应实现整理。2026-10-16 已根据用户提供的运行输出记录部分设备 HTTP 接口实测结果，范围和未验证项见 20.1；不代表所有接口均已实测。部署的可执行文件若未重新编译，行为可能与当前源码不同。

## 1. 访问地址与通用约定

| 场景 | 基础地址 | 说明 |
| --- | --- | --- |
| 域名入口（本文默认） | `https://aittelegramapi.uk` | 用户已配置的 HTTPS 域名；本次未在线核验 DNS、证书或代理配置 |
| Windows 本地通过 Nginx 访问 WSL | `http://localhost:8088` | 保留为本地调试入口，不是公网客户端应使用的地址 |
| 直接访问 C++ 服务 | `http://localhost:8082` | 当前源码监听 `0.0.0.0:8082`；systemd部署副本的实际端口以部署二进制/配置为准 |

以下接口路径拼接在基础地址之后，例如 `https://aittelegramapi.uk/login_by_email`。当前代码没有 `/api` 或 `/v1` 前缀。基础地址不包含 `/login_by_email`；该部分是登录接口路径。原有认证示例使用域名入口，第 12 节的 PowerShell 好友示例使用本地 8082 端口。

- JSON 请求使用 `Content-Type: application/json`，请求体必须是 JSON 对象，不接受数组、字符串或 `null`。当前 Handler 实际按请求体解析，没有强制验证 Content-Type。
- 字段名区分大小写；额外 JSON 字段目前被忽略。
- 路由按方法和路径精确匹配。多一个结尾 `/` 或附带查询字符串，可能导致匹配失败；当前没有独立解析 query 参数。
- 未匹配到方法和路径时返回 404，当前未区分 405 Method Not Allowed。
- 普通HTTP接口每次响应后关闭连接，响应带 `Connection: close`；Nginx 到客户端的连接策略可能不同。
- 带请求体的请求应提供正确的 `Content-Length`，curl 会自动生成。当前解析器没有实现 chunked 请求体。
- 后端请求体长度上限为 1,048,576 字节；超限目前返回 400，不是 413。代理也可能在进入后端前拒绝请求。

### 请求头大小写的当前限制

当前解析器保留请求头原始大小写，而下游使用区分大小写的 map 查找。联调时请使用下列精确写法：

```http
Content-Type: application/json
Content-Length: 123
authorization: Bearer ACCESS_TOKEN
X-User-Email: alice@example.com
```

其中 `Content-Length` 的值由客户端计算，不要复制示例中的 123。认证头当前接受 `authorization` 或 `Authorization`，并使用精确前缀 `Bearer `；其他大小写组合尚未统一支持。这是当前实现的兼容性缺陷，不是对客户端应长期施加的协议规则；经过 Nginx 时还需确认转发后的头名称。

## 2. 接口总览

| 方法 | 路径 | 认证方式 | 当前用途 |
| --- | --- | --- | --- |
| POST | `/register_by_email` | 不需要 | 邮箱密码注册 |
| POST | `/login_by_email` | 不需要 | 验证密码并签发一次性login_ticket |
| POST | `/register_device` | login_ticket | 登记设备并完成登录 |
| POST | `/device_verification` | login_ticket | 验证已有设备并完成登录 |
| POST | `/revoke_device` | 必须 | 密码确认后撤销本人设备；支持重复请求 |
| POST | `/upload_one_time_prekeys` | 必须 | 上传 1～10 个设备一次性公钥 |
| POST | `/get_one_time_prekey` | 必须 | 领取好友设备的一次性预密钥 |
| POST | `/user_devices` | 必须 | 拉取本人或好友的 active 设备 |
| POST | `/friend_devices` | 必须 | 兼容旧入口，必须指定目标 |
| POST | `/get_device_key_bundle` | 必须 | 获取好友设备的带签名预密钥资料 |
| POST | `/verify_access_token` | 必须 | 验证 Access Token，成功后进入受保护 Handler |
| GET | `/refresh_token` | 不需要 | 验证 Refresh Token 并签发新的 Access Token |
| GET | `/user` | 不需要 | 旧版按邮箱查询用户的调试接口，不建议继续依赖 |
| GET | `/find_user_by_username` | 必须 | 按用户名查找用户，要求 JSON Body |
| POST | `/request_friendship` | 必须 | 创建好友申请 |
| GET | `/fetch_unprocessed_friend_request` | 必须 | 拉取收到的待处理申请，无需 Body |
| POST | `/process_friendship_request` | 必须 | 接受、拒绝或取消申请 |
| GET | `/fetch_friends` | 必须 | 拉取当前用户的好友列表，无需 Body |

表中“必须”指Authorization中的正式Access Token；login_ticket表示JSON请求体认证，不能用普通Bearer Token替代。

注意：`/refresh_token` 当前确实注册为 GET，且要求 JSON 请求体。本文忠实记录此行为，不代表推荐这样设计；后续应改为 POST。

## 3. 通用错误格式

由 `ErrorResponseMaker` 产生的错误响应使用 JSON：

```json
{
  "error": {
    "code": "device_not_owned",
    "message": "Device does not belong to this login"
  }
}
```

客户端根据 HTTP 状态码和 `error.code` 判断，不应依赖英文 message 的大小写或措辞。 密码登录例外：读取顶层code；客户端可使用error?.code或code统一分支，但不要假定所有失败都有message。

### 请求体通用错误

以下错误码适用于要求JSON请求体的接口，但结构有区别：/login_by_email在Handler中统一返回顶层code，不含error/message；其他接口通常返回error.code/message。HTTP解析器在进入Handler前产生的错误仍是通用error对象。

| HTTP 状态 | code | message | 条件 |
| --- | --- | --- | --- |
| 400 | `missing_request_body` | Request body is required | 请求体为空 |
| 400 | `invalid_json` | Request body must be a valid JSON object | JSON 解析失败或顶层不是对象 |

### 基础设施错误

| HTTP 状态 | code | 条件 |
| --- | --- | --- |
| 400 | `invalid_http_request` | HTTP 解析器返回错误，包括 Content-Length 无效或超出上限 |
| 404 | `route_not_found` | 方法和路径未匹配任何路由 |
| 503 | `service_unavailable` | 请求处理过程中出现 DatabaseErrorKind::Connection |
| 500 | `internal_server_error` | 未被业务处理的数据库查询、约束、数据转换错误或其他标准异常 |

这些是后端应用的主要错误映射；Nginx 自己生成的 502、504 等响应不保证使用上述 JSON 格式。网络断开等情况也不保证能收到响应。

## 4. 邮箱密码注册

### POST `/register_by_email`

无需 Access Token。

```json
{
  "email": "alice@example.com",
  "password": "TestPass123!",
  "username": "alice_chat01",
  "display_name": "Alice"
}
```

| 字段 | 类型 | 必填 | 当前规则 |
| --- | --- | --- | --- |
| email | string | 是 | 非空；去除首尾普通空格、转为小写，再进行正则校验 |
| password | string | 是 | 去除首尾普通空格后，6～20 个 ASCII 测试字符范围内满足密码正则；至少有小写字母、大写字母、数字、非字母数字且非空白的字符，整体不能含空白 |
| display_name | string | 是 | 去除首尾普通空格后，3～20 个字符，仅英文字母、数字、下划线 |
| username | string | 是 | 去除首尾普通空格后，3～30 个字符，允许英文字母、数字、下划线和英文句点；当前代码不转小写，最终还受数据库约束限制 |

这里的“去除空格”仅指代码移除首尾的 ASCII 空格，不是所有 Unicode 空白字符。当前密码也会被去除首尾空格，这应在后续修正；客户端暂时不要使用带首尾空格的密码。非 ASCII 密码的长度/字符分类不应按 Unicode 字符数理解，当前实现未定义完善的 Unicode 密码规则。

当前邮箱正则：

```text
^[a-zA-Z0-9_+&*-]+(?:\.[a-zA-Z0-9_+&*-]+)*@(?:[a-zA-Z0-9-]+\.)+[a-zA-Z]{2,10}$
```

当前密码正则：

```text
^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)(?=.*[^A-Za-z0-9\s])\S{6,20}$
```

### 成功响应：201 Created

```json
{
  "id": "33e76cf5-fc26-4fff-9f88-d08690ffb863",
  "display_name": "Alice",
  "username": "alice_chat01"
}
```

`id` 是 UUID 字符串。注册在同一事务内创建用户和密码凭据；注册成功不返回 Token，也不等于已经登录。

### 业务错误

| HTTP 状态 | code | 条件 |
| --- | --- | --- |
| 400 | `missing_register_email` | email 缺失、不是字符串或原始值为空字符串 |
| 400 | `missing_register_password` | password 缺失、不是字符串或原始值为空字符串 |
| 400 | `missing_register_display_name` | display_name 缺失、不是字符串或原始值为空字符串 |
| 400 | `missing_register_username` | username 缺失、非字符串或为空 |
| 400 | `invalid_user_name` | username 格式错误或匹配到用户名 CHECK 约束失败 |
| 400 | `invalid_email` | 规范化后邮箱不符合规则 |
| 400 | `invalid_password` | 规范化后密码不符合规则 |
| 400 | `invalid_display_name` | 规范化后名称不符合规则 |
| 409 | `email_already_exists` | 邮箱唯一约束冲突且被服务层识别 |
| 409 | `username_already_exists` |用户名唯一约束冲突且被服务层识别 |
| 500 | `register_failed` | 注册服务返回注册失败 |

多个字段同时错误时，只返回首先检查到的一个错误。重复邮箱映射依赖数据库错误码 `23505` 和约束名称 `password_credentials_login_email_unique`，数据库结构必须与代码一致。

## 5. 邮箱密码登录：取得一次性 login_ticket

### POST `/login_by_email`

公开接口，不需要Authorization。请求只需email、password；旧device_id字段不参与登录校验。注册账号不会签发ticket，注册成功后仍须调用本接口。

```json
{"email":"amy_chat01@example.com","password":"TestPass123@"}
```

| 字段 | 规则 |
|---|---|
| email | 非空字符串，沿用现有trim和小写规范化 |
| password | 非空字符串，当前仍沿用conversionInput处理，不在本次文档更新中改变密码规范化 |

成功200：

```json
{"code":"login_success","login_ticket":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","expires_in":300}
```

ticket示例仅说明64个小写hex字符的格式，不是可用凭据。客户端必须使用本次响应中的真实值。expires_in单位为秒，期限由数据库签发时刻决定，不会因客户端收到较晚而顺延。成功响应不包含正式Token、用户ID、username或device_status。

| HTTP | code | 条件 |
|---|---|---|
| 400 | missing_request_body | 空请求体 |
| 400 | invalid_json | 非法JSON或非对象 |
| 400 | missing_register_email | email缺失、非字符串或空；保留旧错误码拼写 |
| 400 | missing_register_password | password缺失、非字符串或空 |
| 401 | invalid_credentials | 账号不存在或密码不匹配，统一处理 |
| 403 | account_disabled | 账号无效，或签发ticket前复查账号失败 |
| 503 | service_unavailable | 数据库连接故障 |
| 500 | internal_error | 其他SQL、约束、转换或内部处理失败 |

所有上述Handler失败只有顶层code，例如 `{"code":"internal_error"}`，不是error对象。该500证明已到登录Handler，不等于路径错，也不等于密码错误；新增ticket表缺失/权限不足的排查见22节。TooManyRequests枚举尚无实际限流实现，不能据此假定登录已有频率保护。

已有设备下一步POST /device_verification；新设备下一步POST /register_device，两者均提交login_ticket。ticket不能当作Access Token或Refresh Token，也不能建立WebSocket连接。完整设备登录见21节。

## 6. 验证 Access Token

### POST `/verify_access_token`

```http
POST /verify_access_token HTTP/1.1
Host: aittelegramapi.uk
authorization: Bearer ACCESS_TOKEN
Content-Length: 0
```

无需 JSON 请求体。不要把 Refresh Token 放到这里。

### 成功响应：200 OK

**当前响应体为空**，虽然 main 设置了 `Content-Type: application/json`。Handler 中构造的 `{"success":"success"}` 没有写入 `response.body`，因此客户端目前不能直接把成功响应作为 JSON 解析。

此接口证明 Token 通过验证，并进入受保护 Handler；它没有进一步查询用户当前是否存在、是否已禁用，也不返回用户资料。

### 认证错误

| HTTP 状态 | code | 条件 |
| --- | --- | --- |
| 401 | `invalid_access_token` | 缺少认证头、Bearer 格式错误、Token 不合法或验证失败 |
| 401 | `access_token_expired` | 认证服务明确返回 Expired |

当前验证码先处理 JWT 库的验证错误，再手动检查过期时间，因此过期 Token 也可能被归类为 `invalid_access_token`；客户端不能假设所有过期情况都必然返回 `access_token_expired`。当前 401 响应未设置 `WWW-Authenticate`。

### 内部调用关系

`HttpSession → Router::route → Router::protected_route → AuthMiddleWare::authenticate → AccessTokenService::verify → protected_handler(request, userId)`。

不需要先调用此接口再访问其他受保护接口；未来每个受保护接口应在自己的请求入口执行同样的认证。当前 main 中只有这个接口注册为受保护路由。

## 7. 使用 Refresh Token 获取新的 Access Token

### GET `/refresh_token`

无需 Access Token，但必须在 JSON 请求体提交 Refresh Token 明文：

```json
{
  "refreshToken": "REFRESH_TOKEN"
}
```

注意输入是驼峰字段 `refreshToken`，不是设备登录完成响应里的 `refresh_token`。不能把数据库保存的哈希当成 Token 提交，也不用提交 user_id。

服务端对明文计算相同哈希，用哈希找到唯一记录，然后检查撤销和过期状态，并从该记录取得 user_id。

### 成功响应：200 OK

```json
{
  "id": "33e76cf5-fc26-4fff-9f88-d08690ffb863",
  "access_token": "NEW_ACCESS_TOKEN",
  "refresh_token": "REFRESH_TOKEN"
}
```

- 返回的 Refresh Token 与本次提交的相同，**没有轮换，也没有延长数据库中的过期时间**。
- 新 Access Token 的有效期从本次生成时起计算 15 分钟。
- 当前成功响应没有显式设置 `Content-Type: application/json`，尽管响应体是 JSON 文本。
- 当前刷新只验证 Token 记录，不查询用户的 is_active 状态。

### 业务错误

| HTTP 状态 | code | message / 条件 |
| --- | --- | --- |
| 400 | `missing_refreshToken` | Missing refresh token；字段缺失、非字符串或为空 |
| 401 | `invalid_refreshToken` | Invalid refresh token；找不到哈希对应记录 |
| 401 | `invalid_refreshToken` | Refresh token is expired；已过期或已撤销 |

已过期或已撤销的 Refresh Token 不能继续刷新，应重新登录。当前遇到过期且未撤销的记录，会先调用 Repository 标记撤销；若这次写数据库失败，可能返回基础设施错误，而不是上述 401。

GET 请求体不适合普通浏览器前端调用；迁移到 POST 后，应同步修改 main 的路由注册、客户端请求和本文。不要在尚未改代码时直接使用 POST，也不要为了绕过 GET 请求体把 Token 放到 URL 查询参数里。

RefreshTokenRepository查询现在JOIN user_devices和users，要求Token绑定设备属于该用户、设备active且账号有效；不满足时表现为401 invalid_refreshToken。Token仍是数据库保存哈希、客户端提交原文；没有把login_ticket当刷新凭据，也没有刷新时延长有效期或轮换Token。Refresh响应不包含device_id，客户端保留完成设备登录时保存的device_id。

## 8. 旧版查询用户接口

### GET `/user`

```http
GET /user HTTP/1.1
Host: aittelegramapi.uk
X-User-Email: alice@example.com
```

这是公开调试接口，没有 Access Token 校验。

| HTTP 状态 | code / 响应 | 条件 |
| --- | --- | --- |
| 400 | `missing_user_email` | 缺少 X-User-Email 或值为空 |
| 404 | `user_not_found` | 查询正常完成但没有用户 |
| 200 | 包含 id、display_name 的 JSON | 查询和序列化成功 |
| 500 | `internal_server_error` | SQL 与当前数据库结构不匹配等 |

目前 Repository 仍执行 `SELECT id, username, email FROM users WHERE email = $1`，与新的 `app.users` / `app.password_credentials` 结构不一致，实际结果还受数据库 search_path 和是否保留旧表影响。

此外，这里直接把 UUID 对象赋给 JSON，没有像注册/设备登录完成响应一样显式转换成字符串；不要依赖其 id 字段已有统一的字符串格式。该接口尚不适合作为正式用户资料接口，本文不提供稳定的成功响应契约。

## 9. Windows PowerShell：完成已有设备登录

以下是PowerShell代码，不是CMD命令。baseUrl必须为纯URL字符串，不能写成Markdown链接 `[http://localhost:8082](...)`。先替换账号和该账号已有active设备；首次使用没有device_id时，不要执行设备验证，按13节登记新设备。

```powershell
$ErrorActionPreference = 'Stop'
$baseUrl = 'http://localhost:8082'

# Existing active device of this account; replace before executing.
$deviceId = 'f7ab7d41-9955-4a73-922d-7b7a80ebdac4'
$password = [System.Net.NetworkCredential]::new('', (Read-Host 'Password' -AsSecureString)).Password

$loginBody = @{ email = 'alice_chat01@example.com'; password = $password } | ConvertTo-Json -Compress
$loginArgs = @{
    Method = 'Post'; Uri = "$baseUrl/login_by_email"
    ContentType = 'application/json; charset=utf-8'
    Body = [System.Text.Encoding]::UTF8.GetBytes($loginBody)
    TimeoutSec = 20; ErrorAction = 'Stop'
}
$login = Invoke-RestMethod @loginArgs
if ($login.code -ne 'login_success' -or -not $login.login_ticket) { throw 'Password login did not issue a ticket' }

$verifyBody = @{ login_ticket = $login.login_ticket; device_id = $deviceId } | ConvertTo-Json -Compress
$verifyArgs = @{
    Method = 'Post'; Uri = "$baseUrl/device_verification"
    ContentType = 'application/json; charset=utf-8'
    Body = [System.Text.Encoding]::UTF8.GetBytes($verifyBody)
    TimeoutSec = 20; ErrorAction = 'Stop'
}
$verified = Invoke-RestMethod @verifyArgs
if (-not $verified.access_token -or -not $verified.refresh_token) { throw 'Device login did not issue tokens' }
$accessToken = $verified.access_token
$refreshToken = $verified.refresh_token
$userId = $verified.id
$deviceId = $verified.device_id
Write-Host "Login complete: user=$userId device=$deviceId"
# Do not log the credential objects in normal client code.
```

新设备：先执行上面登录部分，然后用同一ticket和13节的公开密钥字段POST /register_device。成功后使用其响应的id/device_id/access_token/refresh_token。只有密码登录成功不会产生Refresh Token记录。

## 10. 当前边界与后续修改清单

下面不是已实现能力，而是根据当前源码列出的后续工作：

1. 修复请求头大小写处理，避免合法认证头或 Content-Length 被遗漏。
2. 给 `/verify_access_token` 写入明确的 JSON 成功响应；给刷新成功响应补 Content-Type。
3. 将刷新接口改为 POST，并统一 refresh_token 字段和错误码命名。
4. 正确区分 JWT 过期与其他验证错误；测试过期分支。
5. 停止修改用户输入的密码（当前会 trim）；制定已有账号兼容策略再修改。
6. 明确用户禁用后现存 Token 的处理策略；当前登录和Refresh查询检查账号有效性，Refresh查询还检查设备归属及active；单纯Access Token签名验证不查询数据库，仍需区分各业务接口。
7. 修复或下线旧 `/user`；正式用户接口应考虑鉴权和统一 UUID 序列化。
8. 本文没有把 Refresh Token 轮换、退出登录、全部设备退出、第三方登录、邮箱验证、自动 Token 清理、限流、CORS 预检写成可用接口：当前 main 未提供这些对应入口或完整链路。Repository 中有函数不等于 HTTP 功能已经接通。

已按用户提供的域名更新访问示例，但域名和 HTTPS 配置不代表应用已满足生产安全要求。上述限制仍需处理；公开部署前还需要独立核验 TLS 证书、网络暴露范围、请求限制和认证安全。本次文档修改没有对公网服务发送注册、登录或其他测试请求。

## 11. 维护文档时对应的代码位置

| 内容 | 代码 |
| --- | --- |
| 端口、公开/受保护路由注册 | [main.cpp](../src/main.cpp) |
| 路由匹配与认证错误响应 | [Router.cpp](../src/handler/Router.cpp) |
| 注册、登录、旧查询、验证成功响应 | [UserHandler.cpp](../src/handler/UserHandler.cpp) |
| 刷新请求和响应 | [RefreshTokenHandler.cpp](../src/handler/RefreshTokenHandler.cpp) |
| 读取 Bearer 认证头 | [AuthMiddleWare.cpp](../src/http/AuthMiddleWare.cpp) |
| Access Token 生成和校验 | [AccessTokenService.cpp](../src/Auth/AccessTokenService.cpp) |
| Refresh Token 生成、哈希和有效性校验 | [RefreshTokenService.cpp](../src/Auth/RefreshTokenService.cpp) |
| 注册校验规则 | [RegisterService.cpp](../src/service/RegisterService.cpp) |
| 登录流程 | [LoginService.cpp](../src/service/LoginService.cpp) |
| 用户查询、好友及设备撤销 | [UserRepository.cpp](../src/repository/UserRepository.cpp) |
| ticket生成与最终Token签发 | [LoginTicketService.cpp](../src/service/LoginTicketService.cpp) |
| ticket持久化与一次性消费事务 | [LoginTicketRepository.cpp](../src/repository/LoginTicketRepository.cpp) |
| Refresh Token插入、查询与有效期 | [RefreshTokenRepository.cpp](../src/repository/RefreshTokenRepository.cpp) |
| 完成登录的响应与错误映射 | [LoginTicketResponse.h](../include/handler/LoginTicketResponse.h) |
| HTTP 请求解析 | [HttpParse.cpp](../src/http/HttpParse.cpp) |
| 通用异常到 HTTP 错误的转换 | [HttpSession.cpp](../src/http/HttpSession.cpp) |

接口路径、参数、状态码或响应结构改变时，应在同一次修改中更新本文，并重新执行注册→登录→受保护访问→刷新的测试链路。

## 12. 用户查找与好友接口

本节接口都要求小写认证头 `authorization: Bearer ACCESS_TOKEN`。认证失败返回 401：
`invalid_access_token` 或 `access_token_expired`，以中间件实际分类为准。
操作者 ID 来自 token，不从 Body 获取。UUID 在 JSON 中表示为字符串。
本次仅核对源码和文档，没有运行接口测试。

### 12.1 GET `/find_user_by_username`

当前要求 JSON Body，不使用查询参数：

```json
{"username":"bob_chat01"}
```

成功为 200：

```json
{"id":"0776df58-9cbe-4f81-b613-4439f6c3b566","username":"bob_chat01","display_name":"Bob"}
```

按用户名精确查询，不自动转换大小写。错误：

| HTTP | code | 条件 |
| --- | --- | --- |
| 400 | missing_request_body / invalid_json | Body 为空或不是合法 JSON 对象 |
| 400 | missing_username | username 缺失、非字符串或为空 |
| 400 | cannot_find_myself | 查找当前用户自己 |
| 404 | user_not_found | 未找到用户 |

数据库异常由 HTTP 层统一处理。GET Body 的兼容性有限，属于当前实现而非推荐的长期接口设计。

### 12.2 POST `/request_friendship`

```json
{"friend_id":"0776df58-9cbe-4f81-b613-4439f6c3b566","message":"Hi Bob, I am Alice!"}
```

friend_id 是目标用户 UUID；message 可省略，当前非字符串或空值也按空字符串处理。
成功返回 **200、空响应体**，表示申请已保存，不表示已成为好友或通知已送达。

| HTTP | code | 条件 |
| --- | --- | --- |
| 400 | missing_request_body / invalid_json | Body 错误 |
| 400 | missing_friend_id | friend_id 缺失、非字符串或为空 |
| 400 | cannot_request_friendship_myself | 向自己申请 |
| 409 | request_already_exists | 双方已有 pending 申请且唯一索引错误被识别 |
| 503 | request_friendship_failed | 当前 Handler 捕获的服务异常 |

当前非法 UUID 转换在 try 外，可能返回通用 500；不存在的目标可能因外键错误返回 503，并非 404。
好友申请说明是服务端可读文本，不是端到端加密聊天正文。

申请成功入库后，Handler 尝试调用 push_To_User 通知接收者的在线设备；构造或投递通知抛出异常时只记录日志，仍返回200空响应体，不会把已保存申请改为503。离线或漏收后通过 /fetch_unprocessed_friend_request 补取。推送不代表好友关系已建立，也不使用密文信封ACK。

通知type为friend.request.received，unprocessed_requests为单项数组，字段与待处理申请列表一致：request_id和id为UUID字符串，id表示申请者，message为申请附言。Pool将同一通知分别投递到接收用户每个有在线索引的设备，跨Worker使用原任务队列。客户端按request_id去重/更新，避免推送和列表拉取重复显示；重新上线或漏收时拉取待处理列表。一次通知包含一项，不代表完整列表；它不走chat.envelopes.ack。

```json
{
  "type": "friend.request.received",
  "unprocessed_requests": [
    {
      "request_id": "efbd99da-b38f-4ee9-9c70-6a2ef96c2820",
      "id": "85467846-84e3-4bcb-b3f9-a4f5dd1cd492",
      "display_name": "Alice",
      "username": "alice_chat01",
      "message": "Hi Bob, I am Alice!"
    }
  ]
}
```


Repository 内部的 request_time 已从数据库 created_at 转换为 system_clock::time_point，创建返回和列表读取统一采用Unix纪元微秒，保留数据库时间精度。HTTP列表和当前推送均未输出request_time，不新增客户端时间字段。


### 12.3 GET `/fetch_unprocessed_friend_request`

无需 Body。查询当前用户作为接收者的 pending 申请，按创建时间及申请 ID 倒序。

```json
{
  "unprocessed_requests": [
    {
      "request_id": "efbd99da-b38f-4ee9-9c70-6a2ef96c2820",
      "id": "85467846-84e3-4bcb-b3f9-a4f5dd1cd492",
      "username": "alice_chat01",
      "display_name": "Alice",
      "message": "Hi Bob, I am Alice!"
    }
  ]
}
```

request_id 是申请 ID；id 是申请者用户 ID。处理申请时使用前者。
当前无申请返回 `{"unprocessed_requests":"None"}`，尚未统一为空数组。
成功为 200；当前捕获异常返回 503 `fetch_new_request_failed`。
已处理申请不在此列表，响应当前不包含申请时间。

### 12.4 POST `/process_friendship_request`

```json
{"request_id":"efbd99da-b38f-4ee9-9c70-6a2ef96c2820","process":"accepted"}
```

| process | 允许操作人 | 效果 |
| --- | --- | --- |
| accepted | 接收者 | 接受，并在同一事务建立双向好友关系 |
| rejected | 接收者 | 拒绝，不建立关系 |
| cancelled | 发起者 | 取消，不建立关系 |

不提交 requester_id 或 friend_id；双方 ID 从申请记录取得。
成功为 200：

```json
{"request_id":"efbd99da-b38f-4ee9-9c70-6a2ef96c2820","status":"accepted","already_processed":false}
```

重复同一操作返回 200、already_processed=true，不重复写入关系，也不恢复后来删除的关系。
同一申请的操作使用行锁串行处理；接受操作中任一数据库步骤失败则事务回滚。
连接断开时提交结果可能无法确认，应以同一 request_id 和 process 重试。

| HTTP | code | 条件 |
| --- | --- | --- |
| 400 | missing_request_body / invalid_json | Body 错误 |
| 400 | invalid_request_id | 缺失、非字符串、空或非法 UUID |
| 400 | invalid_process | 缺失、类型错误或不属于三个允许值 |
| 403 | friend_request_forbidden | 当前用户无权执行该操作 |
| 404 | friend_request_not_found | 申请不存在 |
| 409 | friend_request_already_processed | 申请已处于另一最终状态 |
| 503 | database_unavailable | 数据库连接错误 |
| 500 | process_friend_request_failed | 其他数据库错误 |

尚未接入实时通知、屏蔽或账号禁用校验。申请结果保留在数据库中，但本次未提供通知列表接口。

### 12.5 GET `/fetch_friends`

无需 Body、无需查询参数。只查询认证用户自己的好友，不允许通过提交别人的 user_id 改变查询身份。
从 app.friend_relation 关联 app.users 返回公开资料，按 username、用户 ID 排序。

成功为 200：

```json
{
  "friends": [
    {
      "id": "0776df58-9cbe-4f81-b613-4439f6c3b566",
      "username": "bob_chat01",
      "display_name": "Bob"
    }
  ]
}
```

无好友时返回 `{"friends":[]}`，不是 404，也不是字符串 "None"。
当前返回全部好友，不分页，不按 is_active 过滤已有关系；不返回邮箱、密码哈希或 token。
前提是好友表只包含已建立关系，不能遗留旧 pending 数据。

| HTTP | code | 条件 |
| --- | --- | --- |
| 401 | invalid_access_token / access_token_expired | 认证失败 |
| 503 | database_unavailable | 数据库连接错误 |
| 500 | fetch_friends_failed | 其他数据库错误 |

未被 Handler 捕获的其他异常仍由 HTTP 层转换为 500 `internal_server_error`。

### 12.6 Windows PowerShell 联调

以下不是 CMD 命令。反引号续行符后面不能有空格。使用一次性测试账号，不公开真实 token。

```powershell
# Bob 登录
$loginBody = @{
    email = "bob_chat01@example.com"
    password = "TestPass123@"
} | ConvertTo-Json -Compress

$login = Invoke-RestMethod -Method Post `
    -Uri "http://localhost:8082/login_by_email" `
    -ContentType "application/json" -Body $loginBody

$verified = Invoke-RestMethod -Method Post -Uri 'http://localhost:8082/device_verification' -ContentType 'application/json' -Body (@{
    login_ticket = $login.login_ticket
    device_id = '6b73d84c-f23f-4932-8b4c-d320913914e3'
} | ConvertTo-Json -Compress)
$bobToken = $verified.access_token

# 拉取待处理申请，复制所需的 request_id
Invoke-RestMethod -Method Get `
    -Uri "http://localhost:8082/fetch_unprocessed_friend_request" `
    -Headers @{ authorization = "Bearer $bobToken" } |
    ConvertTo-Json -Depth 10

# 用户确认后接受指定申请；替换为真实申请 ID
$processBody = @{
    request_id = "REPLACE_WITH_REQUEST_UUID"
    process = "accepted"
} | ConvertTo-Json -Compress

Invoke-RestMethod -Method Post `
    -Uri "http://localhost:8082/process_friendship_request" `
    -Headers @{ authorization = "Bearer $bobToken" } `
    -ContentType "application/json" -Body $processBody

# 拉取好友
Invoke-RestMethod -Method Get `
    -Uri "http://localhost:8082/fetch_friends" `
    -Headers @{ authorization = "Bearer $bobToken" } |
    ConvertTo-Json -Depth 10
```

验收应覆盖：无好友空数组、双方接受后互相可见、拒绝/取消不新增好友、
无 token 返回 401、不能通过 Body 读取他人列表、重复接受不产生重复关系。


## 13. 使用登录凭据登记新设备

### POST `/register_device`

请求体携带login_ticket，不再凭普通Bearer Token登记。用户由ticket记录确定，body.user_id即使提供也不参与身份判断。

```json
{
  "login_ticket": "64_LOWERCASE_HEX_CHARACTERS",
  "device_name": "Alice phone",
  "protocol_suite": "TEST-X25519-XEdDSA-HKDF-SHA256-v1",
  "identity_public_key_hex": "64_HEX_CHARACTERS",
  "signed_prekey_id": "42",
  "signed_prekey_public_hex": "64_HEX_CHARACTERS",
  "signed_prekey_signature_hex": "128_HEX_CHARACTERS"
}
```

示例公钥/签名为占位符。原字段校验保留：公钥32字节hex，签名64字节hex；signed_prekey_id为非负BIGINT十进制字符串；设备名和协议不能为空白。这里只验证编码长度，不代表完成密码学签名验证。

同一事务锁定并消费ticket、插入active设备与初始bundle、写入绑定设备的Refresh Token；任一步失败全部回滚。成功返回201：

```json
{
  "id": "85467846-84e3-4bcb-b3f9-a4f5dd1cd492",
  "device_id": "f7ab7d41-9955-4a73-922d-7b7a80ebdac4",
  "device_status": "active",
  "access_token": "ACCESS_TOKEN",
  "refresh_token": "REFRESH_TOKEN"
}
```

| 请求字段 | 类型与规则 |
|---|---|
| login_ticket | 非空字符串；真正消费前要求64个小写hex字符，且存在、未用、未过期 |
| device_name | 非空、非全空白字符串 |
| protocol_suite | 非空、非全空白字符串；尚无协议白名单 |
| identity_public_key_hex | 32字节公钥的64个hex字符，无前缀 |
| signed_prekey_id | 0至9223372036854775807的十进制字符串；不接受JSON数字 |
| signed_prekey_public_hex | 32字节公钥的64个hex字符 |
| signed_prekey_signature_hex | 64字节签名的128个hex字符 |

原字段的400错误使用invalid_字段名，例如invalid_signed_prekey_id。缺失/错误类型/空ticket为401 invalid_login_ticket；有ticket但公钥字段格式错误时，可能先返回400，而不进入ticket数据库校验。登记返回500 register_device_failed包含事务冲突等非连接错误，不等同于设备验证接口的503 retry_later映射。

不再仅返回device_id。客户端保存新设备ID及正式Token，然后上传一次性预密钥并连接WebSocket。一次ticket只能完成登记或验证之一；重放返回401 invalid_login_ticket，不会重复登记。新ticket重试登记仍可能新增设备，客户端需明确处理响应丢失。错误及部署要求见21节。

### 14. 上传一次性预密钥

`POST /upload_one_time_prekeys`，受保护路由；请求头 `authorization: Bearer <access_token>`。
设备必须属于令牌对应的用户且状态为 `active`。客户端保留私钥，只上传公钥。

```json
{
  "device_id": "c3939b6f-ee63-4448-b6ae-949a8bf47d49",
  "prekeys": [
    {"prekey_id": 1001, "public_key_hex": "06625dca527fc9b0fcad1acf661c5fed1e1cf85f25f403fa629b3e24b125d24a"}
  ]
}
```

每次 1～10 条；编号是 JSON 整数，范围 0～2147483647（当前 Repository 使用 C++ int）。
公钥是 64 个十六进制字符，表示 32 字节。示例公钥只演示格式，实际须使用客户端生成的一次性公钥。
同批编号不能重复；同设备已有编号不可覆盖或重新变为 available。
Repository 在同一事务中插入整个批次，失败全部回滚。

成功返回 `201`：
```json
{"device_id":"c3939b6f-ee63-4448-b6ae-949a8bf47d49","uploaded_count":1}
```

失败沿用 `{"error":{"code":"...","message":"..."}}`：

| HTTP | code | 原因 |
|---|---|---|
| 400 | missing_request_body / invalid_json | 请求体缺失或不是 JSON 对象 |
| 400 | invalid_device_id | 缺少 UUID 字符串或格式错误 |
| 400 | invalid_prekeys / invalid_prekey | 批次不是 1～10 条数组或条目不是对象 |
| 400 | invalid_prekey_id / invalid_public_key_hex | 编号或公钥格式错误 |
| 400 | duplicate_prekey_id | 同批编号重复 |
| 401 | 现有鉴权错误 | 未通过访问令牌验证 |
| 403 | device_unavailable | 设备不属于当前用户、不存在或不是 active |
| 409 | prekey_already_exists | 数据库已有同设备同编号 |
| 503 | database_unavailable | 数据库连接不可用 |
| 500 | upload_prekeys_failed | 其他内部错误 |

Windows PowerShell 调用（先设置 `$accessToken` 和实际登记返回的 `$deviceId`）：
```powershell
$body = @{
    device_id = $deviceId
    prekeys = @(@{
        prekey_id = 1001
        public_key_hex = "06625dca527fc9b0fcad1acf661c5fed1e1cf85f25f403fa629b3e24b125d24a"
    })
} | ConvertTo-Json -Depth 5
Invoke-RestMethod -Method Post -Uri "http://localhost:8082/upload_one_time_prekeys" -Headers @{ authorization = "Bearer $accessToken" } -ContentType "application/json" -Body $body
```

本接口验证格式和设备归属，不证明客户端持有对应私钥。插入事务会再次校验 active 设备归属及账号状态，并通过 FOR SHARE 锁与设备撤销互斥。


## 15. 领取本人或好友设备的一次性预密钥

`POST /get_one_time_prekey`，受保护路由，请求头 `authorization: Bearer <access_token>`。
此接口会消耗一条 available 预密钥，仅在建立新会话需要它时调用。

```json
{"friend_id":"3755178b-3b3d-4cf4-9e61-0aba62fb2875","device_id":"6b73d84c-f23f-4932-8b4c-d320913914e3"}
```

target_user_id（兼容 friend_id）是目标用户 UUID；device_id 必须是该用户的 active 设备。省略目标用户时默认本人。
调用者来自访问令牌，不接受请求体中的 my_id/user_id 作为调用者身份。
目标为别人时 Service 检查好友关系和设备归属，Repository 在同一事务内再次校验并锁定好友关系；目标为本人时仅跳过好友关系要求。两种情况均以 FOR SHARE 锁定目标设备与账号，要求 active。
然后用 FOR UPDATE SKIP LOCKED 选择一条 available 记录，UPDATE 标记 claimed 并写入 claimed_at。
提交成功之后才返回 HTTP；未通过权限检查不会领取预密钥。

200 响应：
```json
{"friend_id":"3755178b-3b3d-4cf4-9e61-0aba62fb2875","device_id":"6b73d84c-f23f-4932-8b4c-d320913914e3","prekey_id":1001,"public_key_hex":"810a4c056c3f9c06bc7befa1851916f55c6cfed29730f8a24efe503e2ede8511"}
```

| HTTP | code | 原因 |
|---|---|---|
| 400 | missing_request_body / invalid_json | 请求体缺失或不是 JSON 对象 |
| 400 | invalid_friend_id / invalid_device_id | UUID 字段缺失、类型或格式不正确 |
| 401 | 现有鉴权错误 | 访问令牌无效或过期 |
| 403 | not_friends | 调用者与目标用户没有好友关系 |
| 403 | device_unavailable | 目标设备不存在、归属不符、非 active 或双方账号已停用 |
| 404 | prekey_unavailable | 当前没有可领取的预密钥，包括剩余记录正在被其他事务锁定 |
| 503 | database_unavailable | 数据库连接不可用 |
| 500 | claim_prekey_failed | 其他内部错误 |

Windows PowerShell 示例：使用 Alice 设备验证/登记完成返回的 $verified.access_token，领取 Bob 的设备预密钥。
```powershell
$claimBody = @{
    friend_id = "3755178b-3b3d-4cf4-9e61-0aba62fb2875"
    device_id = "6b73d84c-f23f-4932-8b4c-d320913914e3"
} | ConvertTo-Json -Compress
Invoke-RestMethod -Method Post -Uri "http://localhost:8082/get_one_time_prekey" -Headers @{ authorization = "Bearer $($verified.access_token)" } -ContentType "application/json" -Body $claimBody
```

示例 friend_id 须替换成好友列表实际返回的账号 UUID（测试密钥文件中的账号编号不一定等于数据库账号编号）。
返回内容只有一次性公钥资料，并非完整会话公钥包；建立会话还需要身份公钥、带签名预密钥及其签名。
本接口没有请求幂等记录；重复调用会领取不同记录。响应丢失也不得将已 claimed 的记录重新投入库存。
当前没有领取限流，上线前须增加以防好友耗尽库存。


## 16. 拉取本人或好友的全部 active 设备

受保护接口 `POST /user_devices`，请求头 `authorization: Bearer <access_token>`。

- 查询本人：请求体 `{}`。
- 查询本人或好友：`{"target_user_id":"目标用户UUID"}`。
- 兼容旧字段 `friend_id`；两个字段同时传入时必须表示同一个 UUID。
- 旧路由 `POST /friend_devices` 保留，但仍要求显式传入目标字段。

调用者身份始终来自 Token。本人不需要好友关系；其他用户仍必须是好友。Repository 在查询中再次校验权限，要求账号 active，仅返回目标用户的 active 设备，不包含 pending/revoked。按 created_at、device_id 排序，不分页；没有有效设备时返回 `devices: []`。

列表可能包含当前设备，iOS 建立设备间加密会话时需要自行排除当前 device_id。

本人设备响应示例（UUID、公钥为占位值）：
```json
{
  "target_user_id": "本人用户UUID",
  "friend_id": "本人用户UUID",
  "is_own_device": true,
  "devices": [{
    "device_id": "本人另一台设备UUID",
    "user_id": "本人用户UUID",
    "is_own_device": true,
    "device_name": "我的iPad",
    "status": "active",
    "identity_public_key_hex": "64个十六进制字符",
    "protocol_suite": "TEST-X25519-XEdDSA-HKDF-SHA256-v1"
  }]
}
```
好友列表中 `is_own_device` 为 false。该字段由服务器计算，不接受客户端传入值作为权限依据。保留的 `friend_id` 是目标用户的兼容字段，即使查询本人也会返回。
列表不消耗一次性预密钥。

| HTTP | code | 原因 |
|---|---|---|
| 400 | missing_request_body / invalid_json | 必须提供 JSON 对象；查询本人也需要发送 {} |
| 400 | invalid_target_user_id / conflicting_target_user_id | 目标 UUID 格式、类型、非零校验失败，或两个目标字段冲突 |
| 400 | invalid_friend_id | 旧 /friend_devices 路由未指定目标 |
| 401 | 现有鉴权错误 | Token 无效或过期 |
| 403 | not_friends | 目标不是本人也不是好友 |
| 503 | database_unavailable | 数据库连接不可用 |
| 500 | fetch_user_devices_failed | 内部错误 |

PowerShell（按实际服务端口设置 baseUrl）：
```powershell
$baseUrl = "http://localhost:8082"
$headers = @{ Authorization = "Bearer $accessToken" }
$ownDevices = Invoke-RestMethod -Method Post -Uri "$baseUrl/user_devices" -Headers $headers -ContentType "application/json" -Body '{}'
$friendBody = @{ target_user_id = $friendId } | ConvertTo-Json -Compress
$friendDevices = Invoke-RestMethod -Method Post -Uri "$baseUrl/user_devices" -Headers $headers -ContentType "application/json" -Body $friendBody
$ownDevices | ConvertTo-Json -Depth 5
```

## 17. 获取本人或好友设备的带签名预密钥资料

受保护接口 `POST /get_device_key_bundle`，请求头 `authorization: Bearer <access_token>`。
```json
{"friend_id":"好友账号UUID","device_id":"好友设备UUID"}
```
调用者来自访问令牌。Repository 的查询要求目标是本人或好友、账号 active、设备属于目标用户且 active、bundle 是当前版本且未过期。
200 响应：
```json
{"friend_id":"好友账号UUID","device_id":"好友设备UUID","signed_prekey_id":42,"signed_prekey_public_hex":"64个hex字符","signed_prekey_signature_hex":"128个hex字符","is_current":true}
```
BYTEA 编码为小写 hex。本接口按当前 DeviceKeyBundle 类型返回带签名预密钥及签名，不含一次性预密钥，不会消耗库存。身份公钥和 protocol_suite 从 /user_devices 获取；当前类型未返回 key_version、published_at、expires_at。本响应单独不足以构成完整会话初始化包。
400: missing_request_body / invalid_json / invalid_friend_id / invalid_device_id；401: 现有鉴权错误；404: key_bundle_unavailable（统一表示关系、归属、状态或资料不可用）；503: database_unavailable；500: fetch_key_bundle_failed。

```powershell
$bundleBody = @{ friend_id = $friendId; device_id = $friendDeviceId } | ConvertTo-Json -Compress
$bundle = Invoke-RestMethod -Method Post -Uri "http://localhost:8082/get_device_key_bundle" -Headers @{ authorization = "Bearer $($verified.access_token)" } -ContentType "application/json" -Body $bundleBody
$bundle | ConvertTo-Json -Depth 5
```



两个密钥接口均支持 `target_user_id`（兼容 `friend_id`）；两个字段均省略时默认本人。例如 `{"device_id":"本人另一台设备UUID"}`。目标冲突返回 400 `conflicting_target_user_id`，格式/类型/非零校验失败返回 400 `invalid_target_user_id`。成功响应在原字段外增加 `target_user_id`、`is_own_device`，保留 `friend_id`；不会返回任何私钥。

## 18. WebSocket API（当前实现）

本节按当前 HttpSession、WebsocketWorker、WebSocketSession、WebSocketHandler、WebSocketDispatcher、ChatService 和 EnvelopeRepository 整理。它描述当前代码，而非最终规划。当前支持单目标信封存储、按设备游标分页同步、批量接收确认，以及握手和业务报文 Token 验证。支持接收设备在线推送，包括跨Worker投递；客户端处理见18.13。

### 18.1 地址、握手与身份验证

本地测试地址：`ws://127.0.0.1:8082/`。main 当前监听 0.0.0.0:8082。公网应通过已配置的 TLS 代理使用 WSS；本文未验证代理 WebSocket 路由配置。

标准客户端发送升级请求，例如：

```http
GET / HTTP/1.1
Host: localhost:8082
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13
Authorization: Bearer ACCESS_TOKEN
deviceID: 6b73d84c-f23f-4932-8b4c-d320913914e3
```

示例Key来自固定测试向量，实际客户端应每次生成新的16字节随机值并Base64编码。

```http
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

上面响应只展示关键头，不是完整的响应头集合。握手Accept通过客户端Key与WebSocket固定GUID拼接后SHA-1和Base64得到；它不是消息加密。

HttpSession 检测 Upgrade:websocket 后转交 Worker，不通过 HTTP Router 验证路径。Worker 在返回101之前验证请求头中的 Access Token；缺失或无效返回 HTTP401，JSON错误码为 invalid_access_token，过期为 access_token_expired，并关闭连接。Sec-WebSocket-Key 按头名称大小写不敏感查找且必须非空；当前没有完整检查方法、Connection、Version和Key长度。

每条业务JSON仍需包含 authorization 或 Authorization 字符串，值为精确前缀 Bearer 加一个空格及有效Token。两者同时存在优先小写字段；类型错误不会回退大写字段。握手验证不替代报文验证，连接已绑定验证后的user_id和device_id；握手必须另带deviceID头，X-Device-Id不能替代它。设备归属和有效状态在业务事务中验证。

### 18.2 Frame与JSON编码约定

| 项目 | 当前行为 |
|---|---|
| 业务帧 | FIN=1、opcode=Text(0x1)，payload是UTF-8 JSON对象 |
| 客户端mask | 必须有mask，未mask会作为协议错误处理 |
| 服务端mask | 不使用mask |
| RSV位/压缩 | RSV必须为0，未实现permessage-deflate |
| 单帧payload上限 | 1,048,576字节，包含整份JSON而不只是packet |
| 长度编码 | 支持7/16/64位长度，拒绝非最短编码及非法64位长度 |
| 分片 | 不支持FIN=0数据帧或Continuation，不能把一个JSON拆成多个Frame |
| TCP拆包 | 支持等待不完整Frame；它与WebSocket消息分片不同 |
| Binary | 可解析，但payload变为字节数组，无法通过当前业务对象检查 |
| 非JSON Text、数组、标量、null | 返回invalid_payload |

客户端直接发送应用JSON，不要自行套服务器内部的 `header/payload_format/payload` 包装。ApplicationParse只在服务端添加这些字段。未知type或缺少type不属于上表的“非对象”情况，见18.7。

### 18.3 正式存储：chat.newTextMessage

每次只提交一个目标设备。请求示例中的UUID是假定值，须替换为实际设备和客户端生成的消息编号：

```json
{
  "type": "chat.newTextMessage",
  "authorization": "Bearer ACCESS_TOKEN",
  "sender_device_id": "f7ab7d41-9955-4a73-922d-7b7a80ebdac4",
  "message_id": "4ffded1c-d415-4adb-a888-703116dc192e",
  "recipient_device_id": "6b73d84c-f23f-4932-8b4c-d320913914e3",
  "protocol_version": "TEST-OPAQUE-v1",
  "packet_base64": "AQID"
}
```

| 字段 | 类型 | 必须 | 规则 |
|---|---|---|---|
| type | string | 是 | 精确为chat.newTextMessage，区分大小写 |
| authorization | string | 是 | Bearer Token，也接受Authorization |
| sender_device_id | string | 是 | 36字符UUID，不可nil；实际归属由数据库验证 |
| message_id | string | 是 | 客户端稳定UUID，不可nil；不是数据库生成的envelope_id |
| recipient_device_id | string | 是 | 36字符UUID，不可nil，不能等于发送设备 |
| protocol_version | string | 是 | 非空、非纯空白、最多128字节；目前不做算法白名单或包内版本比对 |
| packet_base64 | string | 是 | 标准Base64，长度为4的倍数，不允许空白，需正确padding；解码后1～65536字节 |

Base64输入最多87,384字符；解码器还检查实际输出上限和完整消费输入。额外字段忽略。目前没有request_id、conversation_id、seq输入或envelopes批量数组；客户端即使传seq，也不会采用它。

AQID是测试字节01 02 03，不是有效加密包。服务器将Base64解码后原样存储BYTEA，不检查棘轮、公钥或认证标签，也不能证明接收端会解密。packet必须由客户端真实协议实现生成，才能进行实际E2EE通信。

### 18.4 保存权限与序号

服务端从Token获得user_id；客户端sender_device_id只是申请使用的设备，不是可信身份。Repository保存事务检查并锁定：发送设备属于该user_id、双方设备active、双方账号active、不同账号时两方向好友关系均存在。同账号的不同 active 设备可直接通信，不要求好友关系；禁止给当前设备自身投递。sender_device_id 必须等于当前 WebSocket 会话设备。

设备对排序后获得事务级advisory lock，seq按该设备对的MAX(seq)+1分配；A1→B1与B1→A1共享序列。不同设备对各自从1开始。seq是整数顺序，不是时间、不参与解密、也不是所有会话共用的同步水位。

数据库提交后才给发送端成功回复。没有发送给Bob的push，不需要Bob在线即可存储。部署前必须核对seq、外键和索引，见 [prepare-envelope-message-storage.sql](prepare-envelope-message-storage.sql)。message_id不再引用app.message(id)，否则可能发生23503并映射为internal_server_error。本文只改文档，不执行迁移。

### 18.5 成功、重复和冲突

新信封存储成功：

```json
{
  "type": "newTextMessage.result",
  "success": true,
  "duplicate": false,
  "message_id": "4ffded1c-d415-4adb-a888-703116dc192e",
  "envelope_id": "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
  "seq": 1
}
```

重复请求成功：

```json
{
  "type": "newTextMessage.result",
  "success": true,
  "duplicate": true,
  "message_id": "4ffded1c-d415-4adb-a888-703116dc192e",
  "envelope_id": "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
  "seq": 1
}
```

重试键为(sender_device_id, recipient_device_id, message_id)。已保存的protocol_version及解码后的packet完全相同才判为重复；返回原envelope_id/seq，不插入、不递增。不同内容返回envelope_conflict。message_id给不同目标设备属于不同请求范围。

success只表示持久化完成，不代表对方收到、解密或已读。权限先于重试识别，撤销设备/删除好友后重试仍可能被拒绝；拒绝不代表此前未保存。对同一设备对不要删除最高seq记录，否则MAX+1可能重用序号，历史清理需另设计水位。

### 18.6 失败响应：两种实际结构

路由前对象/鉴权错误不含success和message：

```json
{"type":"error","error":{"code":"missing_authorization"}}
```

| code | 触发情况 |
|---|---|
| invalid_payload | payload不是JSON对象 |
| missing_authorization | authorization/Authorization缺失或非字符串 |
| invalid_access_token | 字符串为空、Bearer前缀不符、签名/Token校验失败 |
| access_token_expired | Token过期 |

正式消息字段或业务错误：

```json
{
  "type":"newTextMessage.result",
  "success":false,
  "error":{"code":"send_permission_denied","message":"Sender or recipient is not permitted"}
}
```

| code | message/含义 |
|---|---|
| invalid_request | 字段解析时具体说明；Service校验失败时Invalid message fields |
| send_permission_denied | Sender or recipient is not permitted；设备、账号或好友权限不满足 |
| envelope_conflict | Message identifier already exists with different content |
| database_unavailable | Retry later with the same message identifier and packet |
| retry_later | Retry later with the same message identifier and packet；有限事务重试耗尽 |
| server_busy | Service task was not queued；正式消息任务入队失败 |
| internal_server_error | Missing storage result / Unable to save message / Unable to process message / Unable to parse message等安全描述 |

应用消息没有HTTP状态码。失败一般不带message_id/envelope_id/seq，也没有request_id关联字段；并行发送多个请求时不能假定失败可按UUID直接对应。message仅供展示，客户端逻辑应按code判断。失败不必然表示一定没有写入，尤其COMMIT连接故障；超时也不能证明未保存。

连接/数据库临时故障重试保持原message_id、目标、协议版本及packet；不要重新加密后沿用旧编号。Service对40001、40P01和识别到的主键UUID碰撞最多进行3次整事务尝试；其他数据库错误不直接透露SQLSTATE给客户端。envelope_conflict不能靠无脑换UUID绕过去。

### 18.7 测试路由与当前不完备行为

```json
{"type":"chat.newTextMessage_Test","authorization":"Bearer ACCESS_TOKEN","id":"test-1","message":"hello"}
```

```json
{"type":"newTextMessage.result","success":true,"id":"test-1"}
```

测试路由仅调用printId，不持久化信封、不分配seq，不验证sender_device_id或好友关系。id/message应为字符串；代码直接转换字段，未提供完整格式错误回复，异常可能只有线程池日志而没有应用响应。

未知type在switch的UnKnown分支直接return，**当前没有unsupported_type回复**。type缺失/非字符串也没有专门检查，可能触发JSON转换异常，由Worker边界记录日志，无可靠应用错误回复。客户端必须发送已知且格式正确的type，不应假定每种非法消息都收到结果。

### 18.8 Ping、Pong、Close与断线

| opcode | 当前处理 |
|---|---|
| Ping 0x9 | 立即排队同payload的Pong，不进入业务鉴权 |
| Pong 0xA | 清除等待Pong状态并记录活动；目前不核对payload对应关系 |
| Close 0x8 | 排队同payload的Close回复，然后关闭连接 |

控制帧必须FIN=1、payload≤125字节；Close长度为1非法。网络协议错误、无mask、非法RSV、超长或不支持分片会走协议错误关闭路径，不能期待JSON错误报文。

服务端每5秒检查一次心跳。空闲超过30秒且没有等待Pong时发送空payload Ping；发送后等待Pong达到10秒即移除连接。检查粒度会使实际时刻延后。普通消息可以刷新活动时间，但不替代已经等待的Pong确认；客户端应响应真正的WebSocket Ping，不能发送JSON字符串“pong”代替。

回复被投递回原连接Worker，并核对fd和session_id以避免fd重用误投。若连接已关闭，结果可能无法送达；使用稳定消息编号重试，而不是推断未保存。

### 18.9 客户端推荐调用顺序与未实现项

HTTP登录→取得login_ticket→验证/登记设备→保存access_token→确认Alice发送设备有效→fetch_friends获取Bob实际id→friend_devices获取目标设备→按需获取bundle/prekey并在客户端加密→WebSocket升级→发送chat.newTextMessage→处理result或error。Token过期后通过现有HTTP刷新/登录取得新Token，下一条报文使用新Token；没有WebSocket refresh路由。

接收流程：HTTP登录→带Token握手→发送 chat.sync.envelope→持久化保存信封及对应设备游标→发送 chat.envelopes.ack→核对 acknowledged_ids→has_more=true 时携带更新后的全部游标继续同步。未知发送设备不必预先加入 cursors。

已实现在线设备目录和跨Worker推送。尚未实现批量提交信封和已读/解密确认；设备撤销已增加全 Worker 关闭与有界完成确认，见第 19 节。设计文档中的其他示例类型不是当前路由的别名。

当前Worker仍会打印包含authorization的应用报文；日志脱敏不属于本次文档更新。

### 18.10 分页同步信封：chat.sync.envelope

请求：

```json
{
  "type": "chat.sync.envelope",
  "authorization": "Bearer ACCESS_TOKEN",
  "device_id": "947b5e0c-4a11-56d9-8eb0-d16f22edfe24",
  "cursors": [
    {"peer_device_id": "f7ab7d41-9955-4a73-922d-7b7a80ebdac4", "after_seq": 12}
  ]
}
```

| 字段 | 规则 |
|---|---|
| device_id | 当前用户的有效接收设备，非零UUID字符串 |
| cursors | 必填数组，首次同步传[]，最多1000项 |
| peer_device_id | 发送方设备非零UUID，同一请求不能重复，不能等于device_id |
| after_seq | 0到9223372036854775807的JSON整数；不接受字符串、浮点数或负数 |

服务端只返回 recipient_device_id=device_id 且 seq 大于该发送设备 after_seq 的信封。未提供的发送设备默认 after_seq=0，因此可发现第一次发消息的设备；显式0也会拉取该设备全部已存信封。同步验证账号有效、设备active且属于认证用户，不再次要求当前好友关系，避免删除好友后无法同步已接受的消息。

成功响应：

```json
{
  "type": "chat.sync.envelope.result",
  "success": true,
  "device_id": "947b5e0c-4a11-56d9-8eb0-d16f22edfe24",
  "count": 1,
  "has_more": false,
  "envelopes": [
    {
      "envelope_id": "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
      "message_id": "4ffded1c-d415-4adb-a888-703116dc192e",
      "sender_device_id": "f7ab7d41-9955-4a73-922d-7b7a80ebdac4",
      "recipient_device_id": "947b5e0c-4a11-56d9-8eb0-d16f22edfe24",
      "protocol_version": "TEST-OPAQUE-v1",
      "packet_base64": "AQID",
      "seq": 13
    }
  ]
}
```

count等于本页envelopes数量；无消息也是success=true、count=0、envelopes=[]、has_more=false。固定最多100条，并按估算的Base64 JSON大小约512KiB限制页大小，不能指定limit。按sender_device_id、seq、envelope_id排序，不是跨用户时间线。packet_base64为完整协议包的标准Base64，示例AQID仅为测试字节。当前不返回created_at、received_at或decrypted_at。

has_more=true时，保存本页后，对每个发送设备将游标推进到本页已保存的最大seq，保留其他设备游标，再请求下一页。seq在双向设备对中共享，所以接收序号可以有空洞；不要用自己发送成功的seq推进接收游标。has_more=false表示本次查询没有更多匹配消息，不代表以后没有新消息。

同步不自动设置received_at，也不按received_at筛选；同一组游标可重复返回已确认的信封，客户端以envelope_id去重。客户端应将信封及游标可靠持久化，崩溃后恢复未完成的ACK；ACK成功不会推进服务器同步游标。

### 18.11 批量接收确认：chat.envelopes.ack

客户端持久化保存后发送；确认收到不等于解密成功或已读。

```json
{
  "type": "chat.envelopes.ack",
  "authorization": "Bearer ACCESS_TOKEN",
  "device_id": "947b5e0c-4a11-56d9-8eb0-d16f22edfe24",
  "envelope_ids": ["eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"]
}
```
```json
{
  "type": "chat.envelopes.ack.result",
  "success": true,
  "device_id": "947b5e0c-4a11-56d9-8eb0-d16f22edfe24",
  "count": 1,
  "acknowledged_ids": ["eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"]
}
```

envelope_ids必填，每次1–100个不重复的非零UUID字符串。事务验证设备属于认证用户、设备active且账号有效，然后只更新发给该设备的信封。received_at使用服务端CURRENT_TIMESTAMP；COALESCE保留首次时间。事务提交后才回复成功，重复确认仍返回匹配ID，不修改decrypted_at。

不存在或属于其他设备的ID不会更新，也不会返回；count可能为0，部分匹配仍success=true。acknowledged_ids顺序不保证与请求一致，客户端按集合核对，不能只看success。数据库故障或结果不确定时用相同ID重试。历史确认不再要求好友关系。

### 18.12 同步与确认错误

```json
{"type":"chat.envelopes.ack.result","success":false,"error":{"code":"ack_permission_denied","message":"Device must belong to the authenticated user and be active"}}
```

同步失败type为chat.sync.envelope.result，确认失败type为chat.envelopes.ack.result；均含success=false及error.code/message。路由前鉴权错误仍使用18.6中的type=error结构。

| code | 适用及含义 |
|---|---|
| invalid_request | 两接口：字段缺失、类型、范围或重复ID不合法 |
| sync_permission_denied | 同步：设备不属于用户、设备非active或账号无效 |
| ack_permission_denied | 确认：同上 |
| database_unavailable | 两接口：数据库连接不可用；稍后重试同一请求 |
| retry_later | 两接口：事务冲突或死锁；重试原游标/确认ID |
| internal_server_error | 两接口：其他存储或处理异常 |
| server_busy | 两接口：任务未能入队 |

两个接口没有request_id，建议同一连接按请求—响应顺序执行同步及确认，避免同时请求时无法关联错误。

### 18.13 在线推送：客户端接收与处理

#### 建立接收连接

客户端先通过HTTP登录取得login_ticket，再验证/登记设备取得access_token和device_id，再连接本地 `ws://localhost:8082/`；公网使用部署的WSS入口，例如 `wss://aittelegramapi.uk/`。握手请求必须带 `Authorization: Bearer ACCESS_TOKEN` 和 `deviceID: 当前设备UUID`。当前代码读取的是deviceID头，不是X-Device-Id；经过代理时需确保该头被转发。 请求头名称按大小写不敏感比较，推荐统一写deviceID；旧device_id头不再作为握手设备头。登录、发送、同步和ACK的JSON字段仍使用device_id或各接口定义的sender_device_id/recipient_device_id，不随请求头改名。

服务器验证Token得到user_id，再验证设备归属、active状态及账号有效性，成功后创建绑定user_id/device_id的Session并登记在线目录。客户端不传user_id作为身份依据。Token错误返回401；设备不存在、非active/账号无效、归属错误分别返回403及device_not_found、device_not_active、device_not_owned。缺失或格式错误的device_id以及数据库异常，目前只由握手异常边界记录并关闭，不能保证有结构化HTTP错误响应。

客户端保持一个持续接收循环；发送、同步和ACK期间也不能停止接收。使用WebSocket库处理Ping/Pong，定时重连不能代替控制帧响应。

#### 实际推送报文

服务器在新信封事务提交成功后主动发送下列Text JSON报文，不要求接收者先发同步请求：

```json
{
  "type": "Online_Sync",
  "success": true,
  "duplicate": false,
  "recipientDeviceId": "6b73d84c-f23f-4932-8b4c-d320913914e3",
  "seq": 13,
  "envelope": {
    "message_id": "4ffded1c-d415-4adb-a888-703116dc192e",
    "envelope_id": "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
    "sender_device_id": "f7ab7d41-9955-4a73-922d-7b7a80ebdac4",
    "recipient_device_id": "6b73d84c-f23f-4932-8b4c-d320913914e3",
    "protocol_version": "TEST-OPAQUE-v1",
    "packet_base64": "AQID",
    "seq": 13
  }
}
```

| 字段 | 客户端解释 |
|---|---|
| type | 当前发送值严格为Online_Sync；内部Worker也识别online_sync，但服务器不会主动改成该值 |
| success / duplicate | 新存储信封推送当前固定true / false，不代表已收到、解密或已读 |
| recipientDeviceId | 顶层接收设备ID，与envelope.recipient_device_id一致 |
| seq | 顶层序号，与envelope.seq一致 |
| envelope.envelope_id | 信封唯一ID，客户端本地去重键和ACK标识 |
| envelope.message_id | 发送者重试标识；不要用它代替跨设备信封去重键 |
| envelope.sender_device_id / recipient_device_id | 发送及接收设备；接收设备必须等于当前连接的本地设备 |
| envelope.protocol_version | 客户端选择协议解析方式的版本 |
| envelope.packet_base64 | 标准Base64编码的完整协议字节包；AQID为测试字节，不是可解密示例 |
| envelope.seq | 双向设备对中的顺序号，不是全局序号或棘轮计数 |

推送不包含access_token、私钥、明文、received_at、decrypted_at。数据库提交成功后，发送者另收到newTextMessage.result，接收者收到Online_Sync；两者是不同目标的报文。重复提交同一消息目前只给发送者返回duplicate=true，不再次推送。

#### 接收循环按type分发

| type | 处理 |
|---|---|
| Online_Sync | 读取单个envelope，进入本地收信流程 |
| chat.sync.envelope.result | 成功时逐个处理envelopes，随后处理分页及游标 |
| newTextMessage.result | 更新本机发送状态；不是对方收到的证明 |
| chat.envelopes.ack.result | 按acknowledged_ids清理待确认记录 |
| error | 根据error.code处理Token/公共格式错误 |

在线推送可能穿插在同步或ACK响应之间。不能将“发送请求之后收到的下一条JSON”直接当作该请求响应；必须先按type分发。当前没有request_id，建议同步和ACK请求各保持清晰的顺序，避免多个同类型请求并发无法关联错误。

#### 保存、去重、解密和确认

1. 验证JSON字段和目标设备，按envelope_id检查本地是否已保存。
2. Base64解码packet_base64得到原始字节。与同步信封使用同一个保存/处理函数，避免两套行为。
3. 在本地事务中可靠保存完整信封及待ACK记录；已存在的信封不重复展示或重复执行棘轮解密，但可再次确认。
4. 根据协议和sender_device_id找到会话，必要时按首次消息协议创建会话；服务器不解析或建立客户端加密会话。协议可能允许解密乱序，必须使用协议库规则。
5. 保存成功后发送ACK，可立即确认单封，也可合并1–100封；无需等待解密成功。仅保存到内存或仅收到网络帧时不要确认。

```json
{
  "type": "chat.envelopes.ack",
  "authorization": "Bearer ACCESS_TOKEN",
  "device_id": "6b73d84c-f23f-4932-8b4c-d320913914e3",
  "envelope_ids": ["eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"]
}
```

收到chat.envelopes.ack.result后按集合检查acknowledged_ids，只删除已确认项的本地待ACK记录。失败、超时或断线时保留记录并用相同ID重试；重复确认不会改变首次received_at。解密失败可保留密文并记录错误，received_at不是解密成功证明。

#### 推送不能直接推进同步游标

在线推送和历史同步可能交错，而且并发消息的推送入队顺序不保证与seq一致。比如本地同步游标是12，先收到推送15，数据库中可能还有尚未保存的13、14；直接把after_seq改成15会跳过它们。

建议分别保存“本地已收到信封”和“通过分页同步确认的水位”。在线推送可保存、展示和ACK，但不要仅按推送最大seq推进同步水位。连接建立及重连后仍从原水位执行18.10的同步，去重后按已完成处理的页面推进游标。seq可能有来自自己发送方向的空洞，不能简单要求所有整数连续，也不能用发送回执推进接收水位。

#### 断线、重连与服务端保证

在线目录通过Pool定位目标Worker，发送Task在目标Worker内检查fd、session_id和设备ID后进入输出缓冲。接收者离线、连接正在关闭或任务执行时断线，推送可被丢弃；不会回滚已提交信封，也不会自动设置received_at。即使发送者已拿到成功回执，接收者也可能需要通过同步补取。

客户端启动/重连顺序：取得有效Token→带deviceID请求头握手→立即开启接收循环→从持久化游标分页同步→去重保存→发送/重试ACK。以后有新消息时实时处理推送，并在需要补齐时再次同步。

当前同一设备只有一个目录索引，新连接覆盖旧索引；旧连接不会因此自动被关闭，不保证向同一设备所有连接广播。这是单在线目标策略，不是多连接订阅。连接Token过期目前不主动触发断线，但同步/ACK等请求仍验证Token；通过HTTP刷新并为后续请求提供新Token，重连也使用新Token。撤销现已向所有 Worker 请求关闭同设备连接并有界等待，重复撤销也会重新关闭。main 已完成依赖接线，详见第 19 节的 200/202 与 websocket_closed 语义。

## 19. 撤销设备

### POST /revoke_device

受保护接口，要求 `Authorization: Bearer ACCESS_TOKEN`。用户身份来自 Token，不接受请求体指定 user_id。当前仅撤销本人 active 设备，不删除设备、历史密钥或信封。

请求字段区分大小写：这里保留 `deviceID`，不是 `device_id`；响应沿用设备接口的 `device_id`。

```json
{
  "deviceID": "6b73d84c-f23f-4932-8b4c-d320913914e3",
  "password": "YOUR_CURRENT_PASSWORD"
}
```

password 必须是非空字符串，按原值验证，不去除空格；deviceID 必须是标准带连字符的非零 UUID。额外字段忽略。

撤销且服务端关闭已确认返回 200；尚未确认关闭返回 202。Content-Type 为 application/json：

```json
{
  "device_id": "6b73d84c-f23f-4932-8b4c-d320913914e3",
  "status": "revoked",
"already_revoked": false,
  "websocket_closed": true
}
```

已撤销设备再次请求仍需验证密码和归属，返回相同结构、already_revoked=true，不改变首次 revoked_at。超时或提交结果不确定时，可用相同设备 ID 重试。

| HTTP | error.code | 含义 |
| --- | --- | --- |
| 400 | missing_request_body | 请求体为空 |
| 400 | invalid_json | 无效 JSON 或不是对象 |
| 400 | missing_password | 密码缺失、非字符串或空 |
| 400 | missing_deviceID | deviceID 缺失、非字符串或空 |
| 400 | invalid_deviceID | UUID 格式错误或全零 |
| 401 | invalid_access_token / access_token_expired | 由认证中间件拒绝，未进入 Handler；过期分类受现有验证器实现影响 |
| 403 | invalid_password | 密码验证失败 |
| 403 | device_not_owned | 设备不属于当前用户 |
| 404 | user_not_found | 没有对应密码登录记录 |
| 404 | device_not_found | 设备不存在 |
| 409 | device_not_active | pending 等非 active、非 revoked 状态 |
| 503 | database_unavailable | 数据库连接故障，提交结果可能不确定 |
| 503 | retry_later | 事务序列化冲突或死锁，可重试 |
| 500 | revoke_device_failed | 其他数据库或内部错误 |

错误响应使用第 3 节的统一 error 对象。格式检查通过后先验证密码，再判断设备归属和状态。

#### 并发和当前限制

撤销事务锁定设备行并更新 status/revoked_at；预密钥上传和信封写入在事务内锁定设备并检查 active。已先取得锁的写入可以在撤销之前完成；撤销提交后的新操作不能通过状态检查。这不是禁止任意直接 SQL 写入的数据库触发器。

**本接口不是完整的退出登录或远程踢下线。** Refresh Token 尚未绑定设备，不会在此撤销；已签发 Access Token 不会立即失效。撤销会等待本进程所有 Worker 确认关闭，200 表示服务端关闭已确认，202 表示尚未确认；关闭前已缓冲的数据可能到达；账号级接口和新设备注册仍可能接受旧 Token。新握手和有设备状态检查的业务会拒绝已撤销设备。

#### Windows PowerShell 测试

只对测试设备执行，撤销不可通过本接口恢复。以下示例使用域名；直连当前 main 可改为 http://localhost:8082。

```powershell
$baseUrl = "https://aittelegramapi.uk"
$token = Read-Host "Access Token"
$deviceId = Read-Host "Test device UUID to revoke"
$securePassword = Read-Host "Current password" -AsSecureString
$password = [System.Net.NetworkCredential]::new("", $securePassword).Password
$body = @{ deviceID = $deviceId; password = $password } | ConvertTo-Json -Compress
try {
    Invoke-RestMethod -Method Post -Uri "$baseUrl/revoke_device" -Headers @{ Authorization = "Bearer $token" } -ContentType "application/json" -Body $body
    # 重复请求：预期 already_revoked=true。
    Invoke-RestMethod -Method Post -Uri "$baseUrl/revoke_device" -Headers @{ Authorization = "Bearer $token" } -ContentType "application/json" -Body $body
} finally {
    Remove-Variable password, body, token, securePassword -ErrorAction SilentlyContinue
}
```

验收还需覆盖：空请求体、坏 JSON、非法 UUID、错误密码、他人设备、不存在设备、pending 设备、数据库失败，以及撤销与预密钥上传/消息写入的并发。本文示例未对在线服务执行。

### 19.1 关闭完成结果与调用链

首次成功和重复撤销均执行：数据库确认 revoked → Pool 向所有 Worker 投递任务 → 每个 Worker 扫描本人该设备的全部 Session → 停止业务收发 → 发送 Close 帧 → 关闭连接并完成 promise → Pool 汇总结果 → HTTP 响应。

main 已调整构造顺序并传入 Pool 引用。Pool 等待时不持有自己的锁；避免 Worker 清理在线索引时死锁。无匹配连接也视为已关闭。

- HTTP 200：status=revoked，websocket_closed=true；所有 Worker 确认匹配连接已从服务端清除，或本来没有连接。
- HTTP 202：status=revoked，websocket_closed=false；撤销已提交，但关闭因停止、投递失败或等待超时尚未确认。不得继续使用该设备；可以用相同设备 ID 和密码重试，重复撤销也会重新发起关闭。
- Pool 共享等待上限为 8 秒（任务投递之外）。Worker 为慢连接设置 2 秒关闭期限，由现有 5 秒定时器检查；正常事件调度下可能到下一次定时检查才强制关闭，Worker 阻塞时不保证准时。
- Close 采用应用私有码 4001，UTF-8 reason 为 device_revoked。帧先加入输出缓冲，排空后关闭 socket；不是等待客户端登出 ACK 的完整远程注销协议。超时、网络故障或本来已进入其他关闭流程时，原因不保证送达。
- 已有同设备旧连接也会扫描关闭，而非只处理在线索引里最新一条。任务与握手在同一 Worker 串行执行；已在执行的握手会先登记再被扫描，之后的握手依赖已提交的数据库 revoked 检查。
- 关闭期间拒绝新的业务读取、推送和普通业务回复；撤销前已提交业务不回滚，原输出缓冲的数据可能先于 Close 帧送达。
- websocket_closed 仅确认本进程服务端连接清理；不确认 iOS 已收到原因、已退出或已删除凭证。运行多个后端实例时，尚无跨实例通知机制。

关闭未确认示例（202）：

```json
{"device_id":"6b73d84c-f23f-4932-8b4c-d320913914e3","status":"revoked","already_revoked":false,"websocket_closed":false}
```

### 19.2 iOS 必须执行的登出流程

**被撤销的 iOS 设备在确认撤销且 WebSocket 已关闭或本地已终止后，必须 log out，删除本次登录会话的 Access Token 和 Refresh Token，包括内存及 Keychain 等持久化存储。**

- 本机发起撤销：200/202 中 status=revoked 都代表设备已撤销。立即停止发送、刷新、重连和自动登录；200 确认服务端关闭，202 则由客户端有界等待并主动取消本地连接，不能无限等待或继续用旧 Token。
- 接收 Close code=4001、reason=device_revoked：对对应连接的当前登录会话执行本地终止和凭证清理，提示“设备已撤销，请重新登录”。不得让旧连接回调清理后来新登录的会话。
- A 撤销 B：HTTP 响应只给 A；不得清理 A 的凭证。B 通过自己的关闭通知处理；A 只能显示服务器连接清理结果，不能声称 B 已清除凭证。
- Close 原因丢失时，普通断线不能视为撤销。需要通过明确的设备身份拒绝恢复判断；当前握手 device_not_active 同时覆盖多种无效状态，且未验证 iOS 能否读取失败握手 JSON。专用状态查询仍未实现。
- 停止接收任务并释放本地 WebSocket；删除两种 Token 和请求头缓存；取消刷新/重连任务；用会话代次阻止在途刷新重新写入 Token；然后显示登录页。凭证删除失败应记录、重试，且不得恢复使用。
- 可以在确认撤销时提前清理 Token。不要自动删除聊天历史或加密私钥；这是独立的数据保留策略。
- B 离线/未运行时不能立即执行本地删除。客户端本地清理不等于服务端 JWT 或 Refresh Token 撤销，后者仍未实现设备绑定。

验收：两条同设备连接都关闭、重复撤销、设备本来离线、慢客户端强制关闭/202、安全的 fd 复用、HTTP 与断线顺序不同、普通网络故障不误退出、A 撤销 B 不影响 A、迟到刷新不能恢复凭证。构建和真实双端验收仍待执行。

## 20. 同账号设备的加密会话与收发

1. A1 调用 POST /user_devices，传 {}，获取本人 active 设备并排除 A1 自身。
2. 针对 A2 获取 bundle，仅在需要建立新会话时领取预密钥。iOS 验证公钥资料并在本地建立 A1↔A2 的加密会话。这不是再与 A2 建立 WebSocket：每台设备仍用自己的 deviceID 连接后端。
3. A1 为 A2 单独加密，用 chat.newTextMessage 投递，sender_device_id=A1、recipient_device_id=A2。服务端不会重加密或自动广播。给 Bob 发送并同步给自己的 A2，需要分别生成目标设备密文。原有幂等键和设备对 seq 规则不变。
4. 在线仍通过 Online_Sync 按目标设备推送。离线 chat.sync.envelope 和 chat.envelopes.ack 仍仅操作当前收件设备的信封，同账号发来的信封自动包含其中；不是按 user_id 拉取所有设备的信封。
5. 发送的 sender_device_id、拉取和 ACK 的 device_id 必须与当前 WebSocket 会话设备相同。不匹配返回对应 result 类型、success=false、error.code=session_device_mismatch；缺失/格式错误返回 invalid_request。A1 不能冒用 A2 的身份拉取或确认。
6. iOS 可用 is_own_device 区分自身同步与好友消息，但仍需正常验证密钥并解密，避免将同步消息循环转发。本功能不自动迁移旧聊天历史；pending/revoked 设备不开放收发。

部分 HTTP 接口已实测，详情见下节；真实双设备 WebSocket、消息投递与密码学验收仍待执行。隔离测试及完整验收清单见 [测试说明](../tests/message_storage_isolated/README.md)。

### 20.1 设备接口实测记录（2026-10-16）

依据：用户提供的 Windows PowerShell 运行输出，服务地址为 `http://localhost:8082`，Alice 登录成功，使用 [user_devices_test.ps1](../tests/user_devices_test.ps1)。结果：**PASS=87、FAIL=0、SKIP=3**。87 是断言数量，不是 HTTP 请求数；本记录不代表代理重新运行了测试，也不证明隔离 C++ 测试或完整构建已通过。

已验证：

- 默认本人查询、显式 target_user_id 查询、旧 /friend_devices + friend_id 查询、两个相同目标字段查询均返回 200。
- 四种查询各返回 4 台设备；返回设备的用户归属、active 状态、UUID 唯一性、顶层及逐项 is_own_device=true、公钥 hex 格式和非空协议名称均通过断言。
- 冲突目标字段返回 400 conflicting_target_user_id。
- 非法 UUID、全零 UUID、数字类型目标返回 400 invalid_target_user_id。
- 随机未知目标返回 403 not_friends；旧路由缺失目标返回 400 invalid_friend_id；缺失 Token 返回 401。

| 跳过项 | 原因及下一步 |
|---|---|
| 好友设备列表 | 未传 FriendUserId；提供实际已建立好友关系的用户 UUID 后验证 is_own_device=false |
| 本人设备 bundle | 脚本选中的第一台设备返回 key_bundle_unavailable；需准备当前且未过期的 bundle。不能据此认定其他设备都无 bundle，也不能确定具体是哪项查询条件未满足 |
| WebSocket 会话绑定 | 未传 SessionDeviceId；提供 Alice 的实际 active 设备 UUID，验证发送、拉取、ACK 的身份冒用被拒绝 |

复测（在 Windows PowerShell 中执行文件，不要把 param/try/catch/finally 分段粘贴）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "D:\projects\cpp_backend_server\tests\user_devices_test.ps1" -BaseUrl "http://localhost:8082"
```

追加好友与会话隔离测试：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "D:\projects\cpp_backend_server\tests\user_devices_test.ps1" `
  -BaseUrl "http://localhost:8082" `
  -FriendUserId "0776df58-9cbe-4f81-b613-4439f6c3b566" `
  -SessionDeviceId "替换为Alice的active设备UUID"
```

前提是该 Bob UUID 在当前数据库中存在且与 Alice 已是好友。密码通过交互输入，不写入文档或输出 Token。

验证边界：返回项均为 active 不等于已用 pending/revoked 测试数据验证过滤。密钥真实性、一次性预密钥领取、成功投递、Online_Sync、离线拉取、ACK 持久化、重复投递及撤销竞争仍需独立验收。脚本本身不会注册/撤销设备、消耗预密钥或写入消息，不验证完整端到端加密。

## 21. login_ticket完整流程、设备验证及重试

### 21.1 三类凭据与顺序

| 凭据 | 来源 | 用途 | 生命周期 |
|---|---|---|---|
| login_ticket | POST /login_by_email | 完成一次已有设备验证或新设备登记 | 5分钟，一次性 |
| access_token | POST /device_verification或/register_device；之后可刷新 | 普通受保护HTTP接口、WebSocket握手及业务报文 | 当前15分钟 |
| refresh_token | 设备登录完成 | GET /refresh_token换取Access Token | 当前7天，绑定设备；刷新不延期 |

新账号：POST /register_by_email→POST /login_by_email→POST /register_device→保存Token及device_id。已有设备：POST /login_by_email→POST /device_verification→保存新Token。ticket只放两个完成接口的JSON请求体，不放URL，也不作为普通Bearer Token。只有user_id或device_id不是身份凭据。

### 21.2 POST `/device_verification`

公开路由，内部强制ticket认证；旧GET方式不再注册。请求：

```json
{
  "login_ticket": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "device_id": "f7ab7d41-9955-4a73-922d-7b7a80ebdac4"
}
```

ticket示例为格式占位，不能直接使用。device_id必须为标准36字符非零UUID字符串；缺失、非字符串、格式不合法、nil都返回400 invalid_device_id。服务器从ticket取user_id，忽略body.user_id，验证设备归属、状态及账号。成功200返回：

```json
{
  "id": "7a29cba5-cfd4-41c2-b805-2f9782d5dffc",
  "device_id": "f7ab7d41-9955-4a73-922d-7b7a80ebdac4",
  "device_status": "active",
  "access_token": "ACCESS_TOKEN",
  "refresh_token": "REFRESH_TOKEN"
}
```

新设备登记返回同结构但HTTP201。两个接口不再需要额外GenerateTokenPairs调用，也不接收账号密码。

### 21.3 完成接口错误

```json
{"error":{"code":"invalid_login_ticket","message":"Login ticket is invalid, expired or already used"}}
```

| HTTP | code | 适用/处理 |
|---|---|---|
| 400 | missing_request_body / invalid_json | 两完成接口，修正请求 |
| 401 | invalid_login_ticket | 两接口，缺失/格式/未知/过期/重放统一；过期或已用时重新密码登录 |
| 403 | account_disabled | ticket关联账号不可用 |
| 400 | invalid_device_id | 设备验证的UUID错误 |
| 404 | device_not_found | 设备验证：设备不存在，可在ticket仍有效时选择登记新设备 |
| 403 | device_not_owned | 设备验证：不属于该ticket用户，不能靠改body.user_id绕过 |
| 403 | device_not_active | 设备验证：非active；不自动恢复撤销设备 |
| 503 | database_unavailable | 设备验证：连接类/08开头SQLSTATE；登记：Connection类；结果可能不确定 |
| 503 | retry_later | 仅设备验证：40001/40P01，可重试原ticket |
| 500 | verification_device_failed | 设备验证其他异常 |
| 500 | register_device_failed | 登记其他异常；保留13节字段400规则 |

不同接口Handler错误映射不完全相同，客户端读取HTTP和code，不依赖message逐字匹配；多个字段错误时只返回先检测到的一个。

### 21.4 原子性、并发和响应丢失

ticket为256位随机数，数据库保存带login-ticket-v1用途前缀的BLAKE2b哈希，原文只返回客户端。LoginTicketRepository在事务中锁ticket行FOR UPDATE，从记录取可信用户，锁账号/设备并检查active，在取得锁后用clock_timestamp复查过期，标记消费，再写设备/bundle（仅登记）和绑定设备的Refresh Token。RefreshTokenRepository::insertRefreshToken接收同一个DatabaseConnection，不重新借连接、不提交。数据库失败回滚整组写入。

校验失败一般不消费ticket；格式不合法的HTTP请求也不会触发消费。并发提交同一ticket至多一次成功；过期即使未清理也不能使用。成功只表示事务已提交，不代表客户端已保存Token。

提交成功后响应丢失，再用同ticket返回401；连接类错误可能无法判断是否已提交。客户端重新验证密码取得新ticket。已有设备继续验证同一个device_id；新设备因UUID由服务器生成，丢失登记响应不具有恢复或幂等重放接口，新ticket再次登记可能产生另一个设备，禁止盲目无限重试。Access Token签名在提交后生成，签名失败同样需要重新登录。不要把捕获到500/超时视为事务一定未提交。

### 21.5 iOS接入顺序

1. 登录成功只保留短期ticket，不能此时连接WebSocket或上传预密钥。
2. 本机有device_id时POST验证；没有时本地生成并保存真实私钥、公钥资料，POST登记。
3. 成功后可靠保存正式Token和device_id；普通接口用access_token，刷新用refresh_token。
4. WebSocket使用Authorization和deviceID握手头，业务JSON仍按18节带设备字段及有效Token；ticket不参与后续消息。
5. 设备撤销会撤销其Refresh Token并关闭连接；已签发JWT不是立即全局失效，不能把签名验证成功当作设备仍有效。

当前登记仍只验证密钥编码/长度，不证明签名密码学有效；测试随机占位公钥不适合真实加密。ticket不是邮件验证码或设备私钥所有权证明；它证明持有人刚通过密码检查。生产使用TLS，不记录凭据原文。

[部署与登录500排查](#22-部署迁移授权和登录500排查)

## 22. 部署迁移、授权和登录500排查

### 22.1 数据库前置条件

后端实际使用的每个数据库都必须应用 [002_login_tickets.sql](../database/migrations/002_login_tickets.sql)。已有users、user_devices、device_key_bundles和refresh_tokens为前置表。迁移增加/保证：

| 对象 | 规则 |
|---|---|
| app.login_tickets.ticket_hash | TEXT主键，只允许64个小写hex字符；保存哈希而非ticket原文 |
| user_id | UUID非空，引用app.users(id) |
| created_at / expires_at | TIMESTAMPTZ非空，expires_at晚于created_at |
| consumed_at | TIMESTAMPTZ可空，非空表示已消费 |
| login_tickets_expiry_idx | 按expires_at的维护索引 |
| refresh_tokens.device_id | UUID非空，与user_id构成外键指向同一user_devices记录 |
| refresh_tokens_validate_device | 插入/修改归属时检查设备active；后续状态变化仍靠刷新时检查和撤销事务处理 |

迁移是事务性的，不自动删除凭据；存在device_id为空的历史Refresh记录时会失败并回滚。只将旧记录标记revoked不能通过NOT NULL检查；必须由管理员明确清理或根据可信设备归属迁移，不能随机生成UUID回填。

### 22.2 Ubuntu/WSL使用超级用户执行

psql是客户端命令，默认数据库超级用户通常为postgres，不是psql。以下示例连接backend_dev，实际必须与后端DATABASE_URL数据库一致，DEV/TEST/PROD分别执行：

```bash
cd /mnt/d/projects/cpp_backend_server
sudo -u postgres psql -d backend_dev -v ON_ERROR_STOP=1 < database/migrations/002_login_tickets.sql
```

不加-h时使用本地Unix socket；认证策略仍以该实例pg_hba.conf为准。若实际库不是backend_dev，替换后再执行。给运行角色授权，例如后端确实使用back_dev_app时：

```bash
sudo -u postgres psql -d backend_dev -v ON_ERROR_STOP=1 -c 'GRANT USAGE ON SCHEMA app TO back_dev_app; GRANT SELECT, INSERT, UPDATE ON app.login_tickets TO back_dev_app;'
```

如果运行角色是backend_manager或其他角色，请替换授权对象，不要仅给DBeaver管理员授权。既有设备、密钥及refresh_tokens权限仍需保留。没有新增环境变量，仍使用DATABASE_URL、JWT_SECRET。先迁移/授权，再构建部署新二进制并更新客户端；本地示例为8082，部署服务端口以实际配置为准。详细测试命令见 [部署说明](login-ticket-deployment.md)。

### 22.3 区分请求错误与内部失败

| 现象 | 意义/下一步 |
|---|---|
| PowerShell无效URI/未能解析主机名 | 请求尚未发出；baseUrl应是纯http://localhost:8082字符串 |
| 404 route_not_found | HTTP方法或路径错误，或请求到其他/旧二进制 |
| 注册409后登录401 invalid_credentials | 注册冲突不能证明此邮箱属于预期账号，核对账号密码 |
| 登录500、顶层code=internal_error | 已进入登录Handler；检查ticket表、权限、当前数据库和其他内部异常，尚未进入/register_device |
| 设备完成401 invalid_login_ticket | 可能格式不符、过期或已用；重新登录拿ticket，不能靠换user_id解决 |
| 设备完成500/超时 | 结果可能不确定，按21.4处理，不能直接假定可无限重放 |

以与后端相同的数据库/运行角色执行只读检查：

```sql
SELECT current_database(), current_user;
SELECT to_regclass('app.login_tickets') AS ticket_table;
SELECT
    has_table_privilege(current_user, to_regclass('app.login_tickets'), 'SELECT') AS can_select,
    has_table_privilege(current_user, to_regclass('app.login_tickets'), 'INSERT') AS can_insert,
    has_table_privilege(current_user, to_regclass('app.login_tickets'), 'UPDATE') AS can_update;
```

ticket_table为NULL表示此连接看不到该表；权限为false需要授权。以postgres运行时得到的true不能证明应用角色也有权限。常见SQLSTATE：42P01表不存在；42501权限不足（包括FOR SHARE相关权限）；42703字段不存在。不能只凭客户端internal_error确定具体哪一种。

```bash
sudo journalctl -u backend-server -n 80 --no-pager
```

登录Handler目前将异常转换为code但没有打印具体SQLSTATE，因此日志可能没有细节。诊断日志应只补错误码/阶段，不能输出密码、ticket、Token或带凭据的DATABASE_URL。文档不会把客户端500当作已确认的数据库故障。

### 22.4 验证与维护范围

仓库有login_ticket_isolated_test，覆盖模拟依赖下的过期、并发一次消费及回滚；tests/login_ticket_http_test.py可对已迁移的测试服务器执行真实HTTP验收。未配置测试URL和账号时该脚本跳过，不代表真实数据库测试成功。本次只核对源码和文档，没有执行目标数据库迁移、授权或部署。

清理任务尚未自动调度。维护角色可按审计保留策略删除过期ticket；未清理不会让过期ticket恢复有效。API不会返回数据库ticket_hash，也没有ticket查询/续期/重放取回Token接口。
