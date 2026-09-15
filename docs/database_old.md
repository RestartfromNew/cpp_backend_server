# Database Design Document
Project: Multi-AI intergration system

Version: 1.0

Author: Beining Yang

Update Date: 2026-02

## 1.Overview
### 1.1 Purpose 
This document describes the database schema design for the Database System module.
It defines tables, relationships, constraints, and security rules.

### 1.2 Scope
This version includes:
+ User authentication
+ Log in authorization and token

## 2.Entity Relationship Overview
+ User
+ Role 
+ User_Role

## 3.Table definition
### 3.1 user

|  Field Name   |     Type     | Constraint | Description|
|:-------------:|:------------:|:----:|:----:|
|      id       |     UUID     |primary key;Not null|
| display_name  |     TEXT     |Not null|
|     email     | VARCHAR(255) |unique;Not null|
| password_hash |     TEXT     |Not null|
|   is_active   |   BOOLEAN    |Default True|
|  created_at   | TIMESTAMPTZ  | Default NOW()|
|  updated_at   | TIMESTAMPTZ  |  Default NOW()|

```SQL
CREATE TABLE users (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    dispaly_name VARCHAR(50) UNIQUE NOT NULL,
    email VARCHAR(255) UNIQUE NOT NULL,
    -- password_hash TEXT NOT NULL,
    password_hash TEXT,
    -- 重要更新，password_hash应该是可以为空的
    is_active BOOLEAN DEFAULT TRUE,
    is_verified BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);
```

### 3.2 refresh_tokens Table
| Field Name   |     Type    |                      Constraint                      | Description                                                      |
|:----:|:----:|:----:|:----:|
|     id     |     UUID    |   Primary Key; Not Null; Default gen_random_uuid()   | Unique identifier for each refresh token                         |
|   user_id  |     UUID    | Foreign Key → users(id); On Delete Cascade; Not Null | References the user who owns this token                          |
| token_hash |     TEXT    |                       Not Null                       | Hashed version of the refresh token (never store raw token)      |
| expires_at | TIMESTAMPTZ |                       Not Null                       | Expiration timestamp of the refresh token                        |
|   revoked  |   BOOLEAN   |                     Default FALSE                    | Indicates whether the token has been revoked (logout / rotation) |
| created_at | TIMESTAMPTZ |                     Default NOW()                    | Timestamp when the token was created                             |

```SQL
CREATE TABLE refresh_tokens (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    revoked BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
```
### 3.3 oauth 用于第三方授权登陆
```SQL
CREATE TABLE oauth (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    -- email VARCHAR(255),
    -- user_name VARCHAR(50),
    -- 这两个字段应该合并到user表中
    provider_user_id TEXT NOT NULL,
    provider VARCHAR(50) NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    UNIQUE(provider, provider_user_id)
);
```
 ## 4 AI vector
 voice_profile
 ```SQL
 CREATE TABLE voice_profiles (
    id UUID PRIMARY KEY,
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    voice_name VARCHAR(100),
    embedding VECTOR(256) NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
    
);
 ```
## 5 Characters
character profile table
```SQL
CREATE TABLE characters (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    name VARCHAR(100) NOT NULL,
    gender VARCHAR(10),
    relationship VARCHAR(50),
    personality TEXT,
    background_story TEXT,
    speak_style TEXT,
    do_rules JSONB,     
    dont_rules JSONB,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
```
character assets table(voice, picture)
```SQL
CREATE TABLE character_assets (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    character_id UUID REFERENCES characters(id) ON DELETE CASCADE,
    asset_type VARCHAR(20) CHECK (asset_type IN ('avatar', 'voice', 'image')),
    file_url TEXT,
    is_temp BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
```
## 6 chat history
chat session
```SQL
CREATE TABLE chat_sessions (
     id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    character_id UUID REFERENCES characters(id) ON DELETE CASCADE,
    title VARCHAR(255),
    last_message TEXT,
    updated_at TIMESTAMPTZ DEFAULT NOW()
);
```
chat message
```SQL
CREATE TABLE chat_messages (
    id BIGSERIAL PRIMARY KEY, --保持message的连贯性，uuid是随机插入的
    session_id UUID NOT NULL REFERENCES chat_sessions(id) ON DELETE CASCADE,
    role VARCHAR(10) NOT NULL,   -- user / character
    content TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```