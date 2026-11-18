BEGIN;

-- Preserve existing usernames and their uniqueness. The application lowercases new registrations.
ALTER TABLE app.users DROP CONSTRAINT IF EXISTS users_check;
ALTER TABLE app.users ADD CONSTRAINT users_check CHECK (username ~ '^[a-z0-9_.-]{3,30}$') NOT VALID;
ALTER TABLE app.users VALIDATE CONSTRAINT users_check;

-- Existing names can be reviewed separately; new writes use the 1-50 code-point limit.
ALTER TABLE app.users ADD CONSTRAINT users_display_name_length CHECK (
    char_length(display_name) BETWEEN 1 AND 50 AND length(trim(display_name)) > 0
) NOT VALID;

COMMIT;
