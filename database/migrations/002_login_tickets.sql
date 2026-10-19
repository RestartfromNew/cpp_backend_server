-- Run as the schema owner, before deploying the new binary. No data is deleted.
BEGIN;
CREATE TABLE IF NOT EXISTS app.login_tickets (
    ticket_hash TEXT PRIMARY KEY CHECK (ticket_hash ~ '^[0-9a-f]{64}$'),
    user_id UUID NOT NULL REFERENCES app.users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    expires_at TIMESTAMPTZ NOT NULL,
    consumed_at TIMESTAMPTZ,
    CHECK (expires_at > created_at)
);
CREATE INDEX IF NOT EXISTS login_tickets_expiry_idx ON app.login_tickets(expires_at);
ALTER TABLE app.refresh_tokens ADD COLUMN IF NOT EXISTS device_id UUID;
-- Existing unbound tokens cannot be securely assigned to a device automatically.
DO $$ BEGIN
    IF EXISTS (SELECT 1 FROM app.refresh_tokens WHERE device_id IS NULL) THEN
        RAISE EXCEPTION 'Unbound refresh tokens exist: explicitly invalidate/remove them before this migration';
    END IF;
END $$;
ALTER TABLE app.refresh_tokens ALTER COLUMN device_id SET NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS user_devices_device_user_unique
    ON app.user_devices(device_id, user_id);
DO $$ BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint
        WHERE conrelid = 'app.refresh_tokens'::regclass
          AND conname = 'refresh_tokens_device_user_fkey') THEN
        ALTER TABLE app.refresh_tokens ADD CONSTRAINT refresh_tokens_device_user_fkey
            FOREIGN KEY (device_id, user_id) REFERENCES app.user_devices(device_id, user_id)
            ON DELETE RESTRICT;
    END IF;
END $$;
CREATE INDEX IF NOT EXISTS refresh_tokens_device_id_idx ON app.refresh_tokens(device_id);
CREATE OR REPLACE FUNCTION app.validate_refresh_token_device()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    PERFORM 1 FROM app.user_devices
    WHERE device_id = NEW.device_id AND user_id = NEW.user_id AND status = 'active'
    FOR SHARE;
    IF NOT FOUND THEN
        RAISE EXCEPTION 'Refresh token requires an active device owned by the user'
            USING ERRCODE = '23514';
    END IF;
    RETURN NEW;
END $$;
DROP TRIGGER IF EXISTS refresh_tokens_validate_device ON app.refresh_tokens;
CREATE TRIGGER refresh_tokens_validate_device
BEFORE INSERT OR UPDATE OF device_id, user_id ON app.refresh_tokens
FOR EACH ROW EXECUTE FUNCTION app.validate_refresh_token_device();
COMMIT;
-- Grant the runtime role SELECT, INSERT, UPDATE on app.login_tickets separately.
