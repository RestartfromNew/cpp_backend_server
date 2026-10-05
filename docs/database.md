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
|username|TEXT|Unique|在系统中唯一的用户名，用于查找好友|
|   is_active   |   BOOLEAN    |Default True|
|  created_at   | TIMESTAMPTZ  | Default NOW()|
|  updated_at   | TIMESTAMPTZ  |  Default NOW()|

约束
```
CONSTRAINT users_display_name_not_blank
CHECK (
    length(trim(display_name)) > 0
)
ALTER TABLE app.users 
ADD CONSTRAINT users_check CHECK (username ~ '^[a-z0-9_.]{3,30}$');

```
3-30个字符，包括英文，数字，英文点号.,下划线

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

# 一对一实时聊天系统模块
## 对象
两个已经完成注册的用户

## 基本功能 
两方同时在线时，一方用户发送一条消息，对方能够实时接收并实现持久化存储
一方不在线时，发送消息实现持久化存储，在对方上线时能收到消息更新
消息类型包括文字、图片、文件，暂时不支持视频发送
文字能实现持久化存储，图片和文件有失效时限
消息实现端到端加密

## 数据表

以下为 PostgreSQL 建表设计，统一使用现有项目的 `app` schema 和 `app.users(id)`。前提是认证系统迁移已完成，且这四张聊天表尚未创建。按文档顺序执行四个 SQL 块；附件外键在附件表创建后补上，因此不能只执行消息表代码就认为所有约束已经建立。正式迁移时应将四块放入同一事务，并使用现有 `owner_role` 创建，以沿用项目默认表权限；本节不是修改已有表的 ALTER 迁移。

### 好友请求表 friend_request
| 字段 | 类型 | 约束 | 说明 |
|---|---|---|---|
| `id` | UUID | 主键；默认自动生成 | 申请唯一标识 |
| `requester_id` | UUID | 非空；外键 → `app.users(id)` | 申请发起者 |
| `recipient_id` | UUID | 非空；外键 → `app.users(id)` | 申请接收者 |
| `status` | TEXT | 非空；默认 `pending` | 申请状态 |
| `message` | TEXT | 可空；最长 200 字符 | 申请说明 |
| `created_at` | TIMESTAMPTZ | 非空；默认当前时间 | 创建时间 |
| `processed_at` | TIMESTAMPTZ | 可空 | 接受、拒绝或取消的时间 |

| 当前状态 | 操作 | 操作人 | 新状态 |
|---|---|---|---|
| `pending` | 接受 | 接收者 | `accepted` |
| `pending` | 拒绝 | 接收者 | `rejected` |
| `pending` | 取消 | 发起者 | `cancelled` |

### 好友关系 friend_relation

一个好友关系在数据库中成对出现，例如 `(A, B)` 和 `(B, A)`。每行 `id` 标识一个方向的记录，不是两行共用的关系 ID。`status` 保留当前约定的 `pending`、`accept`、`blocked`：blocked 表示本行 user_id 屏蔽 friend_id，不自动表示反方向也屏蔽。

| Field Name | Type | Constraint | Description |
|---|---|---|---|
| id | UUID | Primary Key; Not Null | 单向好友记录的唯一标识 |
| user_id | UUID | FK → app.users(id); ON DELETE CASCADE; Not Null | 当前方向的用户 |
| friend_id | UUID | FK → app.users(id); ON DELETE CASCADE; Not Null | 对方用户 |
| created_at | TIMESTAMPTZ | Not Null; Default CURRENT_TIMESTAMP | 创建时间 |
| updated_at | TIMESTAMPTZ | Not Null; Default CURRENT_TIMESTAMP | 更新时由触发器刷新 |

```sql
CREATE TYPE app.friend_relation_status AS ENUM (
    'pending', 'accept', 'blocked'
);

CREATE TABLE app.friend_relation (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID NOT NULL
        REFERENCES app.users(id) ON DELETE CASCADE,
    friend_id UUID NOT NULL
        REFERENCES app.users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT friend_relation_not_self
        CHECK (user_id <> friend_id),
    CONSTRAINT friend_relation_direction_unique
        UNIQUE (user_id, friend_id),
    -- 事务提交时必须存在反方向记录；不要求两个方向的 status 相同。
    CONSTRAINT friend_relation_reverse_exists
        FOREIGN KEY (friend_id, user_id)
        REFERENCES app.friend_relation (user_id, friend_id)
        ON DELETE NO ACTION
        DEFERRABLE INITIALLY DEFERRED
);

-- UNIQUE(user_id, friend_id) 已支持按 user_id 查询；补充反向索引。
CREATE INDEX friend_relation_friend_id_idx
    ON app.friend_relation (friend_id);

CREATE FUNCTION app.touch_friend_relation_updated_at()
RETURNS trigger
LANGUAGE plpgsql
AS $$
BEGIN
    NEW.updated_at := statement_timestamp();
    RETURN NEW;
END;
$$;

CREATE TRIGGER friend_relation_touch_updated_at
BEFORE UPDATE ON app.friend_relation
FOR EACH ROW
EXECUTE FUNCTION app.touch_friend_relation_updated_at();
```

成对插入应使用一条多行 INSERT，或在同一事务内插入两行；删除关系也应在同一事务删除两个方向。只插入一个方向后提交会违反延迟外键。确认好友时需由业务事务更新双方状态，不能由客户端任意设置 accept。当前字段没有记录申请发起者，也不能恢复屏蔽前状态；好友申请的发起/接受权限、解除屏蔽后的状态仍需补充规则或独立申请/屏蔽表，不能声称这张表已覆盖完整好友工作流。

### 私聊会话表 direct_conversation

`last_seq` 是会话内最新已提交消息的序号，不是消息 UUID；无消息时为 0。发送新消息时，在同一事务内更新该计数器并插入消息，不能单独递增后再另外提交消息。成员 ID 按 PostgreSQL UUID 比较规则规范化，较小者放 member_id_one。

| Field Name | Type | Constraint | Description |
|---|---|---|---|
| id | UUID | Primary Key; Not Null | 私聊会话唯一标识 |
| member_id_one | UUID | FK → app.users(id); ON DELETE RESTRICT; Not Null | 较小的成员 ID |
| member_id_second | UUID | FK → app.users(id); ON DELETE RESTRICT; Not Null | 较大的成员 ID |
| last_seq | BIGINT | Not Null; Default 0; CHECK ≥ 0 | 最新已提交消息序号；不是外键 |
| created_at | TIMESTAMPTZ | Not Null; Default CURRENT_TIMESTAMP | 创建时间 |

```sql
CREATE TABLE app.direct_conversation (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    member_id_one UUID NOT NULL
        REFERENCES app.users(id) ON DELETE RESTRICT,
    member_id_second UUID NOT NULL
        REFERENCES app.users(id) ON DELETE RESTRICT,
    last_seq BIGINT NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT direct_conversation_members_ordered
        CHECK (member_id_one < member_id_second),
    CONSTRAINT direct_conversation_pair_unique
        UNIQUE (member_id_one, member_id_second),
    CONSTRAINT direct_conversation_last_seq_nonnegative
        CHECK (last_seq >= 0)
);

CREATE INDEX direct_conversation_member_second_idx
    ON app.direct_conversation (member_id_second);
```

两个成员字段在会话建立后由业务层保持不变。这里采用 RESTRICT，避免硬删除账号时意外级联删除另一方的聊天历史；普通账号停用沿用 users.is_active，正式注销的数据处理规则以后单独定义。会话不外键关联好友记录，因此解除好友关系不会自动删除会话历史。

### 消息表 message

| 字段 | 类型 | 约束或含义 |
|---|---|---|
| id | UUID | 主键，一条消息的唯一标识 |
| conversation_id | UUID | 非空，引用私聊会话；ON DELETE RESTRICT |
| sender_id | UUID | 非空，引用发送用户；ON DELETE RESTRICT |
| seq | BIGINT | 非空且大于 0，会话内序号 |
| client_message_id | UUID | 非空，客户端为本次发送生成，重试保持不变 |
| message_type | TEXT | 非空，限制为 text、image、file |
| payload_ciphertext | BYTEA | 非空且长度大于 0，加密后的消息内容 |
| attachment_id | UUID | 可空，引用附件；文字为空，图片/文件非空 |
| created_at | TIMESTAMPTZ | 非空，服务端记录的创建时间 |

```sql
CREATE TABLE app.message (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    conversation_id UUID NOT NULL
        REFERENCES app.direct_conversation(id) ON DELETE RESTRICT,
    sender_id UUID NOT NULL
        REFERENCES app.users(id) ON DELETE RESTRICT,
    seq BIGINT NOT NULL,
    client_message_id UUID NOT NULL,
    message_type TEXT NOT NULL,
    payload_ciphertext BYTEA NOT NULL,
    -- app.attachment 在下一节创建，外键在下一节末尾添加。
    attachment_id UUID,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT message_seq_positive
        CHECK (seq > 0),
    CONSTRAINT message_conversation_seq_unique
        UNIQUE (conversation_id, seq),
    CONSTRAINT message_client_request_unique
        UNIQUE (conversation_id, sender_id, client_message_id),
    CONSTRAINT message_type_valid
        CHECK (message_type IN ('text', 'image', 'file')),
    CONSTRAINT message_ciphertext_not_empty
        CHECK (octet_length(payload_ciphertext) > 0),
    CONSTRAINT message_attachment_matches_type
        CHECK (
            (message_type = 'text' AND attachment_id IS NULL)
            OR
            (message_type IN ('image', 'file') AND attachment_id IS NOT NULL)
        )
);

CREATE INDEX message_sender_id_idx ON app.message (sender_id);

CREATE INDEX message_attachment_id_idx
    ON app.message (attachment_id)
    WHERE attachment_id IS NOT NULL;
```

UNIQUE(conversation_id, seq) 已生成索引，支持会话内按序号翻页，无需建立相同的普通索引。每条消息最多引用一个附件；此处不额外规定“一个附件绝不能被多条消息引用”，如将来限制复用再明确业务规则。

这些约束不自动分配 seq，也不会验证 sender_id 是两个会话成员之一。发送事务仍必须从认证身份确定发送者、检查成员及好友权限，锁定会话并更新 last_seq，插入消息后一起提交；重试须先识别已有请求，并检查其会话、类型、密文及附件是否与原提交一致。同一发送标识不允许被用来修改原消息。密文封装的协议版本及必要协议头由加密协议定义；BYTEA 本身不实现端到端加密，服务端不保存明文解密密钥。

### 附件表 attachment

| 字段 | 类型 | 约束或含义 |
|---|---|---|
| id | UUID | 主键，附件唯一标识 |
| uploader_id | UUID | 非空，上传者；FK → app.users(id)，ON DELETE RESTRICT |
| storage_key | TEXT | 非空、非空白且唯一，定位加密文件 |
| size_bytes | BIGINT | 非空且不小于 0，最终 ready 时应为服务端确认的实际对象大小 |
| upload_status | TEXT | 非空，默认 uploading；允许 uploading、ready、failed |
| created_at | TIMESTAMPTZ | 非空，上传记录创建时间 |
| expires_at | TIMESTAMPTZ | 可空，下载截止时间；具体起算规则待确定 |
| deleted_at | TIMESTAMPTZ | 可空，实际文件删除完成时间 |

```sql
CREATE TABLE app.attachment (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    uploader_id UUID NOT NULL
        REFERENCES app.users(id) ON DELETE RESTRICT,
    storage_key TEXT NOT NULL,
    size_bytes BIGINT NOT NULL,
    upload_status TEXT NOT NULL DEFAULT 'uploading',
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMPTZ,
    deleted_at TIMESTAMPTZ,

    CONSTRAINT attachment_storage_key_unique UNIQUE (storage_key),
    CONSTRAINT attachment_storage_key_not_blank
        CHECK (length(btrim(storage_key)) > 0),
    CONSTRAINT attachment_size_nonnegative CHECK (size_bytes >= 0),
    CONSTRAINT attachment_upload_status_valid
        CHECK (upload_status IN ('uploading', 'ready', 'failed')),
    CONSTRAINT attachment_expiration_valid
        CHECK (expires_at IS NULL OR expires_at >= created_at),
    CONSTRAINT attachment_deletion_time_valid
        CHECK (deleted_at IS NULL OR deleted_at >= created_at)
);

CREATE INDEX attachment_uploader_id_idx ON app.attachment (uploader_id);

CREATE INDEX attachment_pending_expiry_idx
    ON app.attachment (expires_at)
    WHERE expires_at IS NOT NULL AND deleted_at IS NULL;

-- 必须执行：完成上一节 message 的附件引用约束。
ALTER TABLE app.message
    ADD CONSTRAINT message_attachment_fk
    FOREIGN KEY (attachment_id)
    REFERENCES app.attachment(id)
    ON DELETE RESTRICT;
```

expires_at 允许在上传准备阶段为空，不表示已发送附件可以永久有效。按最终选择的保留期起算规则，发布附件消息时应保证期限已经设置且尚未到期。附件过期清理的是对象存储中的文件，成功后更新 deleted_at；保留附件记录和消息，以显示“附件已过期”，不删除引用关系。

提交附件消息时，业务事务还需检查 uploader_id 与发送者匹配、upload_status = ready、deleted_at IS NULL、期限有效，并协调消息绑定与清理任务之间的并发。外键仅保证记录存在，不能保证这些可变状态。下载必须检查请求者的会话权限和期限；存储对象不能设为永久公开访问。未发送附件的清理期限另行规定。


## 主用户用例
消息发送分为
- 发送中 ： 消息成功从发送方客户端发出，但是消息未被持久化和成功推送到对方
- 已发送 ：服务器成功持久化消息
- 已送达 ： 消息成功持久化并送达到对方客户端
### 发送消息

发送成功以持久化成功为边界

| 项目 | 内容 |
|---|---|
| 用例编号 | UC-Chat-01 |
| 目标 | 发送方向一对一会话提交消息，系统可靠保存并返回提交结果（持久化成功） |
| 主要参与者 | 发送消息的用户（因为对方可以在线也可以离线） |
| 其他参与者 | 接收消息的好友 |
| 触发条件 | 发送方点击发送 |
| 输入 | 消息内容；图片、文件消息包含已完成上传的附件引用 |
| 前置条件 | 发送方已登录且账号有效；双方满足好友及发送权限规则；接收方可以在线或离线（这很重要）；用户状态+业务规则+对方状态|
| 成功后置条件 | 消息已持久化保存；发送方收到成功确认后更新本地状态；消息可供接收方获取 |
| 失败后置条件 | 明确未提交时，不产生本次新消息，不向接收方投递；结果无法确认时，保留客户端待确认状态，允许安全重试 |

`主流程`
-  A在给B发送消息的窗口中准备消息
- A确认发送消息
- 服务器（系统）接收到消息，验证此次消息是否符合发送标准
- 系统持久化本次消息成功
- 系统返回此次消息已发送成功的结果
- 系统检测B用户在线情况，尝试进行消息推送

1. A

### 离线用户推送未接收消息

| 项目 | 内容 |
|---|---|
| 用例编号 | UC-Chat-02 |
| 目标 | 用户上线或恢复连接后，客户端补齐尚未接收的消息 |
| 主要参与者 | 上线或恢复连接的接收方 |
| 触发条件 | 客户端完成登录、恢复连接，或发现消息遗漏 |
| 输入 | 客户端已有的消息同步进度；首次同步时可以没有进度 |
| 前置条件 | 用户已通过认证；系统存在该用户有权获取的消息 |
| 成功后置条件 | 本轮同步范围内的缺失消息已在客户端保存并更新界面；同步进度推进；重复消息不重复展示 |
| 失败后置条件 | 服务端已保存的消息保持不变；客户端保留已完成的同步结果，未完成部分允许再次同步 |

## UC-Chat-01：按六个方面逐步追问（需求分析练习）

下面沿用上面的六个主流程步骤，演示如何提出问题。表格中的问句是需求检查清单，不表示这些问题都已经有确定答案，也不要求一次全部解决。先从具体场景回答，再将答案整理为业务规则、异常分支或关联流程；数据库表、Redis、线程模型等实现选择留到技术设计。

固定一个正常场景作为起点：A、B 已注册并成为好友，双方账号有效；A 在私聊窗口发送一条文字消息，消息被服务端保存，B 在线接收。每次只改变一个条件，例如把 B 改成离线，或让 A 在等待确认时断网。

六个追问角度：

| 角度 | 想弄清楚什么 |
|---|---|
| 输入 | 这一步需要哪些信息？有哪些类型、范围和限制？ |
| 条件与权限 | 谁可以执行？执行前必须满足什么？ |
| 结果与状态 | 什么算完成？完成后什么发生改变，谁知道这个结果？ |
| 失败与中断 | 开始前、执行中、完成后中断，分别留下什么？ |
| 重复与并发 | 同一操作重复执行，或相关操作同时发生，会怎样？ |
| 时间与后续 | 要等多久？之后什么事件会改变状态？下一步依赖什么？ |

### 步骤 1：A 在给 B 发送消息的窗口中准备消息
（这部分给前端）

| 角度 | 可以直接向自己提出的问题 |
|---|---|
| 输入 | A 准备的是文字、图片还是文件？文字为空、只有空格或超过长度限制时怎么办？文件大小和类型有什么限制？ |
| 条件与权限 | A 能打开窗口是否就代表允许发送？A 离线或登录过期时，还能否编辑草稿？端到端加密所需条件尚未准备好时，是否允许先编辑？ |
| 结果与状态 | 什么叫“准备完成”：文字已经输入，还是附件也必须上传完成？草稿只在本地，还是需要恢复？此时不应把草稿当成已发送消息。 |
| 失败与中断 | 输入一半关闭窗口或应用，草稿是否保留？图片读取失败或上传中断，文字部分是否仍保留？ |
| 重复与并发 | 连续选择同一个附件表示添加两份还是替换？上传过程中删除或替换附件，之前的上传如何处理？ |
| 时间与后续 | 草稿保留多久？已经上传但最终没有发送的附件何时清理？附件在等待发送时过期，下一步是否需要重新上传？ |

追问如何导出边界：如果附件上传有独立的开始、进度、失败重试和取消，并且上传完成后 A 仍可取消发送，就可以提取“上传附件”关联流程。文字长度、附件大小则形成输入规则，暂不必拆成独立用例。具体数值尚未决定时标为“待决定”。

### 步骤 2：A 确认发送消息

| 角度 | 可以直接向自己提出的问题 |
|---|---|
| 输入 | A 点击发送时，提交的是当时的哪一份内容？是否包含会话标识、消息类型、密文或附件引用，以及用于识别本次发送的标识？ |
| 条件与权限 | 附件仍在上传时能否发送？本地离线、登录过期或加密准备失败时，按钮应该允许什么操作？ |
| 结果与状态 | 点击后是否立即显示一个“发送中”的消息项？输入框何时清空？此时用户能否继续准备下一条消息？ |
| 失败与中断 | 点击后请求根本没有发出，界面显示什么？请求可能已发出但客户端退出，重新打开后如何保留或恢复这次待确认操作？ |
| 重复与并发 | 快速双击算一次还是两次发送？主动再次发送相同文字，与重试同一次提交，如何区分？两条消息连续提交时，用户期望怎样排序？ |
| 时间与后续 | 等待多久后提示结果待确认？采用自动重试还是手动重试？等待期间能否取消，取消是停止重试还是要求撤回可能已提交的消息？ |

追问如何形成规则：两条内容相同的消息不一定重复；A 可能确实想说两次“你好”。需要去重的是同一次提交的重试。可先记录“同一次提交必须可被识别，重试不新增消息”；如何生成标识留待协议设计。

### 步骤 3：服务器接收到消息，验证此次消息是否符合发送标准

| 角度 | 可以直接向自己提出的问题 |
|---|---|
| 输入 | 收到的消息是否具备必要字段，字段类型和大小是否允许？附件引用是否有效？是否能判断同一发送标识对应的内容发生了不允许的改变？ |
| 条件与权限 | A 是否已认证且账号有效？A 是否属于这个会话？双方是否满足好友、屏蔽及发送频率规则？A 是否有权引用该附件？ |
| 结果与状态 | 验证通过只代表允许继续提交，还是已经发送成功？不通过时应向 A 返回什么类别的原因，客户端保留什么内容？ |
| 失败与中断 | 查询权限所需的信息暂时不可用时，能否继续接受消息？如何区分“明确没有权限”和“暂时无法验证”？ |
| 重复与并发 | 重试的是此前已经提交成功的消息时，应如何查询和返回结果？验证期间 B 删除好友、拉黑 A，或 A 被禁用，以哪个业务时点的权限为准？ |
| 时间与后续 | 检查通过很久后才进入保存阶段，权限结论是否仍有效？是否需要在正式接受消息时保证权限判断与保存之间的一致性？ |

端到端加密下的追问边界：服务端通常不能直接读取正文，因此要分别列出客户端能够检查的明文输入规则，以及服务端能够检查的身份、消息封装、大小和附件权限等规则。不能默认服务端可以解密并检查文字内容，也不能仅凭客户端声称“已检查”就信任其请求。

可能提取的内容：“检查发送资格”仍是本用例步骤；好友、屏蔽、限流规则放入公共规则。具体权限判定时点需要决定，不在本练习中擅自确定。

### 步骤 4：系统持久化本次消息成功

| 角度 | 可以直接向自己提出的问题 |
|---|---|
| 输入 | 需要可靠保存哪些内容：消息密文、发送者、会话、附件引用和发送标识？哪些信息来自认证结果，不能直接相信客户端填写的值？ |
| 条件与权限 | 本次提交是否已满足发送条件？引用的附件是否已完成上传且可用于本次消息？正式接受时，相关权限如何保持有效？ |
| 结果与状态 | 哪些信息都保存成功才算一次完整提交？是否产生稳定的消息标识和排序依据？这一步成功与 B 是否在线有没有关系？ |
| 失败与中断 | 保存前失败是否没有新增消息？保存一半失败能否留下不完整记录？提交时连接中断，无法判断是否保存成功，应该怎样表达这个结果？ |
| 重复与并发 | 同一次提交并发到达两次，能否保证只产生一条消息？A、B 同时发送时，会话中的顺序如何确定？相同发送标识携带不同内容，应该如何处理？ |
| 时间与后续 | 保存多久没有结果应进入待确认或超时处理？保存成功后进程立即退出，消息能否在重启后被查询，并继续供接收方获取？ |

追问示例：

1. 问：已经保存成功，但服务端还没有发出任何确认就退出，消息应该删除吗？
2. 推导：不应因为后续确认或推送未完成而撤销已成功的提交。
3. 再问：重启后 A 重试，会不会再创建一条？
4. 推导：需要识别同一次提交，返回原结果；不能靠确认是否发出来判断是否已经保存。

这些答案可以整理为持久化与幂等规则。数据库事务、唯一约束和可靠通知机制属于实现这些规则的候选方案，不需要在需求追问时立即定下。

### 步骤 5：系统返回此次消息已发送成功的结果

| 角度 | 可以直接向自己提出的问题 |
|---|---|
| 输入 | 返回结果依据的是什么事实？结果包含哪些信息，才能对应 A 的原始发送请求并指向已保存的消息？ |
| 条件与权限 | 是否只有确认持久化成功，才能返回成功？如果提交结果未知，是否必须使用不同的结果表达？ |
| 结果与状态 | 成功表示“已持久化”还是“B 已收到”？A 收到后更新哪条本地消息？服务端成功与客户端知道成功，是否是两个不同事实？ |
| 失败与中断 | 消息保存成功，但响应丢失，A 显示什么？A 收到响应后尚未记录本地状态就退出，重新打开后怎么办？ |
| 重复与并发 | A 未收到确认而重试时，返回什么？原确认和重试确认先后到达，是否会重复展示消息？迟到的超时处理能否把已成功的消息改回失败？ |
| 时间与后续 | 等待确认多久后进入待确认状态？等待期间 B 能否已经收到消息？后续推送是否必须等待 A 再次确认收到结果？ |

基于当前成功边界，可以先形成以下规则：

- 成功确认表示消息已被接受并持久化，不表示 B 已送达或已读。
- A 未收到确认时，不能直接断言服务端没有保存，应允许用同一发送标识重试。
- A 的确认接收情况不决定是否保留已持久化的消息；当前提交流程不要求“确认的确认”。
- 第 5 步和第 6 步都依赖持久化成功，但第 6 步不必等待 A 收到第 5 步的结果。

对前面“发送中”定义的待修订提示：从客户端角度，发送中或待确认表示“尚未确认提交结果”，不能据此断言服务端尚未持久化，也不能据此断言 B 尚未收到。

### 步骤 6：系统检测 B 用户在线情况，尝试进行消息推送

| 角度 | 可以直接向自己提出的问题 |
|---|---|
| 输入 | 推送的是完整消息，还是新消息通知？接收目标是 B 的哪台设备或哪些连接？客户端怎样识别所属会话及消息？ |
| 条件与权限 | B 是否仍有权获取这条消息？如果后来解除好友关系，以前已经发送的消息如何处理？在线信息是否只是一个可能过时的判断？ |
| 结果与状态 | 什么算“已送达”：服务端写出数据，还是 B 的客户端确认已接收并保存？端到端加密下，收到密文与成功解密是否要分别表达？已送达不能直接表示已读。 |
| 失败与中断 | B 本来离线怎么办？检测时在线、推送时断线怎么办？B 收到但接收确认丢失怎么办？附件已过期是否影响消息记录本身的接收？ |
| 重复与并发 | 实时推送和上线补拉同时包含同一条消息，是否重复展示？推送重试如何去重？若支持多设备，一台收到是否代表其他设备也收到？ |
| 时间与后续 | 多久没收到接收确认应重试或交给恢复同步？B 上线后从哪里继续？B 很久不上线，消息保留和附件过期分别如何处理？ |

追问如何导出边界：投递失败不应撤销提交成功。投递可以有独立的尝试、确认与重试状态，因此适合提取关联流程；B 上线或重连是新的触发条件，进入 UC-Chat-02 恢复同步。是否支持多设备、已送达具体定义、重试时限等，尚需决定。

### 将问题整理成需求，而不是停留在问题列表

每次选一个问题，按“具体场景 → 预期行为 → 分类 → 验收”填写，不必同时回答全部六张表。

| 具体场景 | 预期行为示例 | 放到哪里 | 如何验收 |
|---|---|---|---|
| A 点击发送后，服务端已保存，但确认丢失 | 客户端保持结果待确认；同一次提交重试不重复创建消息 | 第 5 步异常分支＋幂等规则 | 模拟丢失确认，再重试，最终只有一条正式消息 |
| 附件上传完成，但 A 取消发送 | 不产生正式消息；未引用附件按待确定期限清理 | 上传附件关联流程＋清理规则 | 取消后会话无新增消息；到期后无主附件按规则处理 |
| B 离线，A 提交成功 | 消息保留；B 恢复连接后可以同步 | 第 6 步分支，关联 UC-Chat-02 | B 离线期间发送，恢复后完整获取且无重复 |
| B 同时从推送与补拉收到同一消息 | 客户端只展示一次 | 接收去重规则 | 同一消息经两条路径到达，最终仅一个消息项 |

“预期行为示例”中尚未确定的产品选择要标为待决定，不把问题本身当成已经承诺的需求。尤其不要在本次练习中随意规定文件大小、保留天数、重试次数或多设备策略。

可复用记录模板：

```text
所属步骤：
追问角度：
问题：
具体场景（本次只改变一个条件）：
预期行为／待决定：
成功或失败后保留的状态：
归类：主步骤／异常分支／关联流程／公共规则／技术设计
验收场景：
```

当某一步的正常结果、中断后状态和重复执行行为能够说明白，并能写出具体的验收场景，就可以先继续下一步；不要求穷尽所有可能情况后才开始实现。
