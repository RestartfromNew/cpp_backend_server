"""Generate COPY fixtures, never connect to PostgreSQL. Disposable test data only."""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import uuid
from contextlib import ExitStack
from datetime import datetime, timezone
from pathlib import Path

from fix_fixture_device_times import DEVICE_COLUMNS, device_import_sql

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
from cryptography.hazmat.primitives.kdf.argon2 import Argon2id

TABLES = {
    "users": ["id", "display_name", "username", "is_active"],
    "password_credentials": ["user_id", "login_email", "password_hash"],
    "user_devices": ["device_id", "user_id", "device_name", "status", "identity_public_key", "protocol_suite", "authorized_at"],
    "device_key_bundles": ["device_id", "key_version", "signed_prekey_id", "signed_prekey_public", "signed_prekey_signature", "is_current"],
    "device_one_time_prekeys": ["device_id", "prekey_id", "public_key", "state"],
    "friend_relation": ["user_id", "friend_id"],
}


def password_hash(password: str) -> str:
    # Matches the current server's libsodium interactive Argon2id work factors.
    # Stored as PHC, not plaintext. Clients still use plaintext for password login.
    return Argon2id(salt=os.urandom(16), length=32, iterations=2,
                    lanes=1, memory_cost=65536).derive_phc_encoded(password.encode("utf-8"))


def keypair() -> tuple[bytes, bytes]:
    key = X25519PrivateKey.generate()
    private = key.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
                                serialization.NoEncryption())
    public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    return private, public


def device_keys(mode: str, count: int):
    if mode == "opaque":
        # Correct database encodings only. These are NOT cryptographic public keys/signatures.
        return os.urandom(32), os.urandom(32), os.urandom(64), [os.urandom(32) for _ in range(count)], None
    import xeddsa
    # Same explicit test convention as the project's four-device fixture generator.
    while True:
        identity_private, identity = keypair()
        ed_public = xeddsa.priv_to_ed25519_pub(identity_private)
        if not ed_public[31] & 0x80:
            break
    if xeddsa.priv_to_curve25519_pub(identity_private) != identity:
        raise ValueError("X25519 identity convention mismatch")
    converted = xeddsa.curve25519_pub_to_ed25519_pub(identity, False)
    if converted != ed_public:
        raise ValueError("XEdDSA identity convention mismatch")
    signed_private, signed = keypair()
    message = b"\x05" + signed
    signature = xeddsa.ed25519_priv_sign(identity_private, message)
    if not xeddsa.ed25519_verify(signature, converted, message):
        raise ValueError("Generated signed prekey signature failed verification")
    pairs = [keypair() for _ in range(count)]
    private = {"test_only": True, "identity_private_hex": identity_private.hex(),
               "signed_prekey_id": 42, "signed_prekey_private_hex": signed_private.hex(),
               "one_time_prekeys": [{"prekey_id": 1001+i, "private_hex": pair[0].hex()}
                                    for i, pair in enumerate(pairs)]}
    return identity, signed, signature, [pair[1] for pair in pairs], private


def bytea(value: bytes) -> str:
    return "\\x" + value.hex()


def import_sql(database: str, users: int, devices: int, prekeys: int, friendships: int) -> str:
    # Identifier is separately restricted before interpolation. SQL files are fixed-name CSVs.
    lines = ["\\set ON_ERROR_STOP on", "SET client_encoding = 'UTF8';", "BEGIN;",
             "SET LOCAL lock_timeout = '10s';", "SET CONSTRAINTS ALL DEFERRED;",
             f"DO $seed$ BEGIN IF current_database() <> '{database}' THEN",
             "RAISE EXCEPTION 'Wrong target database for load fixtures'; END IF; END $seed$;",
             "-- Table definitions must already match current repositories; no schema changes here."]
    for table, columns in TABLES.items():
        cols = ", ".join(columns)
        if table == "user_devices":
            if tuple(columns) != DEVICE_COLUMNS:
                raise ValueError("Device staging columns are out of sync")
            lines.append(device_import_sql())
        else:
            lines.append(f"\\copy app.{table} ({cols}) FROM '{table}.csv' WITH (FORMAT csv, HEADER true)")
    lines += ["COMMIT;", "-- Refresh planner statistics only AFTER a successful bulk import."]
    lines += [f"ANALYZE app.{table};" for table in TABLES]
    lines += [f"\\echo Imported: {users} users, {devices} devices/bundles, {prekeys} prekeys, {friendships} directed friend rows."]
    return "\n".join(lines) + "\n"


def validate(args):
    if not re.fullmatch(r"[a-z][a-z0-9_]{0,11}", args.run):
        raise ValueError("--run must match [a-z][a-z0-9_]{0,11}")
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{0,62}", args.database):
        raise ValueError("--database must be an unquoted PostgreSQL database name")
    if not 2 <= args.accounts <= 1_000_000 or args.accounts % 2:
        raise ValueError("--accounts must be even and between 2 and 1000000")
    if not 1 <= args.devices <= 16:
        raise ValueError("--devices must be 1..16 per account")
    if not 0 <= args.prekeys <= 10000:
        raise ValueError("--prekeys must be 0..10000 per device")
    if not args.password:
        raise ValueError("--password cannot be empty")
    if args.mode == "xeddsa":
        try:
            import xeddsa  # noqa: F401
        except ImportError as error:
            raise ValueError("xeddsa mode requires XEdDSA==1.2.0; see requirements-xeddsa.txt") from error


def generate(args) -> dict:
    validate(args)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=False)  # Never overwrite an earlier fixture set.
    now = datetime.now(timezone.utc).isoformat()
    namespace = uuid.uuid5(uuid.NAMESPACE_URL, "cpp-backend-load-fixture-v1/" + args.run)
    user_id = lambda index: str(uuid.uuid5(namespace, f"user/{index}"))
    shared_hash = password_hash(args.password) if args.shared_test_hash else None
    suite = "LOADTEST-OPAQUE-v1" if args.mode == "opaque" else "TEST-X25519-XEdDSA-HKDF-SHA256-v1"
    with ExitStack() as stack:
        writers = {}
        for table, columns in TABLES.items():
            stream = stack.enter_context((out / (table + ".csv")).open("x", encoding="utf-8", newline=""))
            writers[table] = csv.writer(stream)
            writers[table].writerow(columns)
        accounts = stack.enter_context((out / "accounts.jsonl").open("x", encoding="utf-8"))
        public = stack.enter_context((out / "public_bundles.jsonl").open("x", encoding="utf-8"))
        private = (stack.enter_context((out / "private_keys.TEST_ONLY.jsonl").open("x", encoding="utf-8"))
                   if args.mode == "xeddsa" else None)
        for index in range(1, args.accounts + 1):
            uid = user_id(index)
            username = f"lt_{args.run}_{index:07d}"
            email = username + "@example.com"
            peer_index = index+1 if index % 2 else index-1
            peer_id = user_id(peer_index)
            writers["users"].writerow([uid, f"Load {index}", username, "true"])
            writers["password_credentials"].writerow([uid, email, shared_hash or password_hash(args.password)])
            device_ids = []
            peer_devices = []
            for number in range(1, args.devices + 1):
                did = str(uuid.uuid5(namespace, f"device/{index}/{number}"))
                peer_did = str(uuid.uuid5(namespace, f"device/{peer_index}/{number}"))
                device_ids.append(did); peer_devices.append(peer_did)
                identity, signed, signature, prekeys, secret = device_keys(args.mode, args.prekeys)
                writers["user_devices"].writerow([did, uid, f"load-{number}", "active", bytea(identity), suite, now])
                writers["device_key_bundles"].writerow([did, 1, 42, bytea(signed), bytea(signature), "true"])
                for key_index, value in enumerate(prekeys, 1001):
                    writers["device_one_time_prekeys"].writerow([did, key_index, bytea(value), "available"])
                bundle = {"user_id": uid, "device_id": did, "protocol_suite": suite,
                          "identity_public_key_hex": identity.hex(), "signed_prekey_id": 42,
                          "signed_prekey_public_hex": signed.hex(), "signed_prekey_signature_hex": signature.hex(),
                          "signature_message_encoding": "05 || raw signed prekey" if args.mode == "xeddsa" else "NONE: opaque fixture",
                          "one_time_prekeys": [{"prekey_id": 1001+i, "public_key_hex": value.hex()}
                                               for i, value in enumerate(prekeys)]}
                public.write(json.dumps(bundle) + "\n")
                if private:
                    private.write(json.dumps({"user_id": uid, "device_id": did, **secret}) + "\n")
            writers["friend_relation"].writerow([uid, peer_id])
            accounts.write(json.dumps({"user_id": uid, "email": email, "username": username,
                                       "password": args.password, "device_ids": device_ids,
                                       "peer_user_id": peer_id, "peer_device_ids": peer_devices}) + "\n")
    total_devices = args.accounts * args.devices
    info = {"test_only": True, "format_version": 1, "run": args.run, "target_database": args.database,
            "mode": args.mode, "protocol_suite": suite, "created_at": now,
            "accounts": args.accounts, "devices": total_devices, "bundles": total_devices,
            "prekeys": total_devices * args.prekeys, "directed_friend_rows": args.accounts,
            "friend_pairs": args.accounts // 2, "shared_test_password_hash": args.shared_test_hash,
            "argon2id": {"iterations": 2, "memory_kib": 65536, "lanes": 1},
            "contains_plaintext_test_passwords": True, "contains_tokens": False,
            "complete": True}
    (out / "manifest.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    # Write the executable import entry point last, after every data file is closed.
    (out / "import.sql").write_text(import_sql(args.database, args.accounts, total_devices,
                                                info["prekeys"], args.accounts), encoding="utf-8")
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True, help="Exact target test database; checked at import time")
    parser.add_argument("--out", required=True, help="A NEW output directory")
    parser.add_argument("--run", default="load01", help="Short unique fixture run label")
    parser.add_argument("--accounts", type=int, default=1000, help="Even count; paired as 1<->2, 3<->4, ...")
    parser.add_argument("--devices", type=int, default=1, help="Devices per account")
    parser.add_argument("--prekeys", type=int, default=20, help="Available one-time keys per device")
    parser.add_argument("--password", default="TestPass123@", help="Disposable test password, exported in accounts.jsonl")
    parser.add_argument("--shared-test-hash", action="store_true", help="Faster fixture generation: reuse one test hash; never use for real users")
    parser.add_argument("--mode", choices=["opaque", "xeddsa"], default="opaque")
    args = parser.parse_args()
    try:
        info = generate(args)
    except (ValueError, FileExistsError) as error:
        parser.exit(1, f"Generation failed: {error}\n")
    print(json.dumps({k: info[k] for k in ["run", "mode", "accounts", "devices", "bundles", "prekeys", "friend_pairs"]}, indent=2))
    print("Files generated only; no database was contacted. Read README before importing.")


if __name__ == "__main__":
    main()
