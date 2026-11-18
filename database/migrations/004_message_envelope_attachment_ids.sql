BEGIN;

ALTER TABLE app.message_envelopes
    ADD COLUMN IF NOT EXISTS attachment_ids UUID[] NOT NULL DEFAULT '{}'::UUID[];

-- Preserve existing links on installations that applied the earlier draft.
DO $$
BEGIN
    IF to_regclass('app.envelope_attachments') IS NOT NULL THEN
        UPDATE app.message_envelopes AS env
        SET attachment_ids = links.ids
        FROM (
            SELECT envelope_id, array_agg(attachment_id ORDER BY attachment_id) AS ids
            FROM app.envelope_attachments
            GROUP BY envelope_id
        ) AS links
        WHERE env.envelope_id = links.envelope_id AND cardinality(env.attachment_ids) = 0;
    END IF;
END
$$;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'app.message_envelopes'::regclass
          AND conname = 'message_envelopes_attachment_ids_valid'
    ) THEN
        ALTER TABLE app.message_envelopes
            ADD CONSTRAINT message_envelopes_attachment_ids_valid CHECK (
                CASE
                    WHEN cardinality(attachment_ids) = 0 THEN TRUE
                    WHEN array_ndims(attachment_ids) = 1 AND array_lower(attachment_ids, 1) = 1
                    THEN cardinality(attachment_ids) <= 16
                         AND array_position(attachment_ids, NULL::UUID) IS NULL
                         AND array_position(attachment_ids, '00000000-0000-0000-0000-000000000000'::UUID) IS NULL
                    ELSE FALSE
                END
            );
    END IF;
END
$$;

CREATE INDEX IF NOT EXISTS message_envelopes_attachment_ids_idx
    ON app.message_envelopes USING GIN (attachment_ids)
    WHERE cardinality(attachment_ids) > 0;

-- Table-level privileges already cover the new column. This also supports
-- deployments where backend_manager was granted privileges by column.
GRANT SELECT (attachment_ids), INSERT (attachment_ids) ON app.message_envelopes TO backend_manager;

COMMIT;
