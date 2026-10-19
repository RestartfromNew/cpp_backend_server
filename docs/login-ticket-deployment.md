# Login ticket deployment

The change is intentionally incompatible with clients that read tokens directly from password login or call GET /device_verification. Deploy clients and server together.

1. Back up the target database and inspect existing refresh_tokens.device_id values. Migration does not delete or assign legacy credentials. Null device IDs cause a transaction rollback; explicitly remove/migrate legacy unbound rows under your migration policy first. Revoked-but-null rows still fail NOT NULL; never invent device ownership.
2. Apply as schema owner or an administrator with the required DDL privileges (replace the example connection and role):

```bash
cd /mnt/d/projects/cpp_backend_server
sudo -u postgres psql -d backend_dev -v ON_ERROR_STOP=1 < database/migrations/002_login_tickets.sql
```

3. Grant the backend runtime role access, in that same database. Replace YOUR_APP_ROLE with its actual role; do not grant PUBLIC access to credentials:

```sql
GRANT USAGE ON SCHEMA app TO "YOUR_APP_ROLE";
GRANT SELECT, INSERT, UPDATE ON app.login_tickets TO "YOUR_APP_ROLE";
```

The existing runtime role also needs its existing users/device/key/token privileges. If using a dedicated owner role, execute the migration under that owner so future migrations do not fail ownership checks. Apply separately to each environment. The migration is transactional and can be reapplied; it requires the previously created app.users, app.user_devices, app.device_key_bundles and app.refresh_tokens tables.

4. Build and test on Linux:

```bash
cmake --build build --target cpp_backend_server login_ticket_isolated_test
ctest --test-dir build -R '^login_ticket_isolated$' --output-on-failure
```

Use your actual configured build directory. Isolated tests simulate PostgreSQL/crypto dependencies; they do not establish actual cryptographic or PostgreSQL concurrency correctness. Server uses libsodium and PostgreSQL, never these test fakes.

5. Restart the service with the rebuilt binary and run the opt-in HTTP tests against a TEST database/device:

```bash
export LOGIN_TICKET_TEST_URL='http://127.0.0.1:8082'
export LOGIN_TICKET_EMAIL='alice_chat01@example.com'
read -rs -p 'Test password: ' LOGIN_TICKET_PASSWORD
echo
export LOGIN_TICKET_PASSWORD
export LOGIN_TICKET_DEVICE_ID='f7ab7d41-9955-4a73-922d-7b7a80ebdac4'
python3 tests/login_ticket_http_test.py
```

These tests create login tickets and refresh credentials for the test account. They do not delete records or register new devices. Do not use them as a production load test. Set LOGIN_TICKET_FOREIGN_DEVICE_ID to an existing device owned by another TEST account for an additional ownership case. Tests send the submitted user_id as a deliberately false value to verify identity is derived from the ticket.

6. Update clients: POST /login_by_email returns code/login_ticket/expires_in. Submit the ticket within five minutes to POST /device_verification with the existing device_id OR to POST /register_device with public key material. Save the final tokens and device_id only after completion succeeds. Never log tickets or tokens; use TLS outside local testing.

Ticket completion locks the ticket, validates the account/device, consumes the ticket, and inserts the bound refresh credential in the same transaction; registration also inserts the device/bundle there. Only one concurrent completion can succeed. Validation failure leaves the ticket unused, and failed inserts roll back. A lost successful response or commit outcome uncertainty requires a fresh password login; replay is deliberately rejected. Access-token signing happens after commit, so a signing failure also requires a fresh login. This version does not recover/reissue a previously committed token pair.

Refresh lookup now excludes inactive devices/accounts. Device revocation also marks that device's refresh credentials revoked. Access JWT subjects remain user IDs; this change does not introduce device claims or implement refresh rotation/logout endpoints.

The active-device trigger is validated on credential insertion/binding updates; refresh-time checks remain necessary after later device status changes. Existing refresh rows pointing to nonexistent or mismatched users/devices make the composite FK migration fail rather than silently grant access.

Cleanup is not scheduled automatically. A maintenance role can periodically run:

```sql
DELETE FROM app.login_tickets
WHERE expires_at < CURRENT_TIMESTAMP - INTERVAL '1 day';
```

Expired rows cannot authenticate before cleanup. Retain consumed rows according to your audit policy. One day is an example retention period.

Refresh insertion SQL lives in RefreshTokenRepository::insertRefreshToken. LoginTicketRepository passes its existing transaction connection; the helper never leases another connection or commits independently.

## Local500 diagnosis

Password login returns top-level code only. A response {"code":"internal_error"} is a Handler failure, not route_not_found, and device registration has not yet occurred. Confirm the runtime database has app.login_tickets and grant the application role SELECT/INSERT/UPDATE; a superuser GUI session is not proof of application-role access. The login Handler currently does not log SQLSTATE. See Section22 of backend-api.md/backend-api.en.md for safe read-only diagnostics and URI troubleshooting. psql is the executable, not a default PostgreSQL username; the local Ubuntu/WSL command above runs as postgres without a TCP host.
