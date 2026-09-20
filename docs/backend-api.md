# 服务器后端接口文档

更新时间：2026-09-20。

English version: [Backend API Documentation](backend-api.en.md)。

本文描述当前源代码已经注册的 HTTP 接口，而不是计划中的最终设计。根据 `main`、Router、Handler、认证服务、Repository 和 HTTP 解析/响应实现整理；本次未对运行中的服务进行接口实测。部署的可执行文件若未重新编译，行为可能与当前源码不同。

## 1. 访问地址与通用约定

| 场景 | 基础地址 | 说明 |
| --- | --- | --- |
| 域名入口（本文默认） | `https://aittelegramapi.uk` | 用户已配置的 HTTPS 域名；本次未在线核验 DNS、证书或代理配置 |
| Windows 本地通过 Nginx 访问 WSL | `http://localhost:8088` | 保留为本地调试入口，不是公网客户端应使用的地址 |
| 直接访问 C++ 服务 | `http://127.0.0.1:8081` | 当前 main 实际监听 `0.0.0.0:8081`，不是只监听回环地址 |

以下接口路径拼接在基础地址之后，例如 `https://aittelegramapi.uk/login_by_email`。当前代码没有 `/api` 或 `/v1` 前缀。基础地址不包含 `/login_by_email`；该部分是登录接口路径。本文请求示例均使用域名入口，原始 HTTP 报文示例通过 HTTPS 连接发送。

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

其中 `Content-Length` 的值由客户端计算，不要复制示例中的 123。认证头当前需要小写 `authorization`，并使用精确前缀 `Bearer `。这是当前实现的兼容性缺陷，不是对客户端应长期施加的协议规则；经过 Nginx 时还需确认转发后的头名称。

## 2. 接口总览

| 方法 | 路径 | Access Token | 当前用途 |
| --- | --- | --- | --- |
| POST | `/register_by_email` | 不需要 | 邮箱密码注册 |
| POST | `/login_by_email` | 不需要 | 邮箱密码登录并签发两种 Token |
| POST | `/verify_access_token` | 必须 | 验证 Access Token，成功后进入受保护 Handler |
| GET | `/refresh_token` | 不需要 | 验证 Refresh Token 并签发新的 Access Token |
| GET | `/user` | 不需要 | 旧版按邮箱查询用户的调试接口，不建议继续依赖 |

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
  "display_name": "Alice"
}
```

| 字段 | 类型 | 必填 | 当前规则 |
| --- | --- | --- | --- |
| email | string | 是 | 非空；去除首尾普通空格、转为小写，再进行正则校验 |
| password | string | 是 | 去除首尾普通空格后，6～20 个 ASCII 测试字符范围内满足密码正则；至少有小写字母、大写字母、数字、非字母数字且非空白的字符，整体不能含空白 |
| display_name | string | 是 | 去除首尾普通空格后，3～20 个字符，仅英文字母、数字、下划线 |

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
  "display_name": "Alice"
}
```

`id` 是 UUID 字符串。注册在同一事务内创建用户和密码凭据；注册成功不返回 Token，也不等于已经登录。

### 业务错误

| HTTP 状态 | code | 条件 |
| --- | --- | --- |
| 400 | `missing_register_email` | email 缺失、不是字符串或原始值为空字符串 |
| 400 | `missing_register_password` | password 缺失、不是字符串或原始值为空字符串 |
| 400 | `missing_register_display_name` | display_name 缺失、不是字符串或原始值为空字符串 |
| 400 | `invalid_email` | 规范化后邮箱不符合规则 |
| 400 | `invalid_password` | 规范化后密码不符合规则 |
| 400 | `invalid_display_name` | 规范化后名称不符合规则 |
| 409 | `email_already_exists` | 邮箱唯一约束冲突且被服务层识别 |
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
  "refresh_token": "REFRESH_TOKEN"
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
curl.exe -i "https://aittelegramapi.uk/register_by_email" -H "Content-Type: application/json" --data-raw "{\"email\":\"api_test01@example.com\",\"password\":\"TestPass123!\",\"display_name\":\"ApiTest01\"}"
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
