# Device revocation: Repository / Service

`UserService::revokeUserDevice` verifies the password, then calls the repository.
Expected business outcomes are enum values, not exceptions. Database errors
propagate through the transaction boundary; no success is returned before commit.

## Results

| Condition | Service result |
| --- | --- |
| No password login record | UserNotFound |
| Incorrect password | InvalidPassword |
| Device absent | DeviceNotFound |
| Device belongs to another user | NotYourDevice |
| Device already revoked | RevokedDevice |
| Device pending or otherwise not active | DeviceNotActive |
| Active device revoked and committed | true |

Ownership is checked before revealing device status. Repeated revocation does
not change revoked_at. Historical envelopes and prekeys are preserved.

## Concurrency contract

Revocation locks the target user_devices row FOR UPDATE, checks ownership and
status, then updates status/revoked_at in the same transaction.

Prekey uploads now lock the active device and account FOR SHARE in the same
transaction as the inserts. Envelope insertion, synchronization, ACK and prekey
claim already check authorization under device FOR SHARE locks.

- If a writer acquires the shared lock first, it may commit before revocation.
  Revocation waits for it. Previously committed messages are not undone.
- If revocation acquires its lock first and commits, later writers cannot pass
  the active-device check. At stronger isolation levels a concurrent operation
  may instead receive a serialization error; it must not report success.
- No whole-table locks are required. Locking existing child records alone would
  not prevent new inserts after commit.
- This is an application transaction contract, not a database trigger. Direct
  SQL or future code bypassing the gate can still insert rows. New write paths
  must participate in the same lock/check protocol.

## Acceptance tests (not yet executed)

Use disposable fixtures in a test database; never revoke a production device.

1. Verify each result in the table; wrong password and wrong owner change no rows.
2. Revoke an active device, then revoke again: first true, then RevokedDevice;
   revoked_at remains unchanged.
3. Upload prekeys for a revoked device: false and no new prekeys.
4. Send an envelope with a revoked sender or recipient: permission denied;
   no new envelope. Claim/sync/ACK must also reject the revoked device.
5. Session A: BEGIN; SELECT the fixture device FOR UPDATE; update it to revoked
   without committing. Session B: invoke prekey upload/envelope insertion.
   B must wait; after A commits, B must reject (or surface serialization failure).
6. Session A: BEGIN; SELECT the active fixture device FOR SHARE. Session B:
   invoke revocation. B must wait until A commits/rolls back. A represents a
   previously authorized writer; subsequent writes after revocation must fail.
7. Concurrent revoke calls: one succeeds, one reports AlreadyRevoked at normal
   READ COMMITTED isolation; database failures must never be swallowed.

## Outside this change

The HTTP route/handler and main Pool dependency wiring are implemented.
Initial and repeated revocation broadcast to every Worker and wait up to a
shared 8-second deadline. Workers scan all owned same-device sessions, queue
Close 4001/device_revoked and close after output drains, or force closure at
the 2-second deadline checked by the existing 5-second timer. Busy Workers may
delay enforcement. No matching sessions also counts as confirmed closure.
HTTP 200/websocket_closed=true confirms local server cleanup, not client logout
or reason delivery. HTTP 202/websocket_closed=false confirms database revocation
but not socket cleanup; retry safely. Token revocation remains separate.
See backend-api.md Section 19 for required iOS logout/token cleanup.
Refresh tokens are not revoked by this method. No database migration was run.

## WebSocket integration test

tests/revoke_device_test.ps1 opens two connections for one disposable device,
revokes it, checks both Close reasons, and repeats revocation. This mutates the
test device permanently; do not run against a real user's device. The script was
syntax-checked only; WSL build/live execution is not available in this session.
