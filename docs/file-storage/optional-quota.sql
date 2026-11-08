-- OPTIONAL LATER DESIGN ONLY. Not executed; not required for the first release.
-- See schema.md: enabling quota requires coordinated service/accounting changes.
-- Existing uploads with reserved_bytes=0 must finish or be reconciled before enabling.
-- Stop new admissions, reconcile disk/DB usage and pending tasks, initialize quotas,
-- then enable the same policy on all writers. Creating this table alone is NOT enough.
BEGIN;

-- Attachment quota only. Avatar limits are ordinary HTTP/image-processing limits.
CREATE TABLE app.file_storage_quotas (
    user_id UUID PRIMARY KEY REFERENCES app.users(id) ON DELETE RESTRICT,
    quota_bytes BIGINT NOT NULL,
    used_bytes BIGINT NOT NULL DEFAULT 0,
    reserved_bytes BIGINT NOT NULL DEFAULT 0,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT file_quota_nonnegative CHECK (
        quota_bytes >= 0 AND used_bytes >= 0 AND reserved_bytes >= 0
    ),
    CONSTRAINT file_quota_within_limit CHECK (
        used_bytes::numeric + reserved_bytes::numeric <= quota_bytes::numeric
    )
);

COMMIT;
