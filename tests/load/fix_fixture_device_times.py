"""Repair an existing fixture import.sql without regenerating CSV/key material."""
import argparse
import os
import re
from pathlib import Path

DEVICE_COLUMNS = ("device_id", "user_id", "device_name", "status", "identity_public_key",
                  "protocol_suite", "authorized_at")
MARKER = "-- load-fixture-device-times-v2"


def device_import_sql():
    # Stage only in this session. Ignore the fixture generation timestamp and
    # assign both lifecycle timestamps using PostgreSQL's transaction clock.
    return "\n".join([
        MARKER,
        "CREATE TEMP TABLE load_fixture_devices (",
        "    device_id uuid, user_id uuid, device_name text, status text,",
        "    identity_public_key bytea, protocol_suite text, authorized_at timestamptz",
        ") ON COMMIT DROP;",
        "\\copy load_fixture_devices (" + ", ".join(DEVICE_COLUMNS) +
        ") FROM 'user_devices.csv' WITH (FORMAT csv, HEADER true)",
        "INSERT INTO app.user_devices (device_id, user_id, device_name, status,",
        "    identity_public_key, protocol_suite, created_at, authorized_at)",
        "SELECT device_id, user_id, device_name, status, identity_public_key, protocol_suite,",
        "    CURRENT_TIMESTAMP, CURRENT_TIMESTAMP FROM load_fixture_devices;",
    ])


def repair(path):
    path = Path(path).resolve(strict=True)
    original = path.read_bytes()
    text = original.decode("utf-8")
    matches = list(re.finditer(r"^\\copy app\.user_devices [^\r\n]*", text, re.M))
    if MARKER in text and not matches:
        return False
    expected = "\\copy app.user_devices (" + ", ".join(DEVICE_COLUMNS) + ") FROM 'user_devices.csv' WITH (FORMAT csv, HEADER true)"
    if len(matches) != 1 or matches[0].group() != expected:
        raise ValueError("Unrecognized device COPY statement; file was not changed")
    newline = "\r\n" if b"\r\n" in original else "\n"
    match = matches[0]
    updated = text[:match.start()] + device_import_sql().replace("\n", newline) + text[match.end():]
    backup = path.with_name(path.name + ".before_device_time_fix")
    if backup.exists():
        if backup.read_bytes() != original:
            raise ValueError("A different backup already exists; file was not changed")
    else:
        with backup.open("xb") as stream:
            stream.write(original)
    temporary = path.with_name(path.name + ".device_times.tmp")
    with temporary.open("xb") as stream:
        stream.write(updated.encode("utf-8"))
    os.replace(temporary, path)
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sql", help="Path to generated import.sql")
    args = parser.parse_args()
    try:
        changed = repair(args.sql)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Repair failed: {error}\n")
    print("Repaired import.sql; original backed up. CSV and keys unchanged." if changed else "Already repaired; no changes.")
