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


---

# 端到端加密通信：设备、公钥、会话和密文投递设计

> 文档性质：设计补充，不表示这些功能已经实现，也不是可直接执行的数据库迁移。
> 本节沿用 `app.users`、`friend_request`、`friend_relation`、`direct_conversation`、`message` 和 `attachment` 的业务概念。新增表名为建议名称。
> 第一阶段建议先做单账号单有效设备、一对一文本和离线补拉；多设备作为后续扩展。此范围是实施建议，不是本次文档修改强制落地的产品限制。

## 1. 目标与最重要的边界

客户端生成和保管私钥，在客户端建立加密会话并完成消息加解密。服务器负责身份认证、好友权限、公钥资料分发、密文持久化和投递。服务器不持有正文解密密钥。

必须区分四种“会话”：

| 名称 | 属于哪一层 | 用途 | 能否用于解密正文 |
|---|---|---|---|
| 登录会话、access token、refresh token | 认证层 | 证明请求者有权使用账号 | 不能 |
| `WebSocketSession` | 网络层 | 管理连接、帧解析、收发缓冲区 | 不能 |
| `direct_conversation` | 聊天业务层 | 表示 A、B 的一个私聊窗口，组织消息与 seq | 不能 |
| 客户端加密会话 | 密码协议层 | 保存两台设备之间的根密钥、链状态、计数等协议资料 | 可以按协议派生消息密钥 |

登录成功不等于已经建立加密会话；成为好友不等于已经核验公钥；WebSocket 连接断开不应删除本地加密会话。HTTPS/WSS 仍然需要，E2EE 不替代传输层保护。

## 2. 贯穿全文的角色与数据归属

| 用户 | 设备 | 示例状态 |
|---|---|---|
| A | A1：手机 | 本次发送端 |
| A | A2：电脑 | 可选，用于同步自己发出的消息 |
| B | B1：手机 | 在线接收 |
| B | B2：电脑 | 离线，稍后补拉 |

`device_id` 表示应用安装实例的密码学身份，不是 MAC 地址、IMEI、socket fd 或每次登录新建的 token。重装应用且丢失私钥，通常应注册新设备，不能把新密钥静默塞回旧身份。

```text
账号 B
├── B1：自己的私钥 + 与各对端设备的会话状态
└── B2：另一套私钥 + 与各对端设备的会话状态

A1 发出一条逻辑消息 M100
├── E1：A1 → B1 的密文信封
├── E2：A1 → B2 的密文信封
└── E3：A1 → A2 的密文同步信封（支持自己的多设备时）
```

M100 只是阅读标签，数据库实际使用 UUID。一个逻辑消息只有一个会话内 seq；三个设备信封不能分别占三个业务 seq。

| 数据 | 客户端 | 服务器 |
|---|---|---|
| 身份私钥、预密钥私钥 | 对应设备安全保存 | 不保存 |
| 身份公钥、公开预密钥、签名 | 自己持有，按需获取对方资料 | 可以保存并分发 |
| 共享秘密 S、棘轮状态、消息密钥 | 协议库管理，按协议生命周期持久化或删除 | 不保存 |
| 聊天正文、本地聊天历史 | 解密后可保存；需保护本地存储 | 不保存明文 |
| 密文信封 | 发送前排队、接收后处理 | 持久化、路由、补拉 |
| 用户、设备、时间、大小等元数据 | 可以知道 | 为业务运行可能知道；E2EE 不隐藏全部元数据 |

## 3. 公钥包是什么，哪些字段参与计算

经典 X3DH 风格的单设备公钥包可以抽象为：

```json
{
  "user_id": "B",
  "device_id": "B1",
  "protocol_suite": "示例-X3DH-X25519",
  "identity_public_key": "32字节身份公钥的编码",
  "signed_prekey": {
    "key_id": 42,
    "public_key": "32字节预密钥公钥的编码",
    "signature": "协议规定的签名字节编码"
  },
  "one_time_prekey": {
    "key_id": 1007,
    "public_key": "32字节一次性预密钥公钥的编码"
  }
}
```

这不是“四个身份包”，而是一个设备的公钥包，其中有三段公钥和一段签名；一次性预密钥可能缺省。一次注册通常上传一批一次性预密钥，领取接口按握手返回其中一个。签名用于验证预密钥，不直接作为 DH 输入。公钥编码可以用 Base64 或 hex；编码不是加密。数据库可保存原始 `BYTEA`，避免把十六进制字符当密钥字节直接输入算法。

协议选择决定真实序列化方式。上面的 JSON 只是业务外壳，不是标准 Signal wire format，也不包含 PQXDH 需要的后量子资料。经典 X3DH 使用 XEdDSA 签名验证；不能把任意 Ed25519 签名或随机字符串冒充为可验证的 X3DH 签名。

## 4. 从注册设备、加好友到收信的完整流程

### UC-E2EE-01：注册或恢复设备

1. 用户按已有认证流程登录，获得 access token 和 refresh token。
2. 客户端检查本地是否仍有该账号、该设备的私钥和会话数据。
3. 首次安装时由协议库生成身份及预密钥材料，先可靠保存在本地，再注册公钥资料。上传失败时重试原注册，不反复生成不一致的身份。
4. 服务端从认证上下文确定 user_id，建立设备归属、授权状态、公钥记录。不能相信请求中任意填写的 user_id。
5. 设备注册请求带稳定幂等标识；相同注册请求重试返回同一设备。相同设备身份不能被另一账号接管。
6. 后续普通登录继续使用本地身份，按协议轮换预密钥、补充一次性公钥库存。
7. 本地密钥丢失时进入新设备/换机流程。仅重新登录账号不能恢复旧私钥。

设备注册应把账号认证身份与上传的密码身份按所选协议绑定，并验证必要的持有证明或授权材料。多设备阶段需独立设计旧设备确认或恢复授权，不能把“提交任意公钥”当作充分授权。

### UC-E2EE-02：A 添加 B 为好友

沿用现有 `friend_request` 的申请、接受流程以及双向 `friend_relation` 记录。好友接受和关系建立在业务事务中完成；此时无须交换私钥，也无须马上为所有设备建立会话。

好友状态和屏蔽规则仍由聊天业务检查。前文关于 `status` 的描述与当前 `friend_relation` DDL 不完全一致：DDL 没有 status 列。本节不假定该列已存在，具体权限实现须与最终好友模型统一。

好友关系确认用户是否可聊天；公钥指纹/安全码核验确认密码身份是否可信。签名只能证明预密钥受某身份私钥签署，不能独立证明服务器给的身份公钥就是用户认识的 B。

### UC-E2EE-03：A1 准备发送第一条消息

前置条件：A 已认证，A1 已授权，B 是可发送对象，目标设备至少有一台有效。

1. 客户端形成稳定的 `client_message_id`，本地记录待发送消息。
2. 获取或更新 B 的授权设备列表和列表版本；多设备时还获取 A 的其他授权设备。
3. 对每台目标设备，按 `(local_device_id, remote_device_id)` 查找本地有效会话。
4. 对已有有效会话的设备直接使用；对缺少会话的设备领取公钥包。
5. 验证签名、密钥格式、协议版本，执行身份信任检查。验证失败就停止该发送流程，不能降级成明文。
6. 协议库为目标设备发起握手，生成共享秘密并初始化发送会话。接收设备可以离线。
7. 分设备加密同一逻辑消息；初始消息携带接收方完成握手所需的公开协议资料和密钥编号。
8. 将更新后的会话状态和准确的待发送信封一起可靠保存，再发送到服务器。网络重试复用相同密文和标识。

```python
# 业务伪代码：真实签名、握手头、棘轮和 nonce 由协议库处理
targets = active_devices(B) + other_active_devices(A, exclude=A1)
for target in targets:
    with local_session_lock(A1, target):
        session = load_session(A1, target)
        if session is None:
            bundle = claim_prekey_bundle(target)
            validate_bundle_and_peer_identity(bundle)
            session = protocol.initiate_session(local_private_store, bundle)
        next_state, packet = protocol.encrypt(session, plaintext, authenticated_context)
        atomic_save_state_and_outbox(target, next_state, packet)
submit_persisted_outbox()
```

同一设备会话的并发加密必须串行化或受协议库事务保护，否则两个发送线程可能从同一旧状态派生重复消息密钥。提交确认丢失时，不要重新调用 encrypt 然后冒充原请求重发。

### UC-E2EE-04：服务器保存并投递

1. 由认证连接确定 sender_id 和 sender_device_id，检查设备仍有效。
2. 验证会话成员、好友/屏蔽规则、限流、封装版本和密文大小；服务端不能检查明文内容。
3. 检查目标设备属于接收用户或发送者自己的其他设备，并检查设备列表版本。旧列表返回需更新的设备，不让已撤销设备继续获得新信封。
4. 识别已提交的幂等请求。相同请求返回原 message_id/seq，不能再递增 last_seq。相同标识带不同已提交内容应报冲突。
5. 锁定相应会话，分配 seq，在同一事务保存逻辑消息、目标信封和可靠投递记录。
6. 提交成功后返回“已发送”；并安排在线推送。提交结果未知时允许查询或幂等重试。
7. 设备离线或实时推送失败时保留信封，重连后按设备补拉。推送失败不撤销已提交消息。

批量发送建议先采用“本次目标集合整体接受或整体拒绝”，避免只写入半组信封。设备列表变化而整组请求尚未被接受时，可以在保留旧目标原信封的基础上，新增目标信封、移除失效目标，重新提交。同一逻辑消息已被接受后，不允许普通重试修改收件集合；新增设备的历史同步是另外的流程。

可靠投递可选用同事务 outbox，或把持久化待收信封作为可扫描队列。不能只依赖提交后的内存任务：进程可能在提交后、入队前退出。重复发送由幂等和接收去重处理。

### UC-E2EE-05：B1 接收并解密

1. B1 收到推送或主动拉取仅属于自身设备的信封。
2. 查询 envelope_id 是否已经成功处理；已处理则重发确认，不重复展示、不再次推进会话。
3. 对初始握手消息，根据协议头找到本地对应预密钥私钥，由协议库创建候选接收会话。
4. 对后续消息，加载匹配的已有会话。
5. 验证并解密。认证失败不展示任何候选明文，不提交失败的候选状态，不误报已读。
6. 成功后将本地消息、更新的会话状态、去重记录和同步进度协调持久化；具体事务接口遵循协议库。
7. 再发送接收确认，界面展示正文。

接收端一次性预密钥私钥在对应握手成功处理后按协议删除；服务器“已领取”不能触发客户端立即删私钥，因为消息可能尚未到达。重复握手包和后续初始化消息应按协议库处理，不能每次都重新消耗一把私钥。

### UC-E2EE-06：第二条消息、回复和重启

后续消息使用已有会话，但消息密钥随协议推进变化。B 从 B1 回复时使用与 A1 的对应双向会话；B 从 B2 回复则使用 B2 的设备会话，并向 A 的目标设备及自己的其他设备分别投递。

应用重启后恢复身份与会话状态，不因断开 WebSocket 重新生成身份。设备乱序收信、跳号和延迟消息由协议库处理；应限制跳过消息密钥的缓存资源，业务层不能自行跳过验证。

## 5. 与当前 C++ WebSocket 线程设计的衔接

根据 `docs/websocket-worker-message-flow.md` 中描述的当前线程边界，E2EE 接入点为：

```text
A1 客户端：协议加密 → 上传密文应用消息
    ↓
WebsocketWorker → WebSocketMessageHandler → ApplicationParse
    ↓
WebSocketDispatcher → ServiceThreadPool
    ↓
ChatService：权限、设备集合、幂等、事务
    ↓
Repository → PostgreSQL：message + envelopes + 投递记录
    ↓ 提交成功
发送方确认任务 / 接收方设备投递任务
    ↓
各目标连接所属 WebsocketWorker：编码 Frame、写缓冲区、send
    ↓
B1 客户端：协议验证解密 → 本地持久化 → 接收确认
```

这是对现有线程模型的接入设计，不宣称 ChatService 已完成这些能力。网络线程不做数据库阻塞操作；业务线程不直接操作其他 Worker 的 session map、Connection 或 epoll。路由要从“只按 user_id 找连接”扩展为“按授权 device_id 找连接/会话”，并处理连接关闭及 fd 复用。写 socket 成功仅说明网络写入进展，不等于 B 保存或解密成功。

## 6. 数据库设计：沿用表、新增表和迁移边界

### 6.1 现有 message 表如何处理

前文 `app.message.payload_ciphertext BYTEA NOT NULL` 是“一条逻辑消息一个密文”的模型。采用本文逐设备会话时，不能让 B1 和 B2 共用该字段里的一个设备密文。

建议目标模型：`message` 保留逻辑消息元数据；设备协议包放到 `message_envelopes`。逻辑消息增加 sender_device_id。`payload_ciphertext` 的移除/可空化、旧数据迁移和读写版本切换须另做迁移方案，本次不执行 ALTER，也不以空字节绕过原非空约束。

若先保持单设备模型，也可暂把完整协议包保存到现有 payload_ciphertext，但仍须能明确识别目标设备。进入多设备前再完成拆表；这两个阶段不能让两份密文同时成为不清楚谁权威的数据源。

当前 `message_type` 和 `attachment_id` 明文存储会泄露类型和附件关系。第一版可保留它们用于权限和资源管理，并在加密内容/认证上下文中绑定必要字段；不能把该设计描述成元数据完全隐藏。

### 6.2 user_devices：设备目录

| 字段 | 建议类型 | 规则 |
|---|---|---|
| device_id | UUID | 主键 |
| user_id | UUID | 非空，FK → app.users(id) |
| device_name | TEXT | 可选的用户可读名称，不参与密码身份 |
| status | TEXT | pending / active / revoked，服务端管理 |
| identity_public_key | BYTEA | 设备身份公钥；也可归入密钥表，但只能有一个权威来源 |
| protocol_suite | TEXT | 明确算法组合与编码版本 |
| created_at / authorized_at / revoked_at | TIMESTAMPTZ | 记录设备生命周期 |

账号与设备目录需有单调版本，例如独立 `user_device_directory(user_id, version)`；授权/撤销与版本递增同事务。若首版采用单有效设备，可用 `user_id WHERE status='active'` 的部分唯一索引限制，而不是删除 device_id。登录凭据/连接必须绑定设备，服务器验证归属，不能只信客户端填写的 device_id。

### 6.3 device_key_bundles：可重复获取的公钥资料

| 字段 | 建议类型 | 规则 |
|---|---|---|
| device_id | UUID | FK → user_devices 属于哪一台设备|
| key_version | BIGINT（64位有符号整数） | 与 device_id 联合唯一 公玥资料版本号 | 
| signed_prekey_id | BIGINT | 设备范围内唯一，标识客户端私钥，带签名预密钥的编号，方便客户端找到对应私钥，比如43 |
| signed_prekey_public | BYTEA | 只保存公钥 编号为 43 的预密钥的公钥内容|
| signed_prekey_signature | BYTEA | 保存协议规定的有效签名 设备用身份私钥对该预密钥生成的数字签名|
| published_at| TIMESTAMPTZ | 轮换生命周期；具体期限待定 |
| expires_at | TIMESTAMPTZ | 轮换生命周期；具体期限待定 |
| is_current | BOOLEAN | 每台设备至多一个当前版本，是不是当前版本 |
设备 B1，资料版本 2，预密钥编号 42，预密钥公钥 = abc...

身份公钥来自设备表，接口可 JOIN 后组装 bundle，不必物理重复存储。旧预密钥能否继续用于迟到消息，由接收端保留对应私钥与协议规则处理。PQXDH 等协议需要额外材料和版本化结构，不能假定此表已完整覆盖。

### 6.4 device_one_time_prekeys：一次性公钥库存

| 字段 | 建议类型 | 规则 |
|---|---|---|
| device_id | UUID | FK → user_devices |
| prekey_id | BIGINT | 与 device_id 联合主键 |
| public_key | BYTEA | 公钥，禁止私钥 |
| state | TEXT | available / claimed |
| created_at | TIMESTAMPTZ | 领取时间 |
 |claimed_at | TIMESTAMPTZ | 领取时间 |

领取必须在事务中选择并更新一条 available 记录，可以用行锁加 `FOR UPDATE SKIP LOCKED` 后 `UPDATE ... RETURNING`。两次并发领取不能分配同一个公钥。记录一旦已领取，不因超时随意重新放回库存。可删除已领取行或保留不可再分配的审计记录；公开库存耗尽时通知设备补充。

若采用经典 X3DH，缺少一次性预密钥有协议定义的分支；其他协议按库行为处理，不能悄悄删掉安全步骤。领取请求要鉴权、限流，防止任意耗尽别人库存。

### 6.5 message_envelopes：设备密文信封

| 字段 | 建议类型 | 规则 |
|---|---|---|
| envelope_id | UUID | 主键，网络重试保持不变 |
| message_id | UUID | 非空，FK → app.message(id) |
| sender_device_id | UUID | 发送设备，从认证上下文确定 |
| recipient_device_id | UUID | 目标设备，FK → user_devices |
| protocol_version | TEXT | 解析封装需要的版本 |
| packet | BYTEA | 协议库生成的完整包，含必要头、密文与认证资料 |
| created_at | TIMESTAMPTZ | 服务端接受时间 |
| received_at / decrypted_at | TIMESTAMPTZ | 可选，客户端报告，不是服务器自行判定 |

约束建议：`UNIQUE(message_id, recipient_device_id)`、非空 packet、按 `(recipient_device_id, created_at, envelope_id)` 建同步查询索引。这里假设一条逻辑消息对同一设备只有一个正式信封；会话修复/替换需独立版本化流程，不能偷偷覆盖已提交密文。

初始协议头包含 A 的公开身份/临时材料及 B 的预密钥编号；之后普通消息包含相应棘轮头。nonce/tag 是否是独立字段取决于协议包格式，不要把教学 JSON 强加给库。

外键只证明设备存在，不能证明设备有效、与用户匹配或本次有权接收；这些在发送事务验证。不要级联删除设备记录导致另一方历史消息失去引用，优先软撤销。

### 6.6 客户端本地存储同样必需

| 本地集合 | 内容 |
|---|---|
| device_private_material | 身份私钥、预密钥私钥、生命周期信息 |
| peer_identity_store | 对方身份公钥、信任/核验状态与变化记录 |
| crypto_sessions | 每个对端设备的协议状态；按库格式持久化 |
| local_outbox | 确切已生成信封、稳定请求标识、提交状态 |
| local_messages / inbox_dedup | 本地明文历史或受保护存储、成功处理标记 |
| sync_state | 设备收信游标和业务会话进度 |

系统 Keychain/Keystore 等能力保护本地密钥。设备收到但尚未解密的密文可进入待处理队列，不能因推进了网络拉取游标就永久跳过解密重试。message.seq 是聊天排序；设备同步游标是投递进度，不能无条件混为一个值。

## 7. 加密算法的函数表示

下述公式只解释经典 X3DH 的 DH/KDF 核心。身份验证和签名验证必须先完成；完整生产会话由成熟协议库实现。[X3DH 规范](https://signal.org/docs/specifications/x3dh/)

```text
IK：身份密钥；EK：A 本次临时密钥；SPK：B 带签名预密钥；OPK：B 一次性预密钥
priv：私钥；pub：公钥；||：拼接字节

A 本地：
D1 = X25519(A_IK_priv, B_SPK_pub)
D2 = X25519(A_EK_priv, B_IK_pub)
D3 = X25519(A_EK_priv, B_SPK_pub)
D4 = X25519(A_EK_priv, B_OPK_pub)

B 本地：
D1 = X25519(B_SPK_priv, A_IK_pub)
D2 = X25519(B_IK_priv, A_EK_pub)
D3 = X25519(B_SPK_priv, A_EK_pub)
D4 = X25519(B_OPK_priv, A_EK_pub)

双方：S = X3DH_KDF(D1 || D2 || D3 || D4)
```

双方每组 D 相等，因此 S 相等。没有 OPK 时省略 D4，是否允许此分支由产品所选协议确定。签名不是第四个公钥；服务器没有任何一方私钥，不能仅由公开资料算出 S。

S 用来初始化会话，后续 `RatchetEncrypt(state, plaintext, context)` 返回新状态和消息包；`RatchetDecrypt(state, packet, context)` 返回新状态和验证后的明文。Double Ratchet 的密钥派生与会话初始化包含角色和协议头要求，不是把 S 永久拿来加密所有消息。[Double Ratchet 规范](https://signal.org/docs/specifications/doubleratchet/)

## 8. 实际计算的随机字符示例

### 8.1 示例边界

下面各数字由本次实际执行 Python `cryptography` 库得到，不是随意编造的“输入输出”。所有私钥都是专为教学随机生成并公开的废弃示例材料，绝不可用于真实通信。

本例分两段：先计算 X3DH 的四次 X25519 与 KDF 核心；再从 S 派生一个一次性教学消息密钥，以 ChaCha20-Poly1305 加解密“你好”。**未实现 XEdDSA 签名、联系人身份核验、完整握手包或 Double Ratchet，所以不是完整、安全可部署的聊天协议。**不能把这段脚本接进生产发送接口。

十六进制中每两个字符表示一个字节。X25519 原始密钥为 32 字节，显示为 64 个 hex 字符；真实协议编码可能额外带类型前缀。

### 8.2 五组真实示例密钥

```json
{
  "A_IK": {
    "private_hex": "60860237e15f8f85adc80ec7723a6f0bf2601ac49234a1150c1154236fdab57e",
    "public_hex": "4e363d104b612837ce05a3ffd96cf3e6e836081a3f2cc49a73fd56861edb3659"
  },
  "A_EK": {
    "private_hex": "d09ea7e07d773d633232c017815e5d1fdfd5290a4fb81fb16db95a89505c6862",
    "public_hex": "1749c55b0b8974c7c071489a4e34401de1c4889d5fc19a1c6f7968b467133c01"
  },
  "B_IK": {
    "private_hex": "a8f7c99c020a1a16e7237755fa6f73d711c4c22a864359cc4eb369e86c56b67d",
    "public_hex": "e6c4f143099694eb55aa9a659600beb89fa7884d6b6381e5ad05df936a37a774"
  },
  "B_SPK": {
    "private_hex": "901893ba90df1299fbc4a80368837f57b11b7b70a281aa80a215ef83319aca4b",
    "public_hex": "3fa0c20da93fc57ab3eab2cc5adce362e4ce10641c7b0bf92266b36c51ecd927"
  },
  "B_OPK": {
    "private_hex": "50f8142c6b9539453a518f70cad9630eb05bd842f27001143439f22156071b5c",
    "public_hex": "834aabec48f94e7621d0bdcb2bf931b5f0e36b7289d48efcb9cfbc66ebc8b00d"
  }
}
```

B 公钥包将引用上面的 B_IK.public_hex、B_SPK.public_hex 和 B_OPK.public_hex，分别配合编号 42、1007。签名必须由真实协议签名函数生成，本例未生成，所以不提供伪造签名“凑齐”公钥包。

A 只拿 B 的公钥，自己的私钥留在本地；B 使用自己的私钥以及初始消息中 A 的身份公钥和临时公钥。

### 8.3 四次 DH 与共享秘密 S

```json
{
  "DH_A_hex": [
    "09cd8f35ea0e3e415042aaac9a389d237359efb5b6cc270ab6e2f1ffdd074d65",
    "f5952e69491e6580d327eb8665df9296280890551b34dec0690a64db0fe6cd02",
    "82aba2c4fa230f3f07d9a23126b564574cdb673cd60de22e979f19963690d71c",
    "0d1feeedbd461ebc4458d74adcbe78ac17715aace63a574297f90fff2c4fd858"
  ],
  "DH_B_hex": [
    "09cd8f35ea0e3e415042aaac9a389d237359efb5b6cc270ab6e2f1ffdd074d65",
    "f5952e69491e6580d327eb8665df9296280890551b34dec0690a64db0fe6cd02",
    "82aba2c4fa230f3f07d9a23126b564574cdb673cd60de22e979f19963690d71c",
    "0d1feeedbd461ebc4458d74adcbe78ac17715aace63a574297f90fff2c4fd858"
  ],
  "S_A_hex": "861134e2187bc12887e6d47325a8666f86282ce8984428afc2669e7458c82a56",
  "S_B_hex": "861134e2187bc12887e6d47325a8666f86282ce8984428afc2669e7458c82a56"
}
```

教学例的 X3DH KDF 参数明确为：SHA-256，输入 `0xff` 重复 32 字节再拼接 D1～D4，salt 为 32 个零字节，info 为 ASCII `Example-X3DH-Core-v1`，输出 32 字节。这是为了让上面的结果能复核；应用参数不是所有聊天软件共用的常量。

### 8.4 派生教学消息密钥并加密

```text
K = HKDF-SHA256(S, salt, info, output_length=32)
(ciphertext, tag) = ChaCha20Poly1305.Encrypt(K, nonce, UTF8("你好"), AAD)
```

这里 HKDF 的 salt 和 info 是下方列出的教学参数，不是上一段握手 KDF 的参数。[HKDF 标准](https://www.rfc-editor.org/rfc/rfc5869)

```json
{
  "message_KDF_salt_hex": "5c52a554ee34cf92798f2e3b8ddeb27e064e6fd1e747a4b49a41234e93239842",
  "message_KDF_info_utf8": "Example-Only/A1-to-B1/M100",
  "message_key_hex": "c03970d03c61f750be11efdb22bd4537ac6c99dd1d395821dbe357a5d0e5e371",
  "nonce_hex": "34df7fa4974ed897480904f7",
  "AAD_utf8": "v=1|from=A1|to=B1|conversation=C_AB|message=M100",
  "plaintext_utf8": "���",
  "plaintext_hex": "e4bda0e5a5bd",
  "ciphertext_hex": "d1431664f8bd",
  "tag_hex": "58877d534cb45c6e1e78186184b8e8f1"
}
```

nonce 为 12 字节、tag 为 16 字节；本例一次新密钥只加密一次。正式系统同一密钥下 nonce 不能重复，交由协议库遵守其规则。AAD 是不加密但被认证的上下文。服务器分配的 seq 在加密时还不存在，不能事后改写本次 AAD；选择双方预先知道的稳定字段，用规范编码绑定。[ChaCha20-Poly1305 标准](https://www.rfc-editor.org/rfc/rfc8439)

### 8.5 服务器看到的教学信封

```json
{
  "sender_device_id": "A1",
  "recipient_device_id": "B1",
  "client_message_id": "M100（此处为教学标签）",
  "nonce_hex": "34df7fa4974ed897480904f7",
  "ciphertext_hex": "d1431664f8bd",
  "tag_hex": "58877d534cb45c6e1e78186184b8e8f1",
  "AAD_utf8": "v=1|from=A1|to=B1|conversation=C_AB|message=M100"
}
```

这段展示 AEAD 传输字段；若作为首条握手消息，还必须附上 A 的公开握手材料、B 预密钥编号等协议头。它本身不是一个完整 Signal 数据包。密钥 K、S、D1～D4 以及私钥均不上传服务器。

### 8.6 B 解密，以及故意改坏密文

B 先计算相同 S，再用相同派生参数得到 K，然后执行：

```text
明文字节 = ChaCha20Poly1305.Decrypt(K, nonce, ciphertext || tag, AAD)
UTF8(明文字节) = "你好"
```

脚本实际还执行了翻转密文一位、换一把密钥、修改 AAD 三项负向检查；它们均必须触发认证失败，不能输出可信明文。

```json
{
  "all_four_DH_results_match": true,
  "shared_secrets_match": true,
  "message_keys_match": true,
  "plaintext_roundtrip": true,
  "tampered_ciphertext_rejected": true,
  "wrong_key_rejected": true,
  "altered_AAD_rejected": true
}
```

随机输入每次运行不同，因此重新运行脚本会得到另一组数字；双方秘密一致、正确解密及错误输入拒绝的关系保持不变。该验证只证明这个教学计算链闭合，不证明项目已经实现完整 E2EE。

## 9. 状态、异常与恢复规则

| 场景 | 应有行为 |
|---|---|
| 签名验证失败、身份密钥意外变化 | 阻止或进入明确的信任确认流程；不静默忽略 |
| 公钥领取响应丢失 | 已领取项不能重新分配给别人；领取重试策略由接口定义，设备后续补充库存 |
| A 加密后应用退出 | 从本地 outbox 发送原信封，不重复推进棘轮 |
| 服务器事务提交前失败 | 无完整消息，不返回已发送 |
| 提交成功但确认丢失 | A 用原请求重试，返回原 message_id/seq |
| 提交成功后推送进程退出 | 由持久化队列/补拉恢复 |
| B 收到后尚未保存就退出 | 允许重新投递，未完成处理不提前确认 |
| B 保存成功但 ACK 丢失 | 按 envelope_id 去重并补发 ACK |
| B 解密失败 | 保留必要故障状态，报告无法解密；不能让服务器请求上传私钥“修复” |
| 同时推送与补拉同一信封 | 仅一次成功处理和一次界面展示 |
| 已撤销设备 | 不能发送、领取新资料或拉取新信封；新消息排除它；无法撤回其已经获知的明文 |

建议区分 accepted（服务端提交成功）、received（设备已可靠保存密文）、decrypted（客户端报告成功解密）、read（客户端报告用户阅读）。界面“已送达”映射 received 还是 decrypted 要明确确定；它们都不等同于已读。服务器不能独立证明客户端实际解密，只能接收授权设备的报告。

可用独立加密控制消息同步已读等敏感事件。ACK 只能更新自身设备的信封状态，不能让 B1 确认 B2；多设备是否按“任一设备到达”汇总为已送达，是产品规则。

## 10. 多设备、换机和历史记录

多设备扩展采用每个发送设备到每个目标设备分别维护会话的模型，可参考 [Sesame](https://signal.org/docs/specifications/sesame/)。不是所有设备共享同一把账户私钥。

新设备 B3 获得授权并发布资料后，A1 更新列表，为 B3 建立会话，新消息增加 B3 信封。历史消息只有 B1/B2 信封，B3 不能直接解密。服务端长期保留密文不等于新设备可恢复历史。

历史恢复另外选择：旧设备经验证的加密通道传输本地历史，或者用用户掌握恢复密钥的端到端加密备份。服务器不持有备份明文密钥。仅复制身份私钥也无法恢复已删除的历史消息密钥；所有可恢复副本均丢失时，旧消息无法恢复。

同一旧设备已把明文可靠保存到本地后，可以离线查看本地历史；“删除旧消息密钥”不等于“自动删除聊天历史”。不要通过回滚旧会话快照实现历史恢复，回滚可能导致密钥/nonce 重用。

## 11. 图片和文件的后续接入

沿用前文 attachment 表时，上传的对象必须已经在客户端加密。客户端生成独立随机文件密钥，用成熟的认证文件/分块加密方案加密对象；将文件密钥、认证参数、文件名等放进聊天消息的加密正文，再分别发送给各授权设备。

服务器可保存一个加密文件对象，各设备从自己的消息信封获得文件密钥。文件密钥不能明文放在 attachment 表、下载 URL 或公开 JSON 元数据中。大文件的分块顺序、nonce 唯一性与完整性必须由完整方案处理，不能对所有块复用同一个 nonce。附件保留期与消息保留期仍独立。

## 12. 分阶段实施及验收

第一阶段建议：单有效设备、一对一文本、离线补拉、设备身份与会话持久化。第二阶段：多设备授权和逐设备信封。第三阶段：历史迁移、加密备份、附件完善。加密协议库、客户端平台及授权方式需在实施前选定；本节不承诺某库已集成。

| 验收 | 预期 |
|---|---|
| A 首次发“你好”，B 在线 | B 验证解密成功，服务端无明文和私钥 |
| B 离线，A 发送 | 服务器接受；B 上线可完成首次握手和解密 |
| 双方重启后继续聊天 | 恢复会话，正常收发 |
| 提交确认丢失、A 重试 | 只有一条逻辑消息，每个目标一个信封 |
| 实时推送与补拉重复 | B 只展示一次 |
| 篡改密文、上下文或使用错误密钥 | 解密拒绝，不显示伪造内容 |
| 两请求并发领取预密钥 | 不领取同一条可用记录 |
| 同会话连续并发发送 | 无状态回滚、密钥重复或丢消息 |
| 多设备 B1 在线、B2 离线 | B1 即时接收，B2 稍后解密自己的信封 |
| 增加 B3 | 新消息可收；无迁移时旧信封不能直接解密 |
| 撤销 B2 | 后续目标集合排除 B2，旧连接不再拥有设备权限 |

这次文档配套的数值验证只覆盖第 8 节算法示例；以上业务验收是待实现的测试清单，不是已通过的项目测试。

## 13. 术语速查

| 术语 | 在本设计中的含义 |
|---|---|
| E2EE | 端到端加密：消息在端上加解密 |
| X25519 | 每次用本地私钥和对端公钥计算 DH 秘密 |
| X3DH / PQXDH | 异步首次握手协议；后者包含后量子机制 |
| HKDF | 从秘密材料派生具有明确用途的密钥 |
| XEdDSA | 经典 X3DH 规定的签名机制 |
| ChaCha20-Poly1305 / AES-GCM | 认证加密算法示例，具体采用哪种由协议确定 |
| Double Ratchet | 维护持续消息密钥演进的协议 |
| 公钥包 | 为对方发起握手提供的公开资料，不含 S |
| 消息信封 | 发给特定设备的协议包及业务路由信息 |
| 存储转发 | 服务器保存密文，接收设备可用时投递 |

