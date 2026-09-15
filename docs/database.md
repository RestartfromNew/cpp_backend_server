# 用户系统构思
用户模块职责范围：该模块负责维护用户在系统中的身份信息；实现新用户注册（包括第三方授权登录），登录验证，修改验证登录信息，解绑第三方授权身份，为其它功能提供用户身份信息
## 不变量规则
- 一个用户是一个实体，用id表示，是该用户在系统中唯一标识
- 一个用户可以通过邮箱+密码的方式实现注册
- 一个用户可以通过邮箱+密码的方式实现登录
- 一个用户可以用来登录的邮箱是唯一的
- 一个未注册用户可以通过第三方认证的方式实现注册（如Google）
- 一个没有进行密码注册的用户，通过第三方认证注册成功后，可以通过该认证再次登录同一个用户
- 一个第三方注册的用户，可以后续通过绑定邮箱+密码，实现邮箱+密码登录
- 一个注册成功的用户，可以继续绑定其它第三方认证
- 一个拥有至少一个登录方式的用户，可以解绑其它第三方认证。在系统中至少存在一个登录方式（密码或者第三方授权）

# 系统概念
- User
- PasswordCredential
- ExternalIdentity
- Session

## user实体

|  Field Name   |     Type     | Constraint | Description|
|:-------------:|:------------:|:----:|:----:|
|      id       |     UUID     |primary key;Not null| 系统中标识用户实体的唯一id
| display_name  |     TEXT     |Not null|
|   is_active   |   BOOLEAN    |Default True|
|  created_at   | TIMESTAMPTZ  | Default NOW()|
|  updated_at   | TIMESTAMPTZ  |  Default NOW()|

约束
```
CONSTRAINT users_display_name_not_blank
CHECK (
    length(trim(display_name)) > 0
)
```

## password_credential
- 一个未注册用户通过邮箱+密码注册成功之后，表中保存email+password_hash两个记录；注册成功的用户可以通过该方式登录
- 一个通过第三方注册的用户默认在此表中没有记录；通过手动添加primary email和password在该表中建立记录，并可以用这种方式登录
- email在该表中是唯一的，第三方登录ID的email记录在external_credential中

|  Field Name   | Type |                           Constraint                            |      Description       |
|:-------------:|:----:|:---------------------------------------------------------------:|:----------------------:|
|    user_id    | UUID | Primary Key;Foreign Key → user(id); On Delete Cascade; Not Null |   和user表用的id关联   |
|  login_email  | TEXT |                         unique;Not null                         |
| password_hash | TEXT |                            Not null                             |
|email_verified_at|TIMESTAMPTZ | | 如果后面需要加验证功能 |
|password_changed_at|TIMESTAMPTZ |Not NULL; default NOW()|                        |
|  created_at   | TIMESTAMPTZ  | Default NOW()|

约束
```
CREATE UNIQUE INDEX
password_credentials_login_email_unique
ON app.password_credential(
    lower(login_email)
);

```

## external_credential
- 一个用户可以拥有多个external_credential记录，表示多个认证
- 如果没有提供email和display_name，那么就为空；没有经过设置的email不可以用于邮箱+密码登录

|  Field Name  |    Type     |                     Constraint                      |               Description                |
|:------------:|:-----------:|:---------------------------------------------------:|:----------------------------------------:|
|      id      |    UUID     |primary key;Not null|            表示一条记录的主键            |
|   user_id    |    UUID     | Foreign Key → user(id); On Delete Cascade; Not Null |            和user表用的id关联            |
|    issuer    |    TEXT     | Not Null|              谁签发的ID令牌              |
|   subject    |    TEXT     | Not Null|        用户在该issuer中的身份标识        |
| display_name |    TEXT     | |                 用户名称                 |
|    email     |    TEXT     | | 用户在该provider中的邮箱，是一个附加信息 |
|email_verifed| BOOLEAN| |                                          |
|  created_at  | TIMESTAMPTZ | Default NOW()|
|  updated_at  | TIMESTAMPTZ |  Default NOW()|

一个issuer的一个认证用户在表中只能唯一
```
CONSTRAINT external_identities_issuer_subject_unique
        UNIQUE (
            issuer,
            subject
        )
        
```
一个系统用户只能绑定某个第三方授权的一个subject; 例如一个用户不能对应多个Google account
```
CONSTRAINT external_identities_user_issuer_unique
    UNIQUE (
        user_id,
        issuer
    )

```
## refresh_token

| Field Name |     Type    |                      Constraint                      | Description                                                      |
|:----------:|:----:|:----------------------------------------------------:|:----:|
|     id     |     UUID    |   Primary Key; Not Null; Default gen_random_uuid()   | Unique identifier for each refresh token                         |
|  user_id   |     UUID    | Foreign Key → users(id); On Delete Cascade; Not Null | References the user who owns this token                          |
| token_hash |     TEXT    |                  Not Null  ;UNIQUE                   | Hashed version of the refresh token (never store raw token)      |
| expires_at | TIMESTAMPTZ |                       Not Null                       | Expiration timestamp of the refresh token                        |
| revoked_at |   TIMESTAMPTZ  |                                                      | Indicates whether the token has been revoked (logout / rotation) |
| created_at | TIMESTAMPTZ |           Not Null          Default NOW()            | Timestamp when the token was created                             |


# 注册流程用例
|     项目     |                        内容                        |
|:------------:|:--------------------------------------------------:|
|   用例编号   |                     UC-Auth-01                     |
|     目标     |   访问者创建一个系统账号，可以使用与账号密码登录   |
|  主要参与者  |                 未创建系统账号用户                 |
|   触发条件   |                  用户提交注册信息                  |
|     输入     |                     邮箱+密码                      |
|   前置条件   |        系统开放注册；访问者无需事先拥有账号        |
| 成功后置条件 | 创建一个用户及对应的密码凭据，两者通过用户 ID 关联 |
| 失败后置条件|         本次操作不留下部分创建的账号数据           |

## 主成功流程
- 访问者提交显示名称、邮箱和密码。
- 系统检查必填项及输入规则。
- 系统按照统一规则处理显示名称和邮箱。
- 系统生成密码哈希。
- 系统创建用户，并为其建立密码凭据。
- 系统确认用户和凭据均已持久化成功。
- 系统返回注册成功和公开用户信息。
- 访问者可以前往登录；本次注册不建立登录状态。

## 错误断点
| 编号 | 发生位置与条件 | 系统行为 | 最终状态 |
|---|---|---|---|
| A-01 | 第 2 步：必填项缺失或输入不符合规则 | 返回可定位到字段的输入错误 | 不创建账号 |
| A-02 | 第 5 步：登录邮箱已被占用 | 按本例约定返回“邮箱不可用于注册” | 不创建额外用户或凭据 |
| E-01 | 第 4 步：密码处理失败 | 返回暂时无法完成注册，记录内部诊断信息 | 不创建账号 |
| E-02 | 第 5～6 步：持久化失败，且确认未提交 | 撤销本次部分写入，返回暂时无法完成注册 | 不留下半个账号 |
| E-03 | 提交阶段连接中断，无法确认提交结果 | 返回暂时无法确认结果，记录诊断信息 | 可能完整创建，也可能未创建；不能出现部分创建 |
| E-04 | 第 6 步成功后，响应传输失败 | 已创建的数据保留 | 账号存在，但访问者可能没有收到成功提示 |


## 输入和业务规则 Restriction
| 编号 |                               规则                               |
|:----:|:----------------------------------------------------------------:|
| R-01 |         display_name去除空格后不能为空；该字段可以不唯一         |
| R-02 |             邮箱去除空格；格式和长度符合邮箱校验规则             |
|R-03| 	邮箱按照大小写不敏感的方式比较；不能与已有密码凭据的登录邮箱重复 |
|R-04	|           不对邮箱做去点、去除 + 后缀等供应商特定改写            |
|R-05	|  密码必须满足统一密码策略；系统不得悄悄裁剪密码或去除其首尾空格  |
|R-06	|         只保存密码哈希，响应和日志中不出现密码或密码哈希         |
|R-07	|               新用户处于启用状态，邮箱验证时间为空               |
|R-08	|              注册成功必须同时存在用户和对应密码凭据              |
|R-09	|           第三方身份中的邮箱相同，不自动关联或合并账号           |
|R-10	|                       注册成功不自动登录                         |

# 邮箱+密码登录流程用例

|     项目     |                                     内容                                      |
|:------------:|:-----------------------------------------------------------------------------:|
|   用例编号   |                                  UC-Auth-02                                   |
|     目标     |            已经创建账号的用户，使用邮箱+密码验证用户身份，实现登录            |
|  主要参与者  |                         已创建邮箱+密码登录方式的用户                         |
|   触发条件   |                          用户提交邮箱+密码的登录信息                          |
|     输入     |                                   邮箱+密码                                   |
|   前置条件   | 系统开放注册；访问者通过邮箱+密码注册，或第三方授权登录后绑定和设置邮箱和密码 |
| 成功后置条件 |            返回登录成功提示，返回生成的Access_token和refresh_token            |
| 失败后置条件|                  返回登录失败提示和原因，本次操作不生成token                  |

## 主成功流程
- 用户提交邮箱+密码
- 系统检查必填项及输入规则
- 系统查找根据邮箱密码凭证和对应用户
- 系统检查登录限流状态
- 系统验证该用户存在并处于可登录状态
- 系统验证密码
- 系统生成access_token(应该先生成access_token以免refresh_token保存成功但是access_token生成失败，导致客户端无法得知)
- 系统生成refresh_token
- 系统计算refresh_token_hash
- 系统确认refresh_token_hash持久化保存成功
- 系统返回登录成功提示，返回access_token和refresh_token
# 错误断点
| 编号 | 条件 | 对外行为 | 内部行为 |
|---|---|---|---|
| A-01 | JSON 无效、字段缺失或格式错误 | `400 invalid_request`，可指出字段问题 | 不查询凭证 |
| A-02 | 请求超过登录频率限制 | `429 too_many_attempts` | 记录安全事件 |
| A-03 | 邮箱不存在 | `401 invalid_credentials` | 内部记录 `EmailNotFound` |
| A-04 | 密码不匹配 | `401 invalid_credentials` | 增加失败计数 |
| A-05 | 用户被禁用 | `401 invalid_credentials` 或 `403 account_disabled` | 内部记录用户状态 |
| E-01 | token 生成失败 | `500 internal_error` | 不写入数据库 |
| E-02 | refresh token 保存失败 | `500 internal_error` | 回滚事务，不返回 token |
| E-03 | 数据库不可用 | `503 service_unavailable` | 记录详细错误 |

## 输入和业务规则 Restriction
| 编号 |                                   规则                                   |
|:----:|:------------------------------------------------------------------------:|
| R-01 |                     JSON 格式有效，字段完整没有缺失                      |
| R-02 | 邮箱去除空格；格式和长度符合邮箱校验规则；邮箱按照大小写不敏感的方式比较 |
| R-03 | 密码必须满足合理的输入长度限制，防止空密码和超大请求造成资源消耗 |
| R-04 | 只有存在密码凭证且用户处于可登录状态时，才能登录成功 |
| R-05 | 使用密码哈希库验证明文密码与 password_hash 是否匹配；不直接比较两个哈希值 |
| R-06	 |             只比较密码哈希，响应和日志中不出现密码或密码哈希             |
| R-07	 |            邮箱或者密码验证失败，只向前端返回401 Unauthorized            |
| R-08	 |             登录成功必须同时返回access_token和refresh_token              |
| R-09	 |              数据库中不保存refresh_token明文，只保存hash值               |
|R-11|	access token 和 refresh token 必须设置有效期；refresh token 有效期应更长|
|R-12	|refresh token 必须由密码学安全随机数生成器生成，且具有足够的随机性|
|R-13	|登录接口必须限制单位时间内按 IP、邮箱等维度进行的失败尝试次数|
