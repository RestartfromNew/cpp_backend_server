-- Run as the table owner in the intended database. This file is NOT auto-executed.
-- New meaning: message_id = client retry UUID, not app.message(id).
BEGIN;
LOCK TABLE app.message_envelopes IN ACCESS EXCLUSIVE MODE;

-- Remove only the old message foreign key and old two-column retry constraint.
DO $$
DECLARE item RECORD;
BEGIN
    FOR item IN
        SELECT c.conname
        FROM pg_constraint AS c
        WHERE c.conrelid = 'app.message_envelopes'::regclass
          AND (
            (c.contype = 'f' AND c.confrelid = 'app.message'::regclass
             AND c.conkey = ARRAY[(SELECT attnum FROM pg_attribute
                 WHERE attrelid = c.conrelid AND attname = 'message_id')])
            OR
            (c.contype = 'u' AND (
                SELECT array_agg(a.attname::text ORDER BY a.attname)
                FROM unnest(c.conkey) AS keys(attnum)
                JOIN pg_attribute AS a ON a.attrelid = c.conrelid AND a.attnum = keys.attnum
            ) = ARRAY['message_id','recipient_device_id']::text[])
          )
    LOOP
        EXECUTE format('ALTER TABLE app.message_envelopes DROP CONSTRAINT %I', item.conname);
    END LOOP;
END $$;

ALTER TABLE app.message_envelopes
    ADD COLUMN IF NOT EXISTS seq BIGINT;

-- Only assign historical order if all existing seq values are NULL.
-- Preserve established seq values; mixed data must be reconciled deliberately.
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM app.message_envelopes WHERE seq IS NULL)
       AND EXISTS (SELECT 1 FROM app.message_envelopes WHERE seq IS NOT NULL) THEN
        RAISE EXCEPTION 'Mixed NULL and assigned seq: reconcile historical records before migration';
    END IF;
END $$;

WITH numbered AS (
    SELECT envelope_id,
           ROW_NUMBER() OVER (
               PARTITION BY LEAST(sender_device_id, recipient_device_id),
                            GREATEST(sender_device_id, recipient_device_id)
               ORDER BY created_at, envelope_id
           ) AS assigned_seq
    FROM app.message_envelopes
)
UPDATE app.message_envelopes AS env
SET seq = numbered.assigned_seq
FROM numbered
WHERE env.envelope_id = numbered.envelope_id AND env.seq IS NULL;

ALTER TABLE app.message_envelopes
    ALTER COLUMN seq SET NOT NULL,
    ALTER COLUMN envelope_id SET DEFAULT gen_random_uuid();

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint
        WHERE conrelid = 'app.message_envelopes'::regclass AND conname = 'envelope_seq_positive') THEN
        ALTER TABLE app.message_envelopes ADD CONSTRAINT envelope_seq_positive CHECK (seq > 0);
    END IF;
END $$;

CREATE UNIQUE INDEX IF NOT EXISTS envelope_pair_seq_unique
ON app.message_envelopes (
    LEAST(sender_device_id, recipient_device_id),
    GREATEST(sender_device_id, recipient_device_id), seq
);

CREATE UNIQUE INDEX IF NOT EXISTS envelope_retry_unique
ON app.message_envelopes (sender_device_id, recipient_device_id, message_id);

COMMENT ON COLUMN app.message_envelopes.message_id IS
    'Client-generated retry UUID, scoped by sender and recipient device; not app.message FK';
COMMENT ON COLUMN app.message_envelopes.seq IS
    'Ordered within a canonical unordered device pair; allocated under transaction advisory lock';
COMMIT;

-- Check actual constraint/index definitions (IF NOT EXISTS only checks names):
SELECT conname, pg_get_constraintdef(oid)
FROM pg_constraint WHERE conrelid = 'app.message_envelopes'::regclass;
SELECT indexname, indexdef FROM pg_indexes
WHERE schemaname = 'app' AND tablename = 'message_envelopes';
