# 服务器后端接口文档

更新时间：2026-10-13。

English version: [Backend API Documentation](backend-api.en.md)。

本文描述当前源代码已经实现的 HTTP 与 WebSocket 接口，而不是计划中的最终设计。根据 `main`、Router、Handler、认证服务、Repository 和 HTTP 解析/响应实现整理；本次未对运行中的服务进行接口实测。部署的可执行文件若未重新编译，行为可能与当前源码不同。

## 1. 访问地址与通用约定

| 场景 | 基础地址 | 说明 |
| --- | --- | --- |
| 域名入口（本文默认） | `https://aittelegramapi.uk` | 用户已配置的 HTTPS 域名；本次未在线核验 DNS、证书或代理配置 |
| Windows 本地通过 Nginx 访问 WSL | `http://localhost:8088` | 保留为本地调试入口，不是公网客户端应使用的地址 |
| 直接访问 C++ 服务 | `http://localhost:8082` | 当前 main 实际监听 `0.0.0.0:8082`；启动日志可能仍显示旧端口 8081 |

以下接口路径拼接在基础地址之后，例如 `https://aittelegramapi.uk/login_by_email`。当前代码没有 `/api` 或 `/v1` 前缀。基础地址不包含 `/login_by_email`；该部分是登录接口路径。原有认证示例使用域名入口，第 12 节的 PowerShell 好友示例使用本地 8082 端口。

- JSON 请求使用 `Content-Type: application/json`，请求体必须是 JSON 对象，不接受数组、字符串或 `null`。当前 Handler 实际按请求体解析，没有强制验证 Content-Type。
- 字段名区分大小写；额外 JSON 字段目前被忽略。
- 路由按方法和路径精确匹配。多一个结尾 `/` 或附带查询字符串，可能导致匹配失败；当前没有独立解析 query 参数。
- 未匹配到方法和路径时返回 404，当前未区分 405 Method Not Allowed。
- 后端每次响应后关闭连接，响应带 `Connection: close`；Nginx 到客户端的连接策略可能不同。
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

| 方法 | 路径 | Access Token | 当前用途 |
| --- | --- | --- | --- |
| POST | `/register_by_email` | 不需要 | 邮箱密码注册 |
| POST | `/login_by_email` | 不需要 | 邮箱密码登录并签发两种 Token |
| POST | `/register_device` | 必须 | 登记设备及带签名预密钥 |
| POST | `/upload_one_time_prekeys` | 必须 | 上传 1～10 个设备一次性公钥 |
| POST | `/get_one_time_prekey` | 必须 | 领取好友设备的一次性预密钥 |
| POST | `/friend_devices` | 必须 | 拉取一个好友的全部有效设备 |
| POST | `/get_device_key_bundle` | 必须 | 获取好友设备的带签名预密钥资料 |
| POST | `/verify_access_token` | 必须 | 验证 Access Token，成功后进入受保护 Handler |
| GET | `/refresh_token` | 不需要 | 验证 Refresh Token 并签发新的 Access Token |
| GET | `/user` | 不需要 | 旧版按邮箱查询用户的调试接口，不建议继续依赖 |
| GET | `/find_user_by_username` | 必须 | 按用户名查找用户，要求 JSON Body |
| POST | `/request_friendship` | 必须 | 创建好友申请 |
| GET | `/fetch_unprocessed_friend_request` | 必须 | 拉取收到的待处理申请，无需 Body |
| POST | `/process_friendship_request` | 必须 | 接受、拒绝或取消申请 |
| GET | `/fetch_friends` | 必须 | 拉取当前用户的好友列表，无需 Body |

注意：`/refresh_token` 当前确实注册为 GET，且要求 JSON 请求体。本文忠实记录此行为，不代表推荐这样设计；后续应改为 POST。

## 3. 通用错误格式

由 `ErrorResponseMaker` 产生的错误响应使用 JSON：

```json
{
  "error": {
    "code": "invalid_credentials",
    "message": "Email or password is incorrect"
  }
}
```

客户端根据 HTTP 状态码和 `error.code` 判断，不应依赖英文 message 的大小写或措辞。

### 请求体通用错误

以下错误适用于注册、登录、刷新三个要求 JSON 请求体的接口。

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

## 5. 邮箱密码登录

### POST `/login_by_email`

无需 Access Token。

```json
{
  "email": "alice@example.com",
  "password": "TestPass123!"
}
```

两个字段都必须为非空字符串。邮箱去除首尾普通空格并转小写；密码当前也去除首尾普通空格。登录不重新执行注册时的密码复杂度校验，而是验证保存的密码哈希。

### 成功响应：200 OK

```json
{
  "id": "33e76cf5-fc26-4fff-9f88-d08690ffb863",
  "display_name": "Alice",
  "access_token": "ACCESS_TOKEN",
  "refresh_token": "REFRESH_TOKEN",
  "username": "alice_chat01"
}
```

示例中的 Token 是占位符，不可用于验证。响应没有 `expires_in` 或 `token_type` 字段。

- Access Token：JWT，当前有效期 15 分钟；算法 HS256，issuer 为 `cpp_backend_server`，audience 为 `cpp_backend_api`，subject 为用户 UUID。
- Refresh Token：32 字节随机数据编码为 Base64URL 文本；当前数据库过期时间为插入时起 7 天。
- 数据库存储 Refresh Token 的 BLAKE2b 哈希，不存储明文。客户端收到的明文用于之后提交刷新请求。
- 同一用户每次成功登录都会插入新的 Refresh Token 记录，当前没有只允许一个会话的限制。

### 业务错误

| HTTP 状态 | code | 条件 |
| --- | --- | --- |
| 400 | `missing_register_email` | email 缺失、非字符串或为空；当前复用了注册错误码 |
| 400 | `missing_register_password` | password 缺失、非字符串或为空；当前复用了注册错误码 |
| 401 | `invalid_credentials` | 用户不存在或密码错误，统一提示 |
| 403 | `account_disabled` | 密码正确，但用户 is_active 为 false |

Handler 还定义了 `internal_error`（500）和 `service_unavailable`（503）的业务分支，但当前 LoginService 不主动返回这两个枚举；实际数据库异常主要由 HttpSession 按通用错误映射处理。

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

注意输入是驼峰字段 `refreshToken`，不是登录响应里的 `refresh_token`。不能把数据库保存的哈希当成 Token 提交，也不用提交 user_id。

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

此外，这里直接把 UUID 对象赋给 JSON，没有像注册/登录一样显式转换成字符串；不要依赖其 id 字段已有统一的字符串格式。该接口尚不适合作为正式用户资料接口，本文不提供稳定的成功响应契约。

## 9. Windows Terminal 联调命令

以下命令用于 Windows Terminal 中的 **CMD（命令提示符）**，不是 PowerShell。邮箱若已注册，请更换测试编号。不要将真实密码或 Token 复制到公开日志。

### 9.1 注册

```bat
curl.exe -i "https://aittelegramapi.uk/register_by_email" -H "Content-Type: application/json" --data-raw "{\"email\":\"api_test01@example.com\",\"password\":\"TestPass123!\",\"display_name\":\"ApiTest01\",\"username\":\"api_test01\"}"
```

预期：首次注册 201，重复注册在数据库约束匹配时为 409。

### 9.2 登录

```bat
curl.exe -i "https://aittelegramapi.uk/login_by_email" -H "Content-Type: application/json" --data-raw "{\"email\":\"api_test01@example.com\",\"password\":\"TestPass123!\"}"
```

预期：200；复制响应中两种 Token 的值，不要复制字段名或包围它们的双引号。

### 9.3 设置本地 CMD 变量并验证 Access Token

```bat
set "ACCESS_TOKEN=替换为登录返回的access_token"
set "REFRESH_TOKEN=替换为登录返回的refresh_token"
curl.exe -i -X POST "https://aittelegramapi.uk/verify_access_token" -H "authorization: Bearer %ACCESS_TOKEN%" -H "Content-Length: 0"
```

预期：200，当前成功响应体为空。

### 9.4 获取新的 Access Token

```bat
curl.exe -i -X GET "https://aittelegramapi.uk/refresh_token" -H "Content-Type: application/json" --data-raw "{\"refreshToken\":\"%REFRESH_TOKEN%\"}"
```

预期：200，响应带新的 access_token 和原 refresh_token。要测试新的 Access Token，请手动更新 ACCESS_TOKEN 变量后再执行验证命令。

### 9.5 错误分支

```bat
curl.exe -i -X POST "https://aittelegramapi.uk/verify_access_token" -H "Content-Length: 0"
curl.exe -i -X POST "https://aittelegramapi.uk/verify_access_token" -H "authorization: Bearer invalid-token" -H "Content-Length: 0"
curl.exe -i "https://aittelegramapi.uk/login_by_email" -H "Content-Type: application/json" --data-raw "{\"email\":\"api_test01@example.com\",\"password\":\"WrongPass123!\"}"
curl.exe -i "https://aittelegramapi.uk/register_by_email" -H "Content-Type: application/json" --data-raw "{}"
curl.exe -i -X GET "https://aittelegramapi.uk/refresh_token" -H "Content-Type: application/json" --data-raw "{\"refreshToken\":\"invalid-token\"}"
```

依次预期：401、401、401、400、401。前提是请求正常到达后端且数据库可用。

## 10. 当前边界与后续修改清单

下面不是已实现能力，而是根据当前源码列出的后续工作：

1. 修复请求头大小写处理，避免合法认证头或 Content-Length 被遗漏。
2. 给 `/verify_access_token` 写入明确的 JSON 成功响应；给刷新成功响应补 Content-Type。
3. 将刷新接口改为 POST，并统一 refresh_token 字段和错误码命名。
4. 正确区分 JWT 过期与其他验证错误；测试过期分支。
5. 停止修改用户输入的密码（当前会 trim）；制定已有账号兼容策略再修改。
6. 明确用户禁用后现存 Token 的处理策略；当前登录检查 is_active，但 Access 验证和刷新没有同样的检查。
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
| 用户 SQL 和登录时 Refresh Token 的有效期 | [UserRepository.cpp](../src/repository/UserRepository.cpp) |
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

$bobToken = $login.access_token

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


## 13. 登记新设备

### POST `/register_device`

需要 `authorization: Bearer ACCESS_TOKEN`，用户 ID 由认证结果提供，请求无需 user_id。此接口调用 ChatHandler → KeyService → KeyRepository，在同一数据库事务插入 app.user_devices 和 app.device_key_bundles；任一插入失败回滚。当前新设备直接 active，不提供恢复 revoked 设备的功能。

```json
{
  "device_name": "Alice手机",
  "protocol_suite": "TEST-X25519-XEdDSA-HKDF-SHA256-v1",
  "identity_public_key_hex": "替换为64个hex字符",
  "signed_prekey_id": "42",
  "signed_prekey_public_hex": "替换为64个hex字符",
  "signed_prekey_signature_hex": "替换为128个hex字符"
}
```

公钥和签名示例值是占位符，不可直接发送。

| 字段 | 类型 | 校验 |
|---|---|---|
| device_name | string | 必填，非空且不能全为空白 |
| protocol_suite | string | 必填，非空且不能全为空白；目前没有协议白名单或协商 |
| identity_public_key_hex | string | 必填，32 字节公钥，即 64 个 hex 字符，无 0x/\x 前缀 |
| signed_prekey_id | string | 必填，0～9223372036854775807 的十进制数字字符串，例如 "42"；JSON 数字 42 不接受 |
| signed_prekey_public_hex | string | 必填，32 字节，即 64 个 hex 字符 |
| signed_prekey_signature_hex | string | 必填，64 字节，即 128 个 hex 字符 |

成功返回 `201 Created`：

```json
{
  "device_id": "1204404a-4fd1-4419-8c87-99583872faa2",
  "device_status": "active"
}
```

响应 device_id 是实际新 UUID。客户端保存它，并在后续登录中提交。新设备 key_version=1，is_current=true，signed_prekey_id 来自请求，expires_at 暂为空。数据库公钥通过 decode(hex,'hex') 存入 BYTEA。

| HTTP | code | 条件 |
|---|---|---|
| 400 | missing_request_body | 请求体为空 |
| 400 | invalid_json | JSON 无效，或顶层不是对象 |
| 400 | invalid_device_name / invalid_protocol_suite | 相应字段缺失、非字符串、空或全为空白 |
| 400 | invalid_identity_public_key_hex / invalid_signed_prekey_public_hex / invalid_signed_prekey_signature_hex | 相应字段缺失、类型错误、空、长度或 hex 格式错误 |
| 400 | invalid_signed_prekey_id | 缺失、非字符串、空、非十进制或超出非负 BIGINT 范围 |
| 401 | invalid_access_token / access_token_expired | Router 的现有认证错误，未调用登记 Handler |
| 503 | database_unavailable | DatabaseErrorKind::Connection |
| 500 | register_device_failed | 其他数据库错误或意外异常；响应不泄露 SQL/公钥材料 |

Windows PowerShell 示例：从此前生成的 A1 公钥包构造请求。修改文件路径与 token；当前 body.user_id 即使提供也不用于选择用户。应使用与该客户端一致的密钥，并在成功后将服务器返回的 UUID 更新到客户端存储。

```powershell
$accessToken = '替换为Alice登录返回的access_token'
$bundle = Get-Content -LiteralPath 'C:\Users\yangb\Documents\Codex\2026-10-06\xian\outputs\test_keys\public_bundles\A1.json' -Raw | ConvertFrom-Json
$body = @{
    device_name = 'Alice手机'
    protocol_suite = $bundle.protocol_suite
    identity_public_key_hex = $bundle.identity_public_key_hex
    signed_prekey_id = [string]$bundle.signed_prekey.key_id
    signed_prekey_public_hex = $bundle.signed_prekey.public_key_hex
    signed_prekey_signature_hex = $bundle.signed_prekey.signature_hex
} | ConvertTo-Json -Compress

Invoke-RestMethod -Method Post -Uri 'http://localhost:8082/register_device' -Headers @{ authorization = "Bearer $accessToken" } -ContentType 'application/json' -Body $body
```

当前范围：只登记设备和带签名预密钥，不写入一次性预密钥库存，不生成信封，不建立客户端加密会话。仅校验公钥/签名的 hex 编码和长度，Service 未做密码学签名验证，也未验证公钥安全性；不能把 active 当成上述检查已完成的证明。认证中间件目前仅检查用户 Token，账号停用后的 Token 策略沿用现有实现。

本接口尚未提供 registration_id 幂等机制；重发请求可能创建多个设备。没有执行生产数据库请求；完整服务器编译和真实数据库联调结果需另行验证。


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

本接口验证格式和设备归属，不证明客户端持有对应私钥。当前设备检查与插入分属两个 Repository 调用；若要求撤销与上传严格互斥，后续应在同一事务中锁定设备并检查状态。


## 15. 领取好友设备的一次性预密钥

`POST /get_one_time_prekey`，受保护路由，请求头 `authorization: Bearer <access_token>`。
此接口会消耗一条 available 预密钥，仅在建立新会话需要它时调用。

```json
{"friend_id":"3755178b-3b3d-4cf4-9e61-0aba62fb2875","device_id":"6b73d84c-f23f-4932-8b4c-d320913914e3"}
```

friend_id 是目标好友的账号 UUID；device_id 必须是该好友的 active 设备。
调用者来自访问令牌，不接受请求体中的 my_id/user_id 作为调用者身份。
Service 先检查好友关系和设备归属；Repository 在同一事务内再次检查并以 FOR SHARE 锁定好友关系、目标设备和双方账号，要求双方账号 active。
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

Windows PowerShell 示例：使用 Alice 登录返回的 $login.access_token，领取 Bob 的设备预密钥。
```powershell
$claimBody = @{
    friend_id = "3755178b-3b3d-4cf4-9e61-0aba62fb2875"
    device_id = "6b73d84c-f23f-4932-8b4c-d320913914e3"
} | ConvertTo-Json -Compress
Invoke-RestMethod -Method Post -Uri "http://localhost:8082/get_one_time_prekey" -Headers @{ authorization = "Bearer $($login.access_token)" } -ContentType "application/json" -Body $claimBody
```

示例 friend_id 须替换成好友列表实际返回的账号 UUID（测试密钥文件中的账号编号不一定等于数据库账号编号）。
返回内容只有一次性公钥资料，并非完整会话公钥包；建立会话还需要身份公钥、带签名预密钥及其签名。
本接口没有请求幂等记录；重复调用会领取不同记录。响应丢失也不得将已 claimed 的记录重新投入库存。
当前没有领取限流，上线前须增加以防好友耗尽库存。


## 16. 拉取单个好友的全部有效设备

受保护接口 `POST /friend_devices`，请求头 `authorization: Bearer <access_token>`。
请求只接收一个目标好友 UUID；调用者身份来自令牌。
```json
{"friend_id":"好友列表实际返回的用户UUID"}
```

Service 先验证好友关系；Repository 查询中再次要求存在该关系，只返回目标好友的 active 设备，且双方账号必须 active。设备按 created_at、device_id 排序，不分页。
成功返回 200：
```json
{
  "friend_id":"好友UUID",
  "devices":[{
    "device_id":"好友设备UUID",
    "device_name":"Alice的手机",
    "status":"active",
    "identity_public_key_hex":"64个十六进制字符",
    "protocol_suite":"TEST-X25519-XEdDSA-HKDF-SHA256-v1"
  }]
}
```
没有有效设备时返回 `devices: []`，不是 404。账号停用也不返回设备；若好友关系在前置检查后已删除，查询返回空列表，不泄露设备。
公钥由 BYTEA 编码为小写 hex；不返回私钥，不领取一次性预密钥。

| HTTP | code | 原因 |
|---|---|---|
| 400 | missing_request_body / invalid_json | 请求体缺失或不是 JSON 对象 |
| 400 | invalid_friend_id | UUID 缺失、类型或格式错误 |
| 401 | 现有鉴权错误 | 令牌无效或过期 |
| 403 | not_friends | 不存在好友关系 |
| 503 | database_unavailable | 数据库连接不可用 |
| 500 | fetch_friend_devices_failed | 其他内部错误 |

PowerShell（$login 是当前用户登录结果，$friendId 来自好友列表）：
```powershell
$deviceQuery = @{ friend_id = $friendId } | ConvertTo-Json -Compress
$friendDevices = Invoke-RestMethod -Method Post -Uri "http://localhost:8082/friend_devices" -Headers @{ authorization = "Bearer $($login.access_token)" } -ContentType "application/json" -Body $deviceQuery
$friendDevices | ConvertTo-Json -Depth 5
```
当前没有批量 user_ids 接口。收到设备列表后，按建立会话的需要调用 /get_one_time_prekey；不要每次同步就领取所有设备的一次性预密钥。


## 17. 获取好友设备的带签名预密钥资料

受保护接口 `POST /get_device_key_bundle`，请求头 `authorization: Bearer <access_token>`。
```json
{"friend_id":"好友账号UUID","device_id":"好友设备UUID"}
```
调用者来自访问令牌。Repository 的查询要求双方是好友、双方账号 active、设备属于目标好友且 active、bundle 是当前版本且未过期。
200 响应：
```json
{"friend_id":"好友账号UUID","device_id":"好友设备UUID","signed_prekey_id":42,"signed_prekey_public_hex":"64个hex字符","signed_prekey_signature_hex":"128个hex字符","is_current":true}
```
BYTEA 编码为小写 hex。本接口按当前 DeviceKeyBundle 类型返回带签名预密钥及签名，不含一次性预密钥，不会消耗库存。身份公钥和 protocol_suite 从 /friend_devices 获取；当前类型未返回 key_version、published_at、expires_at。本响应单独不足以构成完整会话初始化包。
400: missing_request_body / invalid_json / invalid_friend_id / invalid_device_id；401: 现有鉴权错误；404: key_bundle_unavailable（统一表示关系、归属、状态或资料不可用）；503: database_unavailable；500: fetch_key_bundle_failed。

```powershell
$bundleBody = @{ friend_id = $friendId; device_id = $friendDeviceId } | ConvertTo-Json -Compress
$bundle = Invoke-RestMethod -Method Post -Uri "http://localhost:8082/get_device_key_bundle" -Headers @{ authorization = "Bearer $($login.access_token)" } -ContentType "application/json" -Body $bundleBody
$bundle | ConvertTo-Json -Depth 5
```


## 18. WebSocket API（当前实现）

本节按当前 HttpSession、WebsocketWorker、WebSocketSession、WebSocketHandler、WebSocketDispatcher、ChatService 和 EnvelopeRepository 整理。它描述当前代码，而非最终规划。当前支持单目标信封存储、按设备游标分页同步、批量接收确认，以及握手和业务报文 Token 验证。接收方实时推送尚未实现。

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

每条业务JSON仍需包含 authorization 或 Authorization 字符串，值为精确前缀 Bearer 加一个空格及有效Token。两者同时存在优先小写字段；类型错误不会回退大写字段。握手验证不替代报文验证，连接尚未绑定设备；X-Device-Id 不是当前设备认证依据。设备归属和有效状态在业务事务中验证。

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

服务端从Token获得user_id；客户端sender_device_id只是申请使用的设备，不是可信身份。Repository保存事务检查并锁定：发送设备属于该user_id、双方设备active、双方账号active、双方账号不同、两方向好友关系均存在。当前接口不支持同账号设备同步。

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

HTTP登录→保存access_token→确认Alice发送设备有效→fetch_friends获取Bob实际id→friend_devices获取目标设备→按需获取bundle/prekey并在客户端加密→WebSocket升级→发送chat.newTextMessage→处理result或error。Token过期后通过现有HTTP刷新/登录取得新Token，下一条报文使用新Token；没有WebSocket refresh路由。

接收流程：HTTP登录→带Token握手→发送 chat.sync.envelope→持久化保存信封及对应设备游标→发送 chat.envelopes.ack→核对 acknowledged_ids→has_more=true 时携带更新后的全部游标继续同步。未知发送设备不必预先加入 cursors。

尚未实现：接收方实时推送、批量提交信封、连接设备绑定、接收设备在线目录与跨Worker投递。设计文档中的其他示例类型不是当前路由的别名。

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
