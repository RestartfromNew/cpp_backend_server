# Backend API Documentation

Last updated: 2026-10-16.

Chinese version: [服务器后端接口文档](backend-api.md).

This document describes the HTTP and WebSocket endpoints implemented in the current source code, not a planned final design. It is based on main, Router, Handlers, authentication services, Repositories, and HTTP parsing/response code. Section 20.1 records limited device HTTP tests from user-supplied output on 2026-10-16; other endpoints are not thereby verified. A deployed executable that has not been rebuilt may behave differently from the current source.

## 1. Base URLs and General Conventions

| Scenario | Base URL | Notes |
| --- | --- | --- |
| Domain entry point (default in this document) | `https://aittelegramapi.uk` | HTTPS domain configured by the user; DNS, certificates, and proxy configuration were not verified online during this task |
| Local Windows access to WSL through Nginx | `http://localhost:8088` | Retained for local debugging, not for public clients |
| Direct access to the C++ server | `http://localhost:8082` | main currently binds to `0.0.0.0:8082`; the startup log may still show the old port 8081 |

Append endpoint paths to the base URL, for example `https://aittelegramapi.uk/login_by_email`. There is no `/api` or `/v1` prefix. The base URL does not include `/login_by_email`; that is the login endpoint path. Existing authentication examples use the domain entry point; the PowerShell friendship examples in Section 12 use local port 8082.

- Send JSON requests with `Content-Type: application/json`. The body must be a JSON object, not an array, string, or `null`. Handlers currently parse the body without enforcing Content-Type.
- Field names are case-sensitive. Additional JSON fields are currently ignored.
- Routes match the method and path exactly. A trailing slash or query string may prevent a match; query parameters are not parsed separately.
- An unmatched method/path returns 404. The router does not currently distinguish 405 Method Not Allowed.
- The backend closes the connection after each response and sends `Connection: close`. Nginx may use a different connection policy toward clients.
- Requests with bodies must supply a correct `Content-Length`; curl calculates it automatically. The parser does not implement chunked request bodies.
- The backend body limit is 1,048,576 bytes. Exceeding it currently returns 400, not 413. A proxy may also reject requests before they reach the backend.

### Current Header Case Limitation

The parser preserves header-name case, while downstream code performs case-sensitive map lookups. Use these exact spellings during integration testing:

```http
Content-Type: application/json
Content-Length: 123
authorization: Bearer ACCESS_TOKEN
X-User-Email: alice@example.com
```

The client must calculate Content-Length; do not copy the example value 123. Authentication accepts `authorization` or `Authorization` and the exact prefix `Bearer `; arbitrary header-case variants are not yet supported. This is an implementation compatibility defect, not a protocol rule that clients should permanently have to follow. When using Nginx, also check the header names actually forwarded to the backend.

## 2. Endpoint Overview

| Method | Path | Access Token | Current purpose |
| --- | --- | --- | --- |
| POST | `/register_by_email` | Not required | Register with email and password |
| POST | `/login_by_email` | Not required | Log in and issue both types of token |
| POST | `/register_device` | Required | Register device and signed prekey |
| POST | `/revoke_device` | Required | Revoke an owned device after password verification; repeatable |
| POST | `/upload_one_time_prekeys` | Required | Upload 1–10 device one-time public keys |
| POST | `/get_one_time_prekey` | Required | Claim a friend device one-time prekey |
| POST | `/user_devices` | Required | List active devices of yourself or a friend |
| POST | `/friend_devices` | Required | Legacy alias with explicit target |
| POST | `/get_device_key_bundle` | Required | Fetch a friend device signed prekey bundle |
| POST | `/verify_access_token` | Required | Verify an Access Token and enter a protected Handler |
| GET | `/refresh_token` | Not required | Validate a Refresh Token and issue a new Access Token |
| GET | `/user` | Not required | Legacy email-based user lookup for debugging; not recommended for further integration |
| GET | `/find_user_by_username` | Required | Find a user; requires a JSON body |
| POST | `/request_friendship` | Required | Create a friend request |
| GET | `/fetch_unprocessed_friend_request` | Required | Fetch incoming pending requests; no body |
| POST | `/process_friendship_request` | Required | Accept, reject, or cancel a request |
| GET | `/fetch_friends` | Required | Fetch the authenticated user's friends; no body |

`/refresh_token` is currently registered as GET and requires a JSON body. This documents the existing behavior, not a recommended design. It should be migrated to POST.

## 3. Common Error Format

Errors generated by `ErrorResponseMaker` use JSON:

```json
{
  "error": {
    "code": "invalid_credentials",
    "message": "Email or password is incorrect"
  }
}
```

Clients should branch on the HTTP status and `error.code`, not on the wording or capitalization of message.

### Common Request-Body Errors

These apply to registration, login, and refresh endpoints, which require JSON bodies.

| HTTP status | code | message | Condition |
| --- | --- | --- | --- |
| 400 | `missing_request_body` | Request body is required | Empty request body |
| 400 | `invalid_json` | Request body must be a valid JSON object | Invalid JSON or a non-object top-level value |

### Infrastructure Errors

| HTTP status | code | Condition |
| --- | --- | --- |
| 400 | `invalid_http_request` | HTTP parser error, including invalid or oversized Content-Length |
| 404 | `route_not_found` | No matching method/path |
| 503 | `service_unavailable` | DatabaseErrorKind::Connection during request processing |
| 500 | `internal_server_error` | Unhandled database query, constraint, data-conversion error, or other standard exception |

These are the main backend application mappings. Responses generated by Nginx itself, such as 502 or 504, are not guaranteed to use this JSON format. A response is not guaranteed after a network disconnection either.

## 4. Email and Password Registration

### POST `/register_by_email`

No Access Token is required.

```json
{
  "email": "alice@example.com",
  "password": "TestPass123!",
  "display_name": "Alice",
  "username": "alice_chat01"
}
```

| Field | Type | Required | Current rules |
| --- | --- | --- | --- |
| email | string | Yes | Non-empty; leading/trailing ordinary spaces are removed, then the value is lowercased and checked against the email regex |
| password | string | Yes | After trimming ordinary spaces, ASCII test inputs must be 6–20 characters and match the password regex: at least one lowercase letter, uppercase letter, digit, and non-alphanumeric non-whitespace character; no whitespace anywhere |
| display_name | string | Yes | After trimming ordinary spaces, 3–20 characters consisting only of English letters, digits, and underscores |
| username | string | Yes | After trimming ordinary spaces, 3–30 English letters, digits, underscores or periods; currently not lowercased; database constraints also apply |

Trimming here means removing leading/trailing ASCII spaces, not all Unicode whitespace. Passwords are also currently trimmed; this should be corrected later. For now, do not use passwords with leading/trailing spaces. Non-ASCII password length and character classification must not be interpreted as Unicode character counts; the implementation does not define a complete Unicode password policy.

Current email regex:

```text
^[a-zA-Z0-9_+&*-]+(?:\.[a-zA-Z0-9_+&*-]+)*@(?:[a-zA-Z0-9-]+\.)+[a-zA-Z]{2,10}$
```

Current password regex:

```text
^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)(?=.*[^A-Za-z0-9\s])\S{6,20}$
```

### Success: 201 Created

```json
{
  "id": "33e76cf5-fc26-4fff-9f88-d08690ffb863",
  "display_name": "Alice",
  "username": "alice_chat01"
}
```

`id` is a UUID string. Registration creates the user and password credential in one transaction. Successful registration does not return tokens or automatically log the user in.

### Business Errors

| HTTP status | code | Condition |
| --- | --- | --- |
| 400 | `missing_register_email` | email is missing, not a string, or originally an empty string |
| 400 | `missing_register_password` | password is missing, not a string, or originally an empty string |
| 400 | `missing_register_display_name` | display_name is missing, not a string, or originally an empty string |
| 400 | `missing_register_username` | username is missing, not a string, or empty |
| 400 | `invalid_user_name` | Invalid username format or a recognized username CHECK violation |
| 409 | `username_already_exists` | A recognized username uniqueness violation |
| 400 | `invalid_email` | Normalized email fails validation |
| 400 | `invalid_password` | Normalized password fails validation |
| 400 | `invalid_display_name` | Normalized display name fails validation |
| 409 | `email_already_exists` | Email uniqueness violation recognized by the service |
| 500 | `register_failed` | Registration service reports failure |

When multiple fields are invalid, only the first detected error is returned. Duplicate-email mapping depends on database error code `23505` and constraint name `password_credentials_login_email_unique`; the database schema must match the code.

## 5. Email and Password Login

### POST `/login_by_email`

No Access Token is required.

```json
{
  "email": "alice@example.com",
  "password": "TestPass123!"
}
```

Both fields must be non-empty strings. Email is trimmed of leading/trailing ordinary spaces and lowercased; the password is currently trimmed too. Login verifies the stored password hash rather than repeating the registration password-complexity checks.

### Success: 200 OK

```json
{
  "id": "33e76cf5-fc26-4fff-9f88-d08690ffb863",
  "display_name": "Alice",
  "access_token": "ACCESS_TOKEN",
  "refresh_token": "REFRESH_TOKEN",
  "username": "alice_chat01"
}
```

The tokens shown are placeholders, not valid credentials. The response contains no `expires_in` or `token_type` field.

- Access Token: a JWT valid for 15 minutes, signed with HS256; issuer is `cpp_backend_server`, audience is `cpp_backend_api`, and subject is the user's UUID.
- Refresh Token: 32 random bytes encoded as Base64URL text; its database expiration is currently seven days after insertion.
- The database stores a BLAKE2b hash of the Refresh Token, not its plaintext. Clients submit the plaintext they received when requesting a refresh.
- Each successful login inserts a new Refresh Token record. There is currently no single-session-per-user restriction.

### Business Errors

| HTTP status | code | Condition |
| --- | --- | --- |
| 400 | `missing_register_email` | email is missing, not a string, or empty; the registration error code is currently reused |
| 400 | `missing_register_password` | password is missing, not a string, or empty; the registration error code is currently reused |
| 401 | `invalid_credentials` | User not found or incorrect password; identical public response |
| 403 | `account_disabled` | Password is correct, but the user's is_active is false |

The Handler also defines business branches for `internal_error` (500) and `service_unavailable` (503), but LoginService does not currently return those enum values explicitly. Database exceptions are mainly handled by HttpSession using the common mappings above.

## 6. Verify an Access Token

### POST `/verify_access_token`

```http
POST /verify_access_token HTTP/1.1
Host: aittelegramapi.uk
authorization: Bearer ACCESS_TOKEN
Content-Length: 0
```

No JSON body is required. Do not submit a Refresh Token here.

### Success: 200 OK

**The current response body is empty**, although main sets `Content-Type: application/json`. The Handler constructs `{"success":"success"}` but never assigns it to `response.body`. Clients therefore cannot currently parse the successful response as JSON.

This endpoint establishes that the token passed validation and reached the protected Handler. It does not additionally check whether the user still exists or is disabled, and it does not return user details.

### Authentication Errors

| HTTP status | code | Condition |
| --- | --- | --- |
| 401 | `invalid_access_token` | Missing authentication header, incorrect Bearer format, malformed token, or verification failure |
| 401 | `access_token_expired` | Authentication service explicitly returns Expired |

The current code handles JWT library verification errors before manually checking expiration. An expired token may therefore be classified as `invalid_access_token`; clients must not assume every expiration produces `access_token_expired`. Current 401 responses do not set `WWW-Authenticate`.

### Internal Call Chain

`HttpSession → Router::route → Router::protected_route → AuthMiddleWare::authenticate → AccessTokenService::verify → protected_handler(request, userId)`.

Clients do not need to call this endpoint before accessing other protected endpoints. Each future protected endpoint should authenticate its own request through the same mechanism. This is currently the only endpoint registered as protected in main.

## 7. Obtain a New Access Token Using a Refresh Token

### GET `/refresh_token`

No Access Token is required, but the plaintext Refresh Token must be submitted in a JSON body:

```json
{
  "refreshToken": "REFRESH_TOKEN"
}
```

The input field is camelCase `refreshToken`, not the `refresh_token` field returned by login. Do not submit the database hash instead of the token. No user_id is required.

The server hashes the submitted plaintext, locates the unique matching record, checks revocation and expiration, and obtains user_id from that record.

### Success: 200 OK

```json
{
  "id": "33e76cf5-fc26-4fff-9f88-d08690ffb863",
  "access_token": "NEW_ACCESS_TOKEN",
  "refresh_token": "REFRESH_TOKEN"
}
```

- The returned Refresh Token is the same token submitted in the request. **It is not rotated, and its database expiration is not extended.**
- The new Access Token is valid for 15 minutes from generation.
- The successful response currently does not explicitly set `Content-Type: application/json`, despite containing JSON text.
- Refresh currently validates the token record only, without checking the user's is_active status.

### Business Errors

| HTTP status | code | message / condition |
| --- | --- | --- |
| 400 | `missing_refreshToken` | Missing refresh token; field missing, not a string, or empty |
| 401 | `invalid_refreshToken` | Invalid refresh token; no matching hash record |
| 401 | `invalid_refreshToken` | Refresh token is expired; token expired or revoked |

Expired or revoked Refresh Tokens cannot refresh access; the user must log in again. When an expired, not-yet-revoked record is found, the current service first asks the Repository to mark it revoked. If that database write fails, an infrastructure error may be returned instead of 401.

A GET request body is unsuitable for normal browser frontend usage. When migrating this endpoint to POST, update main's route registration, clients, and this document together. Do not use POST before changing the code, and do not move the token into a URL query parameter to work around the GET-body limitation.

## 8. Legacy User Lookup

### GET `/user`

```http
GET /user HTTP/1.1
Host: aittelegramapi.uk
X-User-Email: alice@example.com
```

This is a public debugging endpoint with no Access Token verification.

| HTTP status | code / response | Condition |
| --- | --- | --- |
| 400 | `missing_user_email` | X-User-Email missing or empty |
| 404 | `user_not_found` | Query completes but no user is found |
| 200 | JSON containing id and display_name | Query and serialization succeed |
| 500 | `internal_server_error` | SQL/schema mismatch or another internal error |

The Repository still executes `SELECT id, username, email FROM users WHERE email = $1`, which does not match the new `app.users` / `app.password_credentials` schema. Actual behavior also depends on database search_path and whether legacy tables still exist.

The UUID object is also assigned directly to JSON instead of explicitly converted to a string as in registration/login. Do not rely on a consistent string format for this endpoint's id field. This endpoint is not ready to serve as a formal user-profile API, so no stable successful-response contract is provided here.

## 9. Windows Terminal Integration Commands

These commands are for **CMD (Command Prompt)** inside Windows Terminal, not PowerShell. Change the test email suffix if the address is already registered. Do not copy real passwords or tokens into public logs.

### 9.1 Register

```bat
curl.exe -i "https://aittelegramapi.uk/register_by_email" -H "Content-Type: application/json" --data-raw "{\"email\":\"api_test01@example.com\",\"password\":\"TestPass123!\",\"display_name\":\"ApiTest01\",\"username\":\"api_test01\"}"
```

Expected: 201 for the first registration; 409 for a duplicate if the database constraint matches the expected name.

### 9.2 Log In

```bat
curl.exe -i "https://aittelegramapi.uk/login_by_email" -H "Content-Type: application/json" --data-raw "{\"email\":\"api_test01@example.com\",\"password\":\"TestPass123!\"}"
```

Expected: 200. Copy the two token values, without the field names or surrounding JSON quotes.

### 9.3 Set Local CMD Variables and Verify the Access Token

```bat
set "ACCESS_TOKEN=REPLACE_WITH_ACCESS_TOKEN_FROM_LOGIN"
set "REFRESH_TOKEN=REPLACE_WITH_REFRESH_TOKEN_FROM_LOGIN"
curl.exe -i -X POST "https://aittelegramapi.uk/verify_access_token" -H "authorization: Bearer %ACCESS_TOKEN%" -H "Content-Length: 0"
```

Expected: 200, currently with an empty response body.

### 9.4 Obtain a New Access Token

```bat
curl.exe -i -X GET "https://aittelegramapi.uk/refresh_token" -H "Content-Type: application/json" --data-raw "{\"refreshToken\":\"%REFRESH_TOKEN%\"}"
```

Expected: 200, containing a new access_token and the original refresh_token. To test the new Access Token, manually update ACCESS_TOKEN before rerunning the verification command.

### 9.5 Error Cases

Each application JSON still requires authorization or Authorization with Bearer followed by a space and a valid token. Lowercase takes precedence. Upgrade authentication does not replace message authentication. The Session binds verified user_id/device_id; supply the deviceID handshake header, not X-Device-Id. Device ownership and active state are checked before upgrade and in business transactions.

Expected in order: 401, 401, 401, 400, 401, assuming requests reach the backend and the database is available.

## 10. Current Limitations and Follow-Up Work

The following items are future work, not already implemented capabilities:

1. Fix case-insensitive header handling so valid authentication and Content-Length headers are not missed.
2. Write an explicit successful JSON body for `/verify_access_token` and add Content-Type to successful refresh responses.
3. Change refresh to POST and standardize refresh_token field names and error codes.
4. Distinguish JWT expiration from other verification failures and test expiration paths.
5. Stop modifying submitted passwords through trimming; plan compatibility for existing accounts before changing this behavior.
6. Define how disabling a user affects existing tokens. Login currently checks is_active, but access verification and refresh do not.
7. Fix or remove the legacy `/user` endpoint. A formal user endpoint should address authorization and consistent UUID serialization.
8. Refresh Token rotation, logout, logout from all devices, third-party login, email verification, automatic token cleanup, rate limiting, and CORS preflight are not documented as available APIs: main does not expose their corresponding endpoints or complete workflows. A Repository function alone does not mean the HTTP feature is connected.

Examples now use the domain provided by the user, but configuring a domain and HTTPS does not establish production readiness. The limitations above still need attention. Public deployment requires separate verification of TLS certificates, network exposure, request limits, and authentication security. No registration, login, or other test requests were sent to the public service during this documentation update.

## 11. Source References for Maintenance

| Area | Source |
| --- | --- |
| Port and public/protected route registration | [main.cpp](../src/main.cpp) |
| Route matching and authentication error responses | [Router.cpp](../src/handler/Router.cpp) |
| Registration, login, legacy lookup, verification success response | [UserHandler.cpp](../src/handler/UserHandler.cpp) |
| Refresh request and response handling | [RefreshTokenHandler.cpp](../src/handler/RefreshTokenHandler.cpp) |
| Reading the Bearer header | [AuthMiddleWare.cpp](../src/http/AuthMiddleWare.cpp) |
| Access Token generation and verification | [AccessTokenService.cpp](../src/Auth/AccessTokenService.cpp) |
| Refresh Token generation, hashing, and validation | [RefreshTokenService.cpp](../src/Auth/RefreshTokenService.cpp) |
| Registration validation rules | [RegisterService.cpp](../src/service/RegisterService.cpp) |
| Login workflow | [LoginService.cpp](../src/service/LoginService.cpp) |
| User SQL and Refresh Token lifetime on login | [UserRepository.cpp](../src/repository/UserRepository.cpp) |
| HTTP request parsing | [HttpParse.cpp](../src/http/HttpParse.cpp) |
| Common exception-to-HTTP-error mapping | [HttpSession.cpp](../src/http/HttpSession.cpp) |

Whenever endpoint paths, parameters, statuses, or response structures change, update both language versions in the same change and rerun the registration → login → protected access → refresh test sequence.

## 12. User Discovery and Friendship Endpoints

All endpoints in this section require the lowercase header `authorization: Bearer ACCESS_TOKEN`.
Authentication failures return 401 with `invalid_access_token` or `access_token_expired`,
depending on the middleware classification. The acting user ID comes from the token, not the body.
UUIDs are JSON strings. This update was checked against source code; no live API tests were run.

### 12.1 GET `/find_user_by_username`

Currently requires a JSON body, not query parameters:

```json
{"username":"bob_chat01"}
```

Success: 200.

```json
{"id":"0776df58-9cbe-4f81-b613-4439f6c3b566","username":"bob_chat01","display_name":"Bob"}
```

Lookup uses an exact username match without automatic lowercasing. Errors:

| HTTP | code | Condition |
| --- | --- | --- |
| 400 | missing_request_body / invalid_json | Empty body or invalid JSON object |
| 400 | missing_username | Missing, non-string, or empty username |
| 400 | cannot_find_myself | Searching for the authenticated user |
| 404 | user_not_found | No matching user |

Database exceptions use the common HTTP-layer mapping. GET bodies have limited interoperability;
this describes the implementation, not the recommended long-term API design.

### 12.2 POST `/request_friendship`

```json
{"friend_id":"0776df58-9cbe-4f81-b613-4439f6c3b566","message":"Hi Bob, I am Alice!"}
```

friend_id is the target user's UUID. message is optional; currently non-string or empty values also
become an empty string. Success is **200 with an empty body**: the request has been saved,
not necessarily delivered or accepted.

| HTTP | code | Condition |
| --- | --- | --- |
| 400 | missing_request_body / invalid_json | Invalid body |
| 400 | missing_friend_id | Missing, non-string, or empty friend_id |
| 400 | cannot_request_friendship_myself | Self-request |
| 409 | request_already_exists | A pending request exists between the pair and its unique-index error is recognized |
| 503 | request_friendship_failed | Service exceptions caught by the current Handler |

Invalid UUID conversion currently occurs outside the try block and may return a generic 500.
A nonexistent target may trigger a foreign-key error mapped to 503, not 404.
The request message is server-readable text, not end-to-end encrypted chat content.

### 12.3 GET `/fetch_unprocessed_friend_request`

No body. Returns pending requests addressed to the authenticated user, ordered by creation time
and request ID descending.

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

request_id identifies the request; id identifies its sender. Use request_id when processing it.
Currently, no requests returns `{"unprocessed_requests":"None"}`, not an empty array.
Success is 200; caught exceptions return 503 `fetch_new_request_failed`.
Processed requests are excluded. The response currently omits the request timestamp.

### 12.4 POST `/process_friendship_request`

```json
{"request_id":"efbd99da-b38f-4ee9-9c70-6a2ef96c2820","process":"accepted"}
```

| process | Authorized actor | Effect |
| --- | --- | --- |
| accepted | Recipient | Accept and create both friendship directions in one transaction |
| rejected | Recipient | Reject without creating a friendship |
| cancelled | Requester | Cancel without creating a friendship |

Do not submit requester_id or friend_id; both identities are read from the stored request.
Success: 200.

```json
{"request_id":"efbd99da-b38f-4ee9-9c70-6a2ef96c2820","status":"accepted","already_processed":false}
```

Repeating the same action returns 200 with already_processed=true, without duplicate relationships
or restoring a friendship deleted later. A row lock serializes operations on the same request.
Database failures within acceptance roll back the transaction. A connection loss can leave the
commit outcome uncertain; retry using the same request_id and process.

| HTTP | code | Condition |
| --- | --- | --- |
| 400 | missing_request_body / invalid_json | Invalid body |
| 400 | invalid_request_id | Missing, non-string, empty, or invalid UUID |
| 400 | invalid_process | Missing, wrong type, or not one of the three allowed values |
| 403 | friend_request_forbidden | Actor is not authorized for this action |
| 404 | friend_request_not_found | Request does not exist |
| 409 | friend_request_already_processed | Request already has a different final state |
| 503 | database_unavailable | Database connection error |
| 500 | process_friend_request_failed | Other database errors |

Real-time notifications, blocking, and disabled-account checks are not integrated.
Results remain in the database; no notification-list endpoint was added in this change.

### 12.5 GET `/fetch_friends`

No body or query parameters. Fetches only the authenticated user's friends; supplying another
user_id in a body does not change the query identity. Joins app.friend_relation to app.users
and returns public fields ordered by username and user ID.

Success: 200.

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

No friends returns `{"friends":[]}`, not 404 or the string "None".
Currently returns all friends without pagination or filtering existing relationships by is_active.
Emails, password hashes, and tokens are excluded.
The friendship table must contain established relationships only, not legacy pending records.

| HTTP | code | Condition |
| --- | --- | --- |
| 401 | invalid_access_token / access_token_expired | Authentication failure |
| 503 | database_unavailable | Database connection error |
| 500 | fetch_friends_failed | Other database errors |

Other uncaught exceptions are mapped by the HTTP layer to 500 `internal_server_error`.

### 12.6 Windows PowerShell Integration Example

These are not CMD commands. Do not put spaces after continuation backticks.
Use disposable test accounts and never publish real tokens.

```powershell
# Log in as Bob
$loginBody = @{
    email = "bob_chat01@example.com"
    password = "TestPass123@"
} | ConvertTo-Json -Compress

$login = Invoke-RestMethod -Method Post `
    -Uri "http://localhost:8082/login_by_email" `
    -ContentType "application/json" -Body $loginBody

$bobToken = $login.access_token

# Fetch pending requests and copy the desired request_id
Invoke-RestMethod -Method Get `
    -Uri "http://localhost:8082/fetch_unprocessed_friend_request" `
    -Headers @{ authorization = "Bearer $bobToken" } |
    ConvertTo-Json -Depth 10

# Accept a chosen request after user confirmation; replace the UUID placeholder
$processBody = @{
    request_id = "REPLACE_WITH_REQUEST_UUID"
    process = "accepted"
} | ConvertTo-Json -Compress

Each application JSON still requires authorization or Authorization with Bearer followed by a space and a valid token. Lowercase takes precedence. Upgrade authentication does not replace message authentication. The Session binds verified user_id/device_id; supply the deviceID handshake header, not X-Device-Id. Device ownership and active state are checked before upgrade and in business transactions.

# Fetch friends
Invoke-RestMethod -Method Get `
    -Uri "http://localhost:8082/fetch_friends" `
    -Headers @{ authorization = "Bearer $bobToken" } |
    ConvertTo-Json -Depth 10
```

Acceptance checks: an empty array for no friends; mutual visibility after acceptance; no new friendship
after rejection/cancellation; 401 without a token; no reading other users' lists through body fields;
and no duplicate relationships after repeated acceptance.


## 13. Register a New Device

### POST `/register_device`

Requires `authorization: Bearer ACCESS_TOKEN`. The authenticated route supplies user_id; a body-supplied user_id is not used. ChatHandler calls KeyService, whose repository inserts app.user_devices and app.device_key_bundles in one transaction. New devices are active; this does not reactivate revoked devices.

Required JSON fields (all strings): device_name, protocol_suite, identity_public_key_hex, signed_prekey_id, signed_prekey_public_hex, signed_prekey_signature_hex. Names/suite must not be blank. Both public keys are 64 hex characters (32 bytes); signature is 128 hex characters (64 bytes), without prefixes. signed_prekey_id is a decimal string in 0..9223372036854775807; a JSON number is rejected.

```json
{
  "device_name": "Alice phone",
  "protocol_suite": "TEST-X25519-XEdDSA-HKDF-SHA256-v1",
  "identity_public_key_hex": "REPLACE_WITH_64_HEX_CHARACTERS",
  "signed_prekey_id": "42",
  "signed_prekey_public_hex": "REPLACE_WITH_64_HEX_CHARACTERS",
  "signed_prekey_signature_hex": "REPLACE_WITH_128_HEX_CHARACTERS"
}
```

The key/signature values above are placeholders. Success is `201 Created`:

```json
{
  "device_id": "1204404a-4fd1-4419-8c87-99583872faa2",
  "device_status": "active"
}
```

Save the returned UUID in the matching client and submit it on subsequent logins. Key version is 1, is_current=true, expires_at is currently null, and hex values are decoded into BYTEA.

Errors: 400 missing_request_body or invalid_json; 400 invalid_<field> for missing/non-string/empty fields, invalid key/signature hex lengths, blank names/suite, or invalid prekey ID. Router returns 401 invalid_access_token/access_token_expired before invoking this handler. Connection DatabaseError maps to 503 database_unavailable; other database/standard exceptions map to 500 register_device_failed. Error responses do not include SQL or key material.

See the Chinese version's PowerShell example to map the generated A1 fixture into this flat JSON request. This endpoint does not populate one-time prekey inventory or create sessions/envelopes. It checks encoding/length only: cryptographic signature verification, public-key safety validation, suite whitelisting, and registration-id idempotency are not implemented. Retry can create another device. Account disabling/token rules remain those of existing middleware. No production requests were executed; full server build and database integration remain separate verification steps.


### 14. Upload one-time prekeys

Protected `POST /upload_one_time_prekeys`, with `authorization: Bearer <access_token>`.
The device must belong to the authenticated user and be active. Private keys stay on the client.

```json
{"device_id":"c3939b6f-ee63-4448-b6ae-949a8bf47d49","prekeys":[{"prekey_id":1001,"public_key_hex":"06625dca527fc9b0fcad1acf661c5fed1e1cf85f25f403fa629b3e24b125d24a"}]}
```

Upload 1–10 entries. Each ID is a JSON integer in 0–2147483647 (the existing Repository uses C++ int); each public key is 64 hex characters encoding 32 bytes. Generate actual one-time public keys on the client; the example only demonstrates the format.
IDs must be unique within the batch and must not already exist for this device. The Repository inserts the batch in one transaction; any failure rolls back the entire batch.

Success: `201`, `{"device_id":"c3939b6f-ee63-4448-b6ae-949a8bf47d49","uploaded_count":1}`.
Errors use the existing error/code/message envelope: 400 for invalid input or duplicate IDs within the batch; 401 for authentication failure; 403 `device_unavailable`; 409 `prekey_already_exists`; 503 `database_unavailable`; 500 `upload_prekeys_failed`.
Validation checks encoding and device ownership, not possession of the private key. Insertion rechecks active device ownership and account status under FOR SHARE locks in the same transaction, serialized with device revocation.


## 15. Claim your own or a friend's device one-time prekey

Protected `POST /get_one_time_prekey`, with `authorization: Bearer <access_token>`.
Request: `{"friend_id":"FRIEND_UUID","device_id":"FRIEND_DEVICE_UUID"}`.
Both fields must be canonical UUID strings. The caller is supplied by authentication, never by the request body.
For a different target account, the Service verifies friendship; for your own account it skips only the friendship requirement. It verifies active target device ownership before claiming. The Repository rechecks authorization in one transaction, locks the friendship, device and both account rows with FOR SHARE, and requires both accounts to be active. It then claims one available prekey using FOR UPDATE SKIP LOCKED and UPDATE RETURNING. A response is sent only after commit; denied requests consume no keys.
200: `{"friend_id":"FRIEND_UUID","device_id":"FRIEND_DEVICE_UUID","prekey_id":1001,"public_key_hex":"64_HEX_CHARACTERS"}`.
400: missing_request_body, invalid_json, invalid_friend_id or invalid_device_id. 401: existing token errors. 403: not_friends or device_unavailable. 404: prekey_unavailable (including temporarily locked remaining records). 503: database_unavailable. 500: claim_prekey_failed.
This returns only a one-time prekey, not a complete session bundle. Retry is not idempotent and may consume a different key; claimed keys must not be returned to inventory after response loss. Claim rate limiting is not yet implemented.


## 16. List active devices of yourself or a friend

Protected `POST /user_devices`, header `authorization: Bearer <access_token>`.
Send `{}` for your own devices, or `{"target_user_id":"TARGET_USER_UUID"}` for yourself/a friend.
The caller always comes from the token. The legacy `friend_id` field is accepted; if both fields are present they must identify the same UUID.
The old `POST /friend_devices` route remains an alias, but still requires an explicit target field.

For your own account no friendship row is required. Other accounts must be friends. The query rechecks authorization and requires active accounts and active target devices; pending/revoked devices are excluded.
Results are ordered by created_at and device_id, without pagination. An empty result is `devices: []`.
The current device may be included: clients must exclude their current device ID when establishing peer sessions.

Example own-account response (UUID/hex values are placeholders):
```json
{
  "target_user_id": "MY_USER_UUID",
  "friend_id": "MY_USER_UUID",
  "is_own_device": true,
  "devices": [{
    "device_id": "MY_OTHER_DEVICE_UUID",
    "user_id": "MY_USER_UUID",
    "is_own_device": true,
    "device_name": "My iPad",
    "status": "active",
    "identity_public_key_hex": "64_HEX_CHARACTERS",
    "protocol_suite": "TEST-X25519-XEdDSA-HKDF-SHA256-v1"
  }]
}
```
Friend responses have `is_own_device: false`. The server computes this flag; it is not an authorization input. `friend_id` is retained as a compatibility alias for the target user, even for your own account.
400: missing_request_body / invalid_json / invalid_target_user_id / conflicting_target_user_id; legacy route missing target: invalid_friend_id. 401: token errors. 403: not_friends. 503: database_unavailable. 500: fetch_user_devices_failed.
No keys are consumed by listing.

## 17. Fetch your own or a friend's device signed prekey bundle

Protected `POST /get_device_key_bundle`, header `authorization: Bearer <access_token>`.
Request: `{"friend_id":"FRIEND_UUID","device_id":"DEVICE_UUID"}`.
The Repository checks that the target is yourself or a friend, active accounts, active target device ownership, and a current unexpired bundle. Caller identity is supplied by authentication.
200: `{"friend_id":"FRIEND_UUID","device_id":"DEVICE_UUID","signed_prekey_id":42,"signed_prekey_public_hex":"64_HEX_CHARACTERS","signed_prekey_signature_hex":"128_HEX_CHARACTERS","is_current":true}`.
This matches the current DeviceKeyBundle model: signed prekey and signature only. Identity public key and protocol suite are obtained from /user_devices; key_version and timestamps are not returned. This response alone is not a complete session initialization bundle. No one-time prekey is consumed.
400: missing_request_body, invalid_json, invalid_friend_id or invalid_device_id. 401: existing authentication errors. 404: key_bundle_unavailable. 503: database_unavailable. 500: fetch_key_bundle_failed.



Both key routes accept `target_user_id` (legacy alias: `friend_id`); omitting both defaults to the authenticated user. For example, `{"device_id":"MY_OTHER_DEVICE_UUID"}` requests your own device's keys. Conflicting targets return 400 `conflicting_target_user_id`; malformed/nil targets return 400 `invalid_target_user_id`. Success responses additionally contain `target_user_id` and `is_own_device`, and retain `friend_id`. No private keys are returned.

## 18. WebSocket API (current implementation)

Implemented: envelope storage, cursor synchronization, batch receipt ACK, authenticated device-bound upgrade, and cross-Worker online push. Client push handling is detailed in18.13.

### 18.1 Endpoint, upgrade and authentication

Local example: `ws://127.0.0.1:8082/`. main listens on 0.0.0.0:8082. Public deployment should use a configured TLS proxy and WSS; proxy routes have not been verified here.

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

The sample key is a fixed test vector. Real clients should generate a fresh random 16-byte key and Base64-encode it for each connection. Key response headers:

```http
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
```

Accept is SHA-1 of the client key concatenated with the WebSocket GUID, then Base64; this is not message encryption. The example omits other response headers.

HttpSession forwards Upgrade:websocket to the Worker without HTTP Router path validation. Before101, the Worker authenticates the HTTP Authorization header. Missing/invalid tokens receive HTTP401 with invalid_access_token; expired tokens receive access_token_expired. It sends the JSON error and closes instead of upgrading. Sec-WebSocket-Key lookup is case-insensitive and requires a non-empty value; full method, Connection, Version and key-length validation is not implemented.

Each application JSON still requires authorization or Authorization with Bearer followed by a space and a valid token. Lowercase takes precedence. Upgrade authentication does not replace message authentication. The Session binds verified user_id/device_id; supply the deviceID handshake header, not X-Device-Id. Device ownership and active state are checked before upgrade and in business transactions.

### 18.2 Frame and JSON rules

| Item | Current behavior |
|---|---|
| Business data | FIN=1 Text frame, UTF-8 JSON object |
| Client masking | Required; unmasked frames are protocol errors |
| Server masking | Not used |
| RSV/compression | RSV must be zero; no permessage-deflate |
| Frame payload limit | 1,048,576 bytes for the complete JSON payload |
| Length encoding | 7/16/64-bit lengths; non-minimal/invalid encodings rejected |
| Fragmentation | FIN=0 data and Continuation frames are rejected |
| TCP segmentation | Incomplete frame bytes can be buffered; this differs from message fragmentation |
| Binary | Parsed into an array, then fails current object-payload validation |
| Non-JSON Text, arrays, scalars, null | invalid_payload |

Send the application JSON directly, without the server-internal header/payload_format/payload wrapper. Missing/unknown type behavior is documented in 18.7.

### 18.3 Storage request: chat.newTextMessage

One request targets one recipient device. Replace example UUIDs with actual registered devices and a client-generated message identifier.

```json
{
  "type":"chat.newTextMessage",
  "authorization":"Bearer ACCESS_TOKEN",
  "sender_device_id":"f7ab7d41-9955-4a73-922d-7b7a80ebdac4",
  "message_id":"4ffded1c-d415-4adb-a888-703116dc192e",
  "recipient_device_id":"6b73d84c-f23f-4932-8b4c-d320913914e3",
  "protocol_version":"TEST-OPAQUE-v1",
  "packet_base64":"AQID"
}
```

| Field | Type | Required rules |
|---|---|---|
| type | string | Exactly chat.newTextMessage, case-sensitive |
| authorization | string | Bearer token; Authorization is also accepted |
| sender_device_id | string | Canonical 36-character, non-nil UUID; ownership checked in DB |
| message_id | string | Stable non-nil client retry UUID, not the database envelope UUID |
| recipient_device_id | string | Canonical non-nil UUID, different from sender device |
| protocol_version | string | Non-empty, non-blank, at most128 bytes; no suite allowlist or internal packet-version check yet |
| packet_base64 | string | Standard padded Base64, length divisible by4, no whitespace; decoded1–65536 bytes |

Encoded Base64 is limited to87,384 characters; the decoder checks full consumption and actual output size. Extra fields are ignored. No request_id, conversation_id, input seq or envelopes array is used.

AQID encodes test bytes01 02 03, not an encrypted protocol packet. The server stores opaque decoded BYTEA and cannot verify ratchet correctness or decryptability. Actual E2EE requires the client's real protocol implementation.

### 18.4 Authorization and ordering

user_id comes from the token; sender_device_id is merely a requested sending identity. The storage transaction checks/locks sender ownership, active devices and accounts, both directional friendship records for different accounts. Different devices of the same account are allowed without friendship rows. Sending to the same device is rejected. The sender must match the current WebSocket session device.

A canonical unordered device-pair transaction advisory lock protects MAX(seq)+1 allocation. A1→B1 and B1→A1 share a sequence; other pairs have independent sequences starting at1. seq is an integer, not a timestamp, cryptographic counter or global synchronization watermark.

Success is returned after commit, without pushing to Bob; Bob may be offline. Review the schema in [prepare-envelope-message-storage.sql](prepare-envelope-message-storage.sql). message_id must no longer reference app.message(id); an old FK can produce23503 mapped to internal_server_error. This documentation update does not execute migrations.

### 18.5 Successful storage and retry

```json
{"type":"newTextMessage.result","success":true,"duplicate":false,"message_id":"4ffded1c-d415-4adb-a888-703116dc192e","envelope_id":"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee","seq":1}
```

```json
{"type":"newTextMessage.result","success":true,"duplicate":true,"message_id":"4ffded1c-d415-4adb-a888-703116dc192e","envelope_id":"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee","seq":1}
```

Retry scope is(sender_device_id, recipient_device_id, message_id). Matching protocol_version and decoded packet returns the original envelope_id/seq without another insert. Changed content produces envelope_conflict. Reusing message_id for another recipient is a separate scope.

success means persisted, not delivered, decrypted or read. Permission checks precede retry lookup, so revocation/friend removal can deny a previously saved request. Do not delete the highest-sequence row without preserving a watermark: MAX+1 could reuse its sequence.

### 18.6 Error responses: two actual shapes

Object/authentication errors have no success/message fields:

```json
{"type":"error","error":{"code":"missing_authorization"}}
```

| code | Trigger |
|---|---|
| invalid_payload | Application payload is not a JSON object |
| missing_authorization | Authorization fields missing or not strings |
| invalid_access_token | Empty string, wrong prefix or failed token validation |
| access_token_expired | Expired token |

Formal message validation/business errors:

```json
{"type":"newTextMessage.result","success":false,"error":{"code":"send_permission_denied","message":"Sender or recipient is not permitted"}}
```

| code | Meaning / message |
|---|---|
| invalid_request | Specific parser description, or Invalid message fields from Service |
| send_permission_denied | Sender or recipient is not permitted |
| envelope_conflict | Message identifier already exists with different content |
| database_unavailable | Retry later with the same message identifier and packet |
| retry_later | Same retry advice; bounded transaction attempts exhausted |
| server_busy | Service task was not queued |
| internal_server_error | Missing storage result / Unable to save message / Unable to process message / Unable to parse message |

These are application messages, not HTTP status codes. Failures generally lack message_id, envelope_id, seq and request_id; clients cannot assume UUID correlation for concurrent failures. Branch on code, not display message. Connection failure at commit or response timeout does not prove nothing was persisted.

Retries keep the original message_id, recipient, protocol_version and packet. Do not encrypt again under the old identifier. Service attempts at most3 whole transactions for40001,40P01 and identified envelope primary-key UUID collisions. SQLSTATE is not returned to clients. Do not bypass conflicts by blindly replacing identifiers.

### 18.7 Test route and incomplete invalid-input handling

```json
{"type":"chat.newTextMessage_Test","authorization":"Bearer ACCESS_TOKEN","id":"test-1","message":"hello"}
```

```json
{"type":"newTextMessage.result","success":true,"id":"test-1"}
```

The test route calls printId only: no envelope persistence, sequence, sending-device or friendship validation. id/message should be strings, but direct field conversions lack comprehensive error replies; task exceptions can produce only logs.

Unknown type reaches UnKnown and returns without a reply: **unsupported_type is not currently emitted**. Missing/non-string type can throw before task dispatch, with no guaranteed application response. Clients must send a recognized string type; not every malformed input receives a structured result.

### 18.8 Ping, Pong, Close and disconnects

| Opcode | Handling |
|---|---|
| Ping0x9 | Queue Pong with the same payload; bypass business authentication |
| Pong0xA | Clear pending Pong and update activity; payload correspondence is not checked |
| Close0x8 | Queue a Close echo, then close the connection |

Control frames require FIN=1 and≤125 bytes; Close length1 is invalid. Protocol errors, missing mask, RSV, oversize and unsupported fragmentation follow the close/error path, not JSON business replies.

Heartbeat checks run every5 seconds. After more than30 seconds of inactivity with no pending Pong, the server sends an empty Ping. At10 seconds waiting for Pong it removes the connection, subject to timer granularity. Application activity does not clear an already pending Pong requirement. Respond with a WebSocket Pong frame, not a JSON string.

Reply tasks return to the original Worker and validate fd/session_id to prevent descriptor reuse. Closed connections may lose replies; retry the stable request rather than assuming a rollback.

### 18.9 Client sequence and unimplemented interfaces

HTTP login→save token→verify sender device→fetch_friends for Bob's real account UUID→friend_devices→fetch bundle/prekey as needed and encrypt locally→WebSocket upgrade→chat.newTextMessage→handle result/error. Refresh/login uses existing HTTP routes; there is no WebSocket refresh route.

Receiving flow: HTTP login → authenticated upgrade → chat.sync.envelope → durably save envelopes and per-sender cursors → chat.envelopes.ack → compare acknowledged_ids → repeat with all updated cursors while has_more=true. Previously unknown senders need no initial cursor entry.

Implemented: device-bound sessions, online registry and cross-Worker push. Batch submission, read/decryption receipts and proactive disconnect after device revocation are not implemented. Proposed message types in design documents are not aliases for implemented routes. Worker application logs still include authorization; this documentation update does not redact logs.

### 18.10 Paginated synchronization: chat.sync.envelope

Request:

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

| Field | Rule |
|---|---|
| device_id | Non-nil UUID string of the authenticated user's active receiving device |
| cursors | Required array; [] for initial synchronization; at most1000 entries |
| peer_device_id | Non-nil sender device UUID, unique within the request and different from device_id |
| after_seq | JSON integer from0 to9223372036854775807; no strings, fractions or negatives |

Returns only envelopes addressed to device_id with seq greater than that sender's after_seq. Missing senders default to0, allowing discovery of first-time senders; explicit0 also fetches all stored incoming envelopes for that sender. The transaction validates device ownership, active device and active account. No current friendship check is required for previously accepted messages.

Success:

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

count equals envelopes.length. No matches is success=true, count=0, envelopes=[], has_more=false. A page contains at most100 envelopes and has an estimated Base64 JSON budget of512KiB; there is no client limit parameter. Ordering is sender_device_id, seq, envelope_id, not a global chronological timeline. packet_base64 is standard Base64 of the complete protocol packet; AQID is test bytes. Timestamps are not currently returned.

After saving a page, advance each sender's cursor to the largest saved incoming seq, retain other cursors, and request another page if has_more=true. Both directions share a device-pair sequence, so incoming sequences can have gaps. Never advance incoming cursors using outgoing storage receipts. has_more=false describes this query, not future arrivals.

Synchronization neither sets nor filters by received_at. Repeating cursors may return already acknowledged envelopes; deduplicate by envelope_id. Persist envelopes and cursors reliably, and recover pending ACKs after a crash. ACK success does not advance a server synchronization cursor.

### 18.11 Batch receipt acknowledgment: chat.envelopes.ack

Send after durable local storage. Receipt is neither successful decryption nor a read receipt.

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

envelope_ids is required:1–100 unique non-nil UUID strings. In one transaction, validate active device ownership and active account, then update only envelopes addressed to that device. received_at uses server CURRENT_TIMESTAMP and COALESCE preserves the first timestamp. Success is sent after commit. Repeated ACKs return matching IDs; decrypted_at is unchanged. No current friendship check is required.

Unknown/foreign IDs are omitted, and count may be zero. Partial matches still return success=true. acknowledged_ids order is unspecified; compare sets before considering every requested envelope acknowledged. Retry failures or uncertain results with the same IDs.

### 18.12 Synchronization and acknowledgment errors

```json
{"type":"chat.envelopes.ack.result","success":false,"error":{"code":"ack_permission_denied","message":"Device must belong to the authenticated user and be active"}}
```

Synchronization failures use chat.sync.envelope.result; acknowledgment failures use chat.envelopes.ack.result. Both include success=false and error.code/message. Authentication before routing uses the common type=error structure in18.6.

| Code | Meaning |
|---|---|
| invalid_request | Both: missing/invalid fields, ranges or duplicate IDs |
| sync_permission_denied | Sync: device ownership/active state or account check failed |
| ack_permission_denied | ACK: same device/account restrictions |
| database_unavailable | Both: database connection unavailable; retry the same request |
| retry_later | Both: serialization conflict/deadlock; retry original cursors/IDs |
| internal_server_error | Both: other storage/processing failure |
| server_busy | Both: task could not be queued |

Neither route has request_id. Execute synchronization and ACK requests sequentially on a connection so errors can be correlated.

### 18.13 Online push: client reception and processing

#### Connect the receiving device

Log in through HTTP, obtain access_token and a registered device_id, then connect to `ws://localhost:8082/` locally or the deployed WSS endpoint such as `wss://aittelegramapi.uk/`. Supply handshake headers `Authorization: Bearer ACCESS_TOKEN` and `deviceID: DEVICE_UUID`. Current code reads deviceID, not X-Device-Id; ensure proxies forward it. Header lookup is case-insensitive; use deviceID consistently. The old device_id header is no longer the handshake device header. JSON fields remain device_id or the endpoint-specific sender_device_id/recipient_device_id.

The server derives user_id from the token and verifies device ownership, active device and active account before registering the bound Session. Do not supply user_id as proof of identity. Token errors return401; device errors return403 with device_not_found, device_not_active or device_not_owned. Missing/malformed device_id and database exceptions currently reach the handshake exception boundary and close the connection without a guaranteed structured HTTP error.

Keep one receive loop running during sends, sync and ACKs. Use a WebSocket library that handles Ping/Pong.

#### Actual pushed message

After committing a new envelope, the server sends an unsolicited Text JSON message:

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

| Field | Meaning |
|---|---|
| type | Exact emitted value Online_Sync; internal Worker also accepts online_sync, but that is not the emitted spelling |
| success / duplicate | Currently true / false for a newly stored envelope; not receipt, decryption or read confirmation |
| recipientDeviceId / seq | Equal the corresponding envelope fields |
| envelope.envelope_id | Local deduplication key and ACK identifier |
| envelope.message_id | Sender retry identifier, not a substitute for envelope deduplication |
| envelope.sender_device_id / recipient_device_id | Source and target devices; target must match the local connected device |
| envelope.protocol_version | Protocol packet parsing version |
| envelope.packet_base64 | Standard Base64 of the complete protocol packet; AQID is test bytes |
| envelope.seq | Shared bidirectional device-pair sequence, not a global sequence or ratchet counter |

No token, private keys, plaintext or timestamps are included. The sender separately receives newTextMessage.result; the recipient receives Online_Sync. Duplicate submissions currently return duplicate=true to the sender without another push.

#### Dispatch incoming JSON by type

| Type | Action |
|---|---|
| Online_Sync | Process the single envelope |
| chat.sync.envelope.result | Process envelopes, then pagination/cursors |
| newTextMessage.result | Update outgoing storage state, not recipient delivery state |
| chat.envelopes.ack.result | Clear only pending IDs present in acknowledged_ids |
| error | Handle common authentication/format errors by error.code |

Pushes may arrive between a request and its response. Never assume the next received JSON is the response to the last request. Dispatch by type first. There is no request_id; keep same-type requests ordered so failures can be correlated.

#### Store, deduplicate, decrypt and acknowledge

1. Validate the target device and fields; look up envelope_id locally.
2. Decode packet_base64 into raw bytes. Use the same processing path for synchronized and pushed envelopes.
3. Durably save the complete envelope and pending ACK in a local transaction. Do not display or ratchet-decrypt duplicates twice; duplicates may be acknowledged again.
4. Use sender_device_id and protocol rules to select/create the client session and decrypt. The server does not establish the client cryptographic session. Follow the protocol library's out-of-order rules.
5. After durable storage, ACK immediately or batch1–100 envelopes. Receipt ACK need not wait for decryption. Network receipt or in-memory storage alone is insufficient.

```json
{
  "type": "chat.envelopes.ack",
  "authorization": "Bearer ACCESS_TOKEN",
  "device_id": "6b73d84c-f23f-4932-8b4c-d320913914e3",
  "envelope_ids": ["eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"]
}
```

Compare acknowledged_ids as a set and remove only confirmed pending ACKs. Retain and retry the same IDs on failure, timeout or reconnect. Repeated ACKs preserve the first received_at. Keep ciphertext on decryption failure; receipt is not proof of successful decryption.

#### Do not advance sync cursors from push maxima

Pushes and synchronization can interleave; concurrent push queueing is not guaranteed to follow seq order. If your sync cursor is12 and push15 arrives first,13/14 may still be missing. Setting after_seq=15 would skip them.

Keep locally stored envelopes separate from synchronization watermarks. Save/display/ACK pushes, but do not advance sync watermarks solely from the maximum pushed seq. On connect/reconnect, sync from the persisted watermark, deduplicate, and advance after processing each returned page. Bidirectional sequences can legitimately have gaps caused by outgoing messages, so integer continuity and outgoing receipts cannot determine incoming progress.

#### Disconnect and delivery semantics

Pool locates the target Worker; its queued task checks fd, session_id and device before buffering output. Offline/closing/stale sessions may drop the push without rolling back committed storage or setting received_at. A sender's successful receipt does not guarantee recipient delivery.

Start/reconnect flow: valid token → upgrade with the deviceID header → start receive loop → paginate from persisted cursors → deduplicate/store → send/retry ACKs. Handle subsequent pushes and sync again when recovery is needed.

One device has one directory entry; a new connection replaces the index but does not automatically close the old connection or broadcast to all its connections. Token expiry does not currently actively close an established Session, but application sync/ACK requests still authenticate. Refresh via HTTP for later requests and use the new token on reconnect. Revocation now broadcasts closure to all Workers and waits with a deadline; repeated revocation retries closure. main dependency wiring is fixed. See Section 19 for 200/202 and websocket_closed semantics.

## 19. Revoke a device

### POST /revoke_device

Protected endpoint requiring `Authorization: Bearer ACCESS_TOKEN`. The user comes from the token, not a body user_id. Revokes an owned active device without deleting its record, historical keys or envelopes.

Field names are case-sensitive. The request preserves `deviceID` (not `device_id`); the response uses `device_id`.

```json
{
  "deviceID": "6b73d84c-f23f-4932-8b4c-d320913914e3",
  "password": "YOUR_CURRENT_PASSWORD"
}
```

password must be a non-empty string and is verified exactly as supplied, without trimming. deviceID must be a standard hyphenated, non-nil UUID. Additional fields are ignored.

HTTP 200 confirms revocation and server closure; 202 means closure is not yet confirmed. Content-Type application/json.

```json
{
  "device_id": "6b73d84c-f23f-4932-8b4c-d320913914e3",
  "status": "revoked",
"already_revoked": false,
  "websocket_closed": true
}
```

Repeating the request still checks the password and ownership. It returns already_revoked=true and preserves the original revoked_at. Retry the same device ID after timeouts or uncertain commit outcomes.

| HTTP | error.code | Meaning |
| --- | --- | --- |
| 400 | missing_request_body | Empty request body |
| 400 | invalid_json | Invalid JSON or not an object |
| 400 | missing_password | Missing, non-string or empty password |
| 400 | missing_deviceID | Missing, non-string or empty deviceID |
| 400 | invalid_deviceID | Malformed or nil UUID |
| 401 | invalid_access_token / access_token_expired | Rejected by authentication before the Handler; expiry classification depends on the existing verifier |
| 403 | invalid_password | Password verification failed |
| 403 | device_not_owned | Device belongs to another user |
| 404 | user_not_found | Password login record not found |
| 404 | device_not_found | Device does not exist |
| 409 | device_not_active | Pending or another non-active, non-revoked state |
| 503 | database_unavailable | Connection failure; commit outcome may be uncertain |
| 503 | retry_later | Serialization failure or deadlock; retry |
| 500 | revoke_device_failed | Other database or internal failure |

Errors use the common error object in Section 3. After input validation, password verification precedes device ownership/state checks.

#### Concurrency and limitations

Revocation locks the device row and updates status/revoked_at. Prekey upload and envelope writes lock and check the active device within their transactions. A writer that acquired its lock first may finish before revocation; new operations after revocation commits cannot pass the state check. This is not a database trigger preventing arbitrary direct SQL inserts.

**This is not full logout or remote session termination.** Refresh tokens are not device-bound and are not revoked here; existing access tokens do not immediately expire. Revocation waits for all local Workers: 200 confirms server closure; 202 means closure is not yet confirmed. Previously buffered output may still arrive before closure. Account-level endpoints and device registration may still accept old tokens. New handshakes and operations that validate device status reject the revoked device.

#### Windows PowerShell test

Use a disposable test device; this endpoint cannot reactivate it. The example uses the domain. For direct access to the current main, use http://localhost:8081.

```powershell
$baseUrl = "https://aittelegramapi.uk"
$token = Read-Host "Access Token"
$deviceId = Read-Host "Test device UUID to revoke"
$securePassword = Read-Host "Current password" -AsSecureString
$password = [System.Net.NetworkCredential]::new("", $securePassword).Password
$body = @{ deviceID = $deviceId; password = $password } | ConvertTo-Json -Compress
try {
    Invoke-RestMethod -Method Post -Uri "$baseUrl/revoke_device" -Headers @{ Authorization = "Bearer $token" } -ContentType "application/json" -Body $body
    # Repeat: expect already_revoked=true.
    Invoke-RestMethod -Method Post -Uri "$baseUrl/revoke_device" -Headers @{ Authorization = "Bearer $token" } -ContentType "application/json" -Body $body
} finally {
    Remove-Variable password, body, token, securePassword -ErrorAction SilentlyContinue
}
```

Also test empty bodies, malformed JSON/UUIDs, wrong passwords, foreign/missing/pending devices, database failures and concurrent revocation versus prekey upload/message writes. No live endpoint requests were executed for this documentation.

### 19.1 Closure completion and call chain

Both initial and repeated revocation execute: confirm database revoked → broadcast to all Workers → scan all sessions owned by that user/device → stop application traffic → queue Close → remove sockets and complete promises → aggregate results → HTTP response.

main now constructs Pool before Handler and passes its reference. Pool releases its mutex before waiting so Worker index cleanup cannot deadlock. No matching sessions counts as closed.

- HTTP 200: status=revoked, websocket_closed=true. All Workers confirmed removal of matching server sessions, or there were none.
- HTTP 202: status=revoked, websocket_closed=false. Revocation committed, but shutdown, enqueue failure or timeout prevented confirmation. Stop using this device; retry the same device/password. Already-revoked requests also retry closure.
- Pool uses a shared 8-second wait deadline after task submission. Worker close deadline is 2 seconds, checked by the existing 5-second timer. A blocked Worker can delay enforcement; no hard real-time guarantee.
- Application-private Close code 4001 carries UTF-8 reason device_revoked. Output is drained before socket removal; this is not a remote logout acknowledgment protocol. Network errors, timeout or an already-closing session can prevent reason delivery.
- Every same-device session is scanned, not only the latest online index entry. Handshake and revocation tasks run serially on each Worker: in-progress handshakes register before the scan; later handshakes check committed database revocation.
- Closing sessions reject new business reads, pushes and ordinary replies. Previously committed operations are not undone; previously buffered output may precede Close.
- websocket_closed confirms cleanup in this server process only, not client receipt, logout or token erasure. Multiple backend instances still need cross-instance coordination.

Unconfirmed closure (202):

```json
{"device_id":"6b73d84c-f23f-4932-8b4c-d320913914e3","status":"revoked","already_revoked":false,"websocket_closed":false}
```

### 19.2 Required iOS logout

**After confirming revocation and WebSocket closure/local termination, the affected iOS device must log out and delete both Access Token and Refresh Token for that login session, from memory and persistent stores such as Keychain.**

- Self-revocation: status=revoked in either 200 or 202 confirms revocation. Immediately stop sends, refresh, reconnect and automatic login. 200 confirms server cleanup; for 202, wait a bounded time and cancel the local connection. Never keep using tokens while waiting.
- Close code 4001 with reason device_revoked: terminate the corresponding current login session, clear credentials and request login. A stale connection callback must not clear a newer login session.
- A revoking B must not log A out. HTTP responds to A; B handles its own Close notification. A cannot claim that B erased credentials.
- A missing Close reason or generic network failure is not proof of revocation. An explicit identity rejection is needed for recovery. Handshake device_not_active covers several invalid states and iOS access to handshake JSON remains unverified; no dedicated status endpoint exists yet.
- Stop the receive task and release WebSocket; delete both tokens and cached headers; cancel refresh/reconnect tasks; guard against late refresh responses restoring credentials; show login. Persistent deletion failure needs retry with credential use disabled.
- Early token deletion on confirmed revocation is acceptable. Do not automatically delete history or encryption keys.
- Offline/suspended B cannot erase credentials immediately. Client cleanup does not revoke server-side JWTs or refresh tokens, which remain unbound to devices.

Acceptance: multiple same-device connections, repeat/offline revocation, slow-client timeout/202, fd reuse, either HTTP/disconnect ordering, ordinary network failure, A remaining logged in, and late refresh responses. Build and live two-client verification remain outstanding.

## 20. Same-account device sessions and delivery

1. A1 lists its own active devices with POST /user_devices and {}, then excludes A1 itself.
2. For each target A2, fetch its bundle and claim a prekey only when needed. The client verifies key material and establishes a device-to-device encrypted session locally. This is not another WebSocket upgrade; each device connects to the server using its own deviceID.
3. A1 encrypts a separate envelope for A2 and submits chat.newTextMessage with sender_device_id=A1 and recipient_device_id=A2. The server does not re-encrypt or automatically fan out messages. Sending a message to B and synchronizing it to A2 requires separate target-specific ciphertexts. Existing idempotency keys and per-device-pair sequence numbers are unchanged.
4. Online_Sync routes to the recipient device as before. Offline chat.sync.envelope and chat.envelopes.ack operate only on the current recipient device's inbox, including envelopes from its own account. Never broaden these operations to all devices owned by the user.
5. For send/sync/ACK, the supplied sender_device_id/device_id must match the authenticated WebSocket session device. Mismatch returns success=false and error.code=session_device_mismatch with the operation's result type. Missing/malformed device fields return invalid_request. An A1 connection cannot read or ACK A2's inbox.
6. iOS uses is_own_device to select the local own-device synchronization UI/semantics, not to relax cryptographic validation. Prevent sync loops. This does not automatically migrate old message history to new devices. Revoked/pending devices remain ineligible.

Some HTTP checks have now been exercised, as recorded below. Live two-device WebSocket, message delivery and cryptographic acceptance remain outstanding. See the [isolated tests and acceptance checklist](../tests/message_storage_isolated/README.md).

### 20.1 Device HTTP test record (2026-10-16)

Evidence: user-supplied Windows PowerShell output from [user_devices_test.ps1](../tests/user_devices_test.ps1), targeting `http://localhost:8082`, with successful Alice login. Result: **PASS=87, FAIL=0, SKIP=3**. These are assertion counts, not HTTP request counts. This is not an agent rerun or evidence that the isolated C++ tests/full build passed.

Verified:

- Default own-account query, explicit target_user_id, legacy /friend_devices with friend_id, and matching target aliases returned 200.
- Each query returned four devices. Assertions checked owner, active status, unique UUIDs, top-level/item is_own_device=true, public-key hex format and nonempty protocol name.
- Conflicting aliases: 400 conflicting_target_user_id.
- Malformed/nil UUID and numeric target: 400 invalid_target_user_id.
- Random unknown target: 403 not_friends. Legacy route without target: 400 invalid_friend_id. Missing token: 401.

| Skipped case | Reason / next step |
|---|---|
| Friend devices | FriendUserId was omitted; supply an existing friend and verify is_own_device=false |
| Own-device bundle | The first selected device returned key_bundle_unavailable; prepare a current unexpired bundle. This neither diagnoses the exact failed query condition nor says that all other devices lack bundles |
| WebSocket session binding | SessionDeviceId was omitted; supply an actual active Alice device and test rejection of mismatched send/sync/ACK identities |

Run the file in Windows PowerShell, rather than pasting its parameter and try/catch/finally blocks separately:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "D:\projects\cpp_backend_server\tests\user_devices_test.ps1" -BaseUrl "http://localhost:8082"
```

To include friend and WebSocket checks:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File "D:\projects\cpp_backend_server\tests\user_devices_test.ps1" `
  -BaseUrl "http://localhost:8082" `
  -FriendUserId "0776df58-9cbe-4f81-b613-4439f6c3b566" `
  -SessionDeviceId "REPLACE_WITH_ALICE_ACTIVE_DEVICE_UUID"
```

The Bob UUID must exist and already be Alice's friend in the test database. Enter the password interactively; do not log tokens.

Limits: active status in returned rows alone does not verify filtering against pending/revoked fixtures. Key authenticity, prekey claims, successful delivery, Online_Sync, offline sync, persisted ACKs, duplicate delivery and revoke races still require independent acceptance. The script does not register/revoke devices, consume prekeys or insert messages, and does not verify end-to-end encryption.
