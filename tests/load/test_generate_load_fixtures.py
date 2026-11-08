import argparse
import csv
import importlib.util
import json
import tempfile
import unittest
import uuid
from pathlib import Path

from cryptography.exceptions import InvalidKey
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
from cryptography.hazmat.primitives.kdf.argon2 import Argon2id

import generate_load_fixtures as fixture
from fix_fixture_device_times import repair, MARKER, DEVICE_COLUMNS


class FixtureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name) / "new"
        self.args = argparse.Namespace(database="backend_test", out=str(self.out), run="case01",
            accounts=4, devices=2, prekeys=3, password="TestPass123@", shared_test_hash=True, mode="opaque")

    def rows(self, table):
        with (self.out / (table + ".csv")).open(encoding="utf-8", newline="") as stream:
            return list(csv.DictReader(stream))

    def test_rows_keys_pairing_and_sql(self):
        info = fixture.generate(self.args)
        self.assertEqual((info["accounts"], info["devices"], info["prekeys"]), (4, 8, 24))
        users = self.rows("users"); devices = self.rows("user_devices")
        self.assertEqual(len(users), 4)
        ids = {row["id"] for row in users}
        self.assertEqual(len(ids), 4)
        self.assertTrue(all(uuid.UUID(i) for i in ids))
        self.assertTrue(all(row["is_active"] == "true" for row in users))
        self.assertEqual(len(devices), 8)
        dids = {row["device_id"] for row in devices}
        self.assertEqual(len(dids), 8)
        self.assertTrue(all(row["user_id"] in ids and row["status"] == "active" for row in devices))
        self.assertTrue(all(len(bytes.fromhex(row["identity_public_key"][2:])) == 32 for row in devices))
        bundles = self.rows("device_key_bundles")
        self.assertEqual({row["device_id"] for row in bundles}, dids)
        self.assertTrue(all(row["is_current"] == "true" and row["signed_prekey_id"] == "42" for row in bundles))
        self.assertTrue(all(len(bytes.fromhex(row["signed_prekey_signature"][2:])) == 64 for row in bundles))
        keys = self.rows("device_one_time_prekeys")
        self.assertEqual(len(keys), 24)
        self.assertEqual(len({(row["device_id"], row["prekey_id"]) for row in keys}), 24)
        self.assertTrue(all(row["device_id"] in dids and row["state"] == "available" for row in keys))
        self.assertTrue(all(len(bytes.fromhex(row["public_key"][2:])) == 32 for row in keys))
        friends = {(row["user_id"], row["friend_id"]) for row in self.rows("friend_relation")}
        self.assertEqual(len(friends), 4)
        self.assertTrue(all(a != b and (b, a) in friends for a, b in friends))
        accounts = [json.loads(line) for line in (self.out / "accounts.jsonl").read_text().splitlines()]
        self.assertEqual(len(accounts), 4)
        for account in accounts:
            self.assertIn((account["user_id"], account["peer_user_id"]), friends)
            self.assertTrue(set(account["peer_device_ids"]) <= dids)
            self.assertEqual(account["password"], self.args.password)
        credentials = self.rows("password_credentials")
        hashes = {row["password_hash"] for row in credentials}
        self.assertEqual(len(hashes), 1)
        encoded = hashes.pop()
        self.assertTrue(encoded.startswith("$argon2id$v=19$m=65536,t=2,p=1$"))
        Argon2id.verify_phc_encoded(self.args.password.encode(), encoded)
        with self.assertRaises(InvalidKey):
            Argon2id.verify_phc_encoded(b"wrong", encoded)
        self.assertFalse((self.out / "private_keys.TEST_ONLY.jsonl").exists())
        sql = (self.out / "import.sql").read_text()
        self.assertIn("current_database() <> 'backend_test'", sql)
        self.assertEqual(sql.count("\\copy "), 6)
        self.assertIn("CURRENT_TIMESTAMP, CURRENT_TIMESTAMP FROM load_fixture_devices", sql)
        self.assertIn(MARKER, sql)
        self.assertNotIn("\\copy app.user_devices", sql)
        self.assertLess(sql.index("app.users"), sql.index("app.user_devices"))
        self.assertLess(sql.index("app.user_devices"), sql.index("app.device_key_bundles"))
        self.assertLess(sql.index("friend_relation.csv"), sql.index("COMMIT;"))
        self.assertLess(sql.index("COMMIT;"), sql.index("ANALYZE"))
        self.assertNotIn("TRUNCATE", sql)
        self.assertNotIn("DISABLE TRIGGER", sql)
        self.assertNotIn("ON CONFLICT", sql)
        self.assertEqual(json.loads((self.out / "manifest.json").read_text())["complete"], True)

    def test_repair_existing_import_is_backed_up_and_idempotent(self):
        self.out.mkdir()
        path = self.out / "import.sql"
        original = ("BEGIN;\n\\copy app.user_devices (" + ", ".join(DEVICE_COLUMNS) +
            ") FROM 'user_devices.csv' WITH (FORMAT csv, HEADER true)\nCOMMIT;\n").encode()
        path.write_bytes(original)
        self.assertTrue(repair(path))
        self.assertEqual(path.with_name(path.name + ".before_device_time_fix").read_bytes(), original)
        fixed = path.read_bytes()
        self.assertIn(b"CURRENT_TIMESTAMP, CURRENT_TIMESTAMP", fixed)
        self.assertIn(b"created_at, authorized_at", fixed)
        self.assertFalse(repair(path))
        self.assertEqual(path.read_bytes(), fixed)

    def test_repair_refuses_unrecognized_sql(self):
        self.out.mkdir()
        path = self.out / "import.sql"
        original = b"SELECT 1;\n"
        path.write_bytes(original)
        with self.assertRaises(ValueError): repair(path)
        self.assertEqual(path.read_bytes(), original)

    def test_refuses_overwrite(self):
        self.out.mkdir()
        (self.out / "keep").write_text("unchanged")
        with self.assertRaises(FileExistsError): fixture.generate(self.args)
        self.assertEqual((self.out / "keep").read_text(), "unchanged")

    def test_unique_hashes_and_zero_prekeys(self):
        self.args.accounts = 2; self.args.devices = 1; self.args.prekeys = 0
        self.args.shared_test_hash = False
        fixture.generate(self.args)
        hashes = {row["password_hash"] for row in self.rows("password_credentials")}
        self.assertEqual(len(hashes), 2)
        self.assertEqual(self.rows("device_one_time_prekeys"), [])

    def test_parameter_validation_before_files(self):
        for name, value in [("run", "bad';drop"), ("database", "bad'db"), ("accounts", 3),
                            ("accounts", 0), ("devices", 0), ("prekeys", -1), ("password", "")]:
            args = argparse.Namespace(**vars(self.args)); setattr(args, name, value)
            with self.assertRaises(ValueError): fixture.generate(args)
            self.assertFalse(self.out.exists())

    def test_missing_xeddsa_is_explicit(self):
        if importlib.util.find_spec("xeddsa"):
            self.skipTest("xeddsa is installed")
        self.args.mode = "xeddsa"
        with self.assertRaisesRegex(ValueError, "XEdDSA"):
            fixture.generate(self.args)
        self.assertFalse(self.out.exists())

    @unittest.skipUnless(importlib.util.find_spec("xeddsa"), "optional XEdDSA dependency is not installed")
    def test_real_test_bundle_and_private_material(self):
        import xeddsa
        self.args.mode = "xeddsa"; self.args.accounts = 2; self.args.devices = 1
        fixture.generate(self.args)
        private = [json.loads(line) for line in (self.out / "private_keys.TEST_ONLY.jsonl").read_text().splitlines()]
        public = [json.loads(line) for line in (self.out / "public_bundles.jsonl").read_text().splitlines()]
        for a, b in zip(private, public):
            self.assertEqual(a["device_id"], b["device_id"])
            key = X25519PrivateKey.from_private_bytes(bytes.fromhex(a["identity_private_hex"]))
            raw = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
            self.assertEqual(raw.hex(), b["identity_public_key_hex"])
            self.assertTrue(xeddsa.ed25519_verify(bytes.fromhex(b["signed_prekey_signature_hex"]),
                xeddsa.curve25519_pub_to_ed25519_pub(raw, False), b"\x05"+bytes.fromhex(b["signed_prekey_public_hex"])))


if __name__ == "__main__":
    unittest.main()
