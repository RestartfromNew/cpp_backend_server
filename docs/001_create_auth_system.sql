BEGIN;

SET LOCAL ROLE :"owner_role";

REVOKE CREATE
    ON SCHEMA public
    FROM PUBLIC;

CREATE SCHEMA IF NOT EXISTS app
    AUTHORIZATION :"owner_role";

ALTER SCHEMA app
OWNER TO :"owner_role";

REVOKE ALL
    ON SCHEMA app
    FROM PUBLIC;

GRANT USAGE
ON SCHEMA app
TO :"app_role";


-- ============================================================
-- users
-- ============================================================

CREATE TABLE app.users (
                           id UUID
                               PRIMARY KEY
                               DEFAULT gen_random_uuid(),

                           display_name TEXT
                               NOT NULL,

                           is_active BOOLEAN
                               NOT NULL
                               DEFAULT TRUE,

                           created_at TIMESTAMPTZ
                               NOT NULL
                               DEFAULT CURRENT_TIMESTAMP,

                           updated_at TIMESTAMPTZ
                               NOT NULL
                               DEFAULT CURRENT_TIMESTAMP,

                           CONSTRAINT users_display_name_not_blank
                               CHECK (
                                   length(trim(display_name)) > 0
                                   )
);


-- ============================================================
-- password_credentials
-- ============================================================

CREATE TABLE app.password_credentials (
                                          user_id UUID
                                              PRIMARY KEY
                                              REFERENCES app.users(id)
                                                  ON DELETE CASCADE,

                                          login_email TEXT
                                              NOT NULL,

                                          password_hash TEXT
                                              NOT NULL,

                                          email_verified_at TIMESTAMPTZ,

                                          password_changed_at TIMESTAMPTZ
                                              NOT NULL
                                              DEFAULT CURRENT_TIMESTAMP,

                                          created_at TIMESTAMPTZ
                                              NOT NULL
                                              DEFAULT CURRENT_TIMESTAMP,

                                          CONSTRAINT password_credentials_email_not_blank
                                              CHECK (
                                                  length(trim(login_email)) > 0
                                                  ),

                                          CONSTRAINT password_credentials_hash_not_blank
                                              CHECK (
                                                  length(trim(password_hash)) > 0
                                                  )
);

CREATE UNIQUE INDEX
    password_credentials_login_email_unique
    ON app.password_credentials (
                                 lower(login_email)
        );


-- ============================================================
-- external_identities
-- ============================================================

CREATE TABLE app.external_identities (
                                         id UUID
                                             PRIMARY KEY
                                             DEFAULT gen_random_uuid(),

                                         user_id UUID
                                             NOT NULL
                                             REFERENCES app.users(id)
                                                 ON DELETE CASCADE,

                                         issuer TEXT
                                             NOT NULL,

                                         subject TEXT
                                             NOT NULL,

                                         provider_display_name TEXT,

                                         provider_email TEXT,

                                         provider_email_verified BOOLEAN,

                                         created_at TIMESTAMPTZ
                                             NOT NULL
                                             DEFAULT CURRENT_TIMESTAMP,

                                         updated_at TIMESTAMPTZ
                                             NOT NULL
                                             DEFAULT CURRENT_TIMESTAMP,

                                         CONSTRAINT external_identities_issuer_not_blank
                                             CHECK (
                                                 length(trim(issuer)) > 0
                                                 ),

                                         CONSTRAINT external_identities_subject_not_blank
                                             CHECK (
                                                 length(trim(subject)) > 0
                                                 ),

                                         CONSTRAINT external_identities_issuer_subject_unique
                                             UNIQUE (
                                                     issuer,
                                                     subject
                                                 ),

                                         CONSTRAINT external_identities_user_issuer_unique
                                             UNIQUE (
                                                     user_id,
                                                     issuer
                                                 )
);


-- ============================================================
-- refresh_tokens
-- ============================================================

CREATE TABLE app.refresh_tokens (
                                    id UUID
                                        PRIMARY KEY
                                        DEFAULT gen_random_uuid(),

                                    user_id UUID
                                        NOT NULL
                                        REFERENCES app.users(id)
                                            ON DELETE CASCADE,

                                    token_hash TEXT
                                        NOT NULL,

                                    expires_at TIMESTAMPTZ
                                        NOT NULL,

                                    revoked_at TIMESTAMPTZ,

                                    created_at TIMESTAMPTZ
                                        NOT NULL
                                        DEFAULT CURRENT_TIMESTAMP,

                                    CONSTRAINT refresh_tokens_token_hash_unique
                                        UNIQUE (
                                                token_hash
                                            ),

                                    CONSTRAINT refresh_tokens_hash_not_blank
                                        CHECK (
                                            length(trim(token_hash)) > 0
                                            ),

                                    CONSTRAINT refresh_tokens_expiration_valid
                                        CHECK (
                                            expires_at > created_at
                                            )
);

CREATE INDEX refresh_tokens_user_id_index
    ON app.refresh_tokens(user_id);

CREATE INDEX refresh_tokens_expires_at_index
    ON app.refresh_tokens(expires_at);


-- ============================================================
-- 当前表权限
-- ============================================================

GRANT SELECT, INSERT, UPDATE, DELETE
      ON ALL TABLES IN SCHEMA app
          TO :"app_role";


-- ============================================================
-- 未来新表的默认权限
-- ============================================================

ALTER DEFAULT PRIVILEGES
FOR ROLE :"owner_role"
IN SCHEMA app
GRANT SELECT, INSERT, UPDATE, DELETE
      ON TABLES
          TO :"app_role";

ALTER DEFAULT PRIVILEGES
FOR ROLE :"owner_role"
IN SCHEMA app
GRANT USAGE, SELECT
             ON SEQUENCES
                 TO :"app_role";

COMMIT;