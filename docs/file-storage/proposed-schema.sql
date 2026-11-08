-- DESIGN DRAFT ONLY: not an executable upgrade or application migration.
-- Avatars and encrypted attachments have INDEPENDENT object tables.
-- Avatars use ordinary HTTP upload; file_uploads is ONLY for encrypted attachments.
-- Prerequisites: app.users(id), app.user_devices(device_id,user_id) UNIQUE
-- (database/migrations/002_login_tickets.sql), app.message_envelopes(envelope_id).
-- Existing attachment / mixed-object draft requires a reviewed ALTER/backfill plan.
-- No hidden DROP, data migration, triggers or runtime GRANT.
BEGIN;

-- Optional cumulative attachment quota: see optional-quota.sql.
-- Core uploads use reserved_bytes=0 until server-side quota mode is enabled.
-- Final avatar metadata only. Insert together with current-avatar binding.
-- active/revoked tracks business availability, not an upload task lifecycle.
CREATE TABLE app.avatars (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    uploader_id UUID NOT NULL REFERENCES app.users(id) ON DELETE RESTRICT,
    storage_key TEXT NOT NULL UNIQUE,
    size_bytes BIGINT NOT NULL,
    content_type TEXT NOT NULL DEFAULT 'image/jpeg',
    status TEXT NOT NULL DEFAULT 'active',
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    revoked_at TIMESTAMPTZ,
    CONSTRAINT avatar_storage_key_valid CHECK (storage_key ~
        '^objects/avatars/[0-9a-f]{2}/[0-9a-f]{2}/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\.jpg$'),
    CONSTRAINT avatar_content_type_valid CHECK (content_type = 'image/jpeg'),
    CONSTRAINT avatar_size_positive CHECK (size_bytes > 0),
    CONSTRAINT avatar_status_valid CHECK (status IN ('active', 'revoked')),
    CONSTRAINT avatar_revocation_consistent CHECK (
        (status = 'active' AND revoked_at IS NULL) OR
        (status = 'revoked' AND revoked_at IS NOT NULL)
    ),
    CONSTRAINT avatar_revocation_time_valid CHECK (revoked_at IS NULL OR revoked_at >= created_at),
    CONSTRAINT avatar_id_owner_unique UNIQUE (id, uploader_id)
);
CREATE INDEX avatar_uploader_idx ON app.avatars(uploader_id, created_at);
CREATE INDEX avatar_revoked_cleanup_idx ON app.avatars(revoked_at)
    WHERE status = 'revoked';

-- Client-encrypted chat attachments only. Never store avatar rows here.
CREATE TABLE app.attachment (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    uploader_id UUID NOT NULL REFERENCES app.users(id) ON DELETE RESTRICT,
    uploader_device_id UUID NOT NULL,
    storage_key TEXT NOT NULL UNIQUE,
    status TEXT NOT NULL DEFAULT 'uploading',
    size_bytes BIGINT,
    sha256 BYTEA,
    content_type TEXT NOT NULL DEFAULT 'application/octet-stream',
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    ready_at TIMESTAMPTZ,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    unreferenced_expires_at TIMESTAMPTZ,
    expires_at TIMESTAMPTZ,
    deleted_at TIMESTAMPTZ,
    CONSTRAINT attachment_device_owner_fk FOREIGN KEY (uploader_device_id, uploader_id)
        REFERENCES app.user_devices(device_id, user_id) ON DELETE RESTRICT,
    CONSTRAINT attachment_status_valid CHECK (
        status IN ('uploading', 'verifying', 'ready', 'failed', 'deleting', 'deleted')
    ),
    CONSTRAINT attachment_storage_key_valid CHECK (storage_key ~
        '^objects/attachments/[0-9a-f]{2}/[0-9a-f]{2}/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\.bin$'),
    CONSTRAINT attachment_content_type_valid CHECK (content_type = 'application/octet-stream'),
    CONSTRAINT attachment_size_positive CHECK (size_bytes IS NULL OR size_bytes > 0),
    CONSTRAINT attachment_sha256_length CHECK (sha256 IS NULL OR octet_length(sha256) = 32),
    CONSTRAINT attachment_ready_metadata CHECK (
        status <> 'ready' OR (size_bytes IS NOT NULL AND sha256 IS NOT NULL AND ready_at IS NOT NULL)
    ),
    CONSTRAINT attachment_time_valid CHECK (
        (ready_at IS NULL OR ready_at >= created_at) AND
        (unreferenced_expires_at IS NULL OR unreferenced_expires_at >= created_at) AND
        (expires_at IS NULL OR expires_at >= created_at) AND
        (deleted_at IS NULL OR deleted_at >= created_at)
    ),
    CONSTRAINT attachment_deleted_state CHECK ((status = 'deleted') = (deleted_at IS NOT NULL)),
    CONSTRAINT attachment_id_device_owner_unique UNIQUE (id, uploader_device_id, uploader_id)
);
CREATE INDEX attachment_uploader_idx ON app.attachment(uploader_id, created_at);
CREATE INDEX attachment_expiry_idx ON app.attachment(expires_at)
    WHERE status = 'ready' AND expires_at IS NOT NULL;
CREATE INDEX attachment_unreferenced_expiry_idx ON app.attachment(unreferenced_expires_at)
    WHERE status = 'ready' AND unreferenced_expires_at IS NOT NULL;
CREATE INDEX attachment_cleanup_idx ON app.attachment(updated_at)
    WHERE status IN ('failed', 'deleting');

-- Encrypted attachment upload tasks ONLY; avatars never enter this table.
CREATE TABLE app.file_uploads (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    attachment_id UUID NOT NULL UNIQUE,
    uploader_id UUID NOT NULL,
    uploader_device_id UUID NOT NULL,
    client_request_id UUID NOT NULL,
    expected_size_bytes BIGINT NOT NULL,
    expected_sha256 BYTEA,
    state TEXT NOT NULL DEFAULT 'authorized',
    upload_token_hash BYTEA NOT NULL,
    credential_version BIGINT NOT NULL DEFAULT 1,
    reserved_bytes BIGINT NOT NULL DEFAULT 0,
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    finished_at TIMESTAMPTZ,
    failure_code TEXT,
    CONSTRAINT file_upload_attachment_owner_fk
        FOREIGN KEY (attachment_id, uploader_device_id, uploader_id)
        REFERENCES app.attachment(id, uploader_device_id, uploader_id) ON DELETE RESTRICT,
    CONSTRAINT file_upload_retry_unique UNIQUE (uploader_device_id, client_request_id),
    CONSTRAINT file_upload_state_valid CHECK (
        state IN ('authorized', 'receiving', 'verifying', 'completed', 'failed', 'cancelled', 'expired')
    ),
    CONSTRAINT file_upload_size_positive CHECK (expected_size_bytes > 0),
    CONSTRAINT file_upload_hash_length CHECK (
        expected_sha256 IS NULL OR octet_length(expected_sha256) = 32
    ),
    CONSTRAINT file_upload_token_hash_length CHECK (octet_length(upload_token_hash) = 32),
    CONSTRAINT file_upload_version_positive CHECK (credential_version > 0),
    CONSTRAINT file_upload_reservation_nonnegative CHECK (reserved_bytes >= 0),
    CONSTRAINT file_upload_active_reservation CHECK (
        reserved_bytes = 0 OR state NOT IN ('authorized', 'receiving', 'verifying')
        OR reserved_bytes >= expected_size_bytes
    ),
    CONSTRAINT file_upload_completed_reservation CHECK (state <> 'completed' OR reserved_bytes = 0),
    CONSTRAINT file_upload_expiry CHECK (expires_at > created_at),
    CONSTRAINT file_upload_finished CHECK (
        (state IN ('completed', 'failed', 'cancelled', 'expired')) = (finished_at IS NOT NULL)
    ),
    CONSTRAINT file_upload_finished_time CHECK (finished_at IS NULL OR finished_at >= created_at)
);
CREATE INDEX file_upload_expiry_idx ON app.file_uploads(expires_at)
    WHERE state IN ('authorized', 'receiving', 'verifying');
CREATE INDEX file_upload_unreleased_idx ON app.file_uploads(updated_at)
    WHERE state IN ('failed', 'cancelled', 'expired') AND reserved_bytes > 0;

-- Current avatar points ONLY to avatars and must belong to the same user.
CREATE TABLE app.user_avatars (
    user_id UUID PRIMARY KEY REFERENCES app.users(id) ON DELETE RESTRICT,
    avatar_id UUID NOT NULL UNIQUE,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT user_avatar_owned_object_fk FOREIGN KEY (avatar_id, user_id)
        REFERENCES app.avatars(id, uploader_id) ON DELETE RESTRICT
);

-- Chat envelopes point ONLY to encrypted attachments, never avatars.
CREATE TABLE app.envelope_attachments (
    envelope_id UUID NOT NULL REFERENCES app.message_envelopes(envelope_id) ON DELETE RESTRICT,
    attachment_id UUID NOT NULL REFERENCES app.attachment(id) ON DELETE RESTRICT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (envelope_id, attachment_id)
);
CREATE INDEX envelope_attachment_object_idx ON app.envelope_attachments(attachment_id, envelope_id);

-- file_uploads.client_request_id maps the original WebSocket permit request_id.
-- Transport does not alter schema: task state survives WebSocket disconnects.
-- 0 reservation denotes disabled quota mode, controlled by server configuration only.
-- Enabled mode MUST reserve/check quota transactionally and forbid zero-budget bypass.
-- Services additionally enforce active status, immutable task targets, permissions,
-- publication, expiry, idempotency and exact quota accounting in transactions.
-- CHECK/FK constraints do not validate disk contents, encryption or image safety.
COMMIT;
