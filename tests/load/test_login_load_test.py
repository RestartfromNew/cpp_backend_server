import contextlib
import csv
import io
import json
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import login_load_test as load


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_POST(self):
        state = self.server.state
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert self.path == "/login_by_email"
        assert body["password"] == "secret-test-password"
        with state["lock"]:
            state["calls"] += 1
            call = state["calls"]
            state["active"] += 1
            state["peak"] = max(state["peak"], state["active"])
        try:
            time.sleep(state["delay"])
            mode = state["mode"]
            status = 200
            response = {"code": "login_success", "login_ticket": "a"*64, "expires_in": 300}
            if mode == "invalid_ticket":
                response["login_ticket"] = "wrong-format"
            elif mode == "forbidden":
                status = 403; response = {"error": {"code": "account_disabled"}}
            elif mode == "fail_after_probe" and call > 3:
                status = 503; response = {"code": "service_unavailable"}
            raw = b"not-json" if mode == "invalid_json" else json.dumps(response).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.send_header("CF-Ray", "test-trace-123")
            self.end_headers()
            try:
                self.wfile.write(raw)
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
        finally:
            with state["lock"]:
                state["active"] -= 1


class LoadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.accounts_file = self.root/"accounts.jsonl"
        self.accounts_file.write_text("\n".join(json.dumps({"email": f"test{i}@example.com", "password": "secret-test-password"})
                                               for i in range(4)), encoding="utf-8")
        self.accounts = load.load_accounts(self.accounts_file)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.server.state = {"calls": 0, "active": 0, "peak": 0, "mode": "ok", "delay": .015, "lock": threading.Lock()}
        thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def test_percentiles_and_success_throughput(self):
        self.assertEqual(load.percentile([10, 20, 30, 40], 50), 25)
        self.assertAlmostEqual(load.percentile([10, 20, 30, 40], 95), 38.5)
        self.assertIsNone(load.percentile([], 95))
        rows, elapsed, cpu, reason = load.run_stage(load.Target(self.base, 1), self.accounts, 4, 12, 10, "measured")
        result = load.summarize(rows, elapsed, cpu, reason, 12)
        self.assertEqual(len(rows), 12)
        self.assertEqual({r.request_id for r in rows}, set(range(12)))
        self.assertTrue(all(r.success for r in rows))
        self.assertLessEqual(self.server.state["peak"], 4)
        self.assertGreaterEqual(self.server.state["peak"], 2)
        self.assertGreaterEqual(elapsed, max(r.finished_s for r in rows))
        self.assertAlmostEqual(result["success_rps"], 12/elapsed)
        rows[0].success = False; rows[0].error = "timeout"
        result = load.summarize(rows, elapsed, cpu, reason, 12)
        self.assertAlmostEqual(result["success_rps"], 11/elapsed)
        self.assertAlmostEqual(result["error_rate"], 1/12)

    def test_timeout_and_admission_deadline(self):
        self.server.state["delay"] = .2
        rows, elapsed, cpu, reason = load.run_stage(load.Target(self.base, .05), self.accounts, 2, 100, .01, "measured")
        self.assertLessEqual(len(rows), 2)
        self.assertTrue(rows)
        self.assertTrue(all(r.error == "timeout" and not r.success for r in rows))
        self.assertEqual(reason, "admission_deadline")
        result = load.summarize(rows, elapsed, cpu, reason, 100)
        self.assertEqual(result["success_rps"], 0)
        self.assertIsNone(result["success_p95_ms"])

    def test_business_validation(self):
        for mode, error in [("invalid_ticket", "invalid_ticket_response"), ("invalid_json", "invalid_json")]:
            self.server.state["mode"] = mode
            rows, _, _, _ = load.run_stage(load.Target(self.base, 1), self.accounts, 1, 1, 5, "probe")
            self.assertFalse(rows[0].success)
            self.assertEqual(rows[0].error, error)

    def run_cli(self, out, extra=None):
        args = ["--accounts", str(self.accounts_file), "--out", str(out), "--base-url", self.base,
                "--concurrency", "1,2", "--requests", "6", "--warmup", "2", "--cooldown", "0"]
        with contextlib.redirect_stdout(io.StringIO()):
            return load.main(args + (extra or []))

    def test_cli_outputs_plotting_and_no_secrets(self):
        out = self.root/"results"
        self.assertEqual(self.run_cli(out), 0)
        data = json.loads((out/"summary.json").read_text())
        self.assertEqual([s["completed"] for s in data["stages"]], [6, 6])
        self.assertEqual(self.server.state["calls"], 3 + 4 + 12)
        with (out/"requests.csv").open(encoding="utf-8-sig", newline="") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(len([r for r in rows if r["phase"] == "measured"]), 12)
        self.assertTrue((out/"performance.png").read_bytes().startswith(b"\x89PNG"))
        self.assertIn("<svg", (out/"performance.svg").read_text())
        for name in ["summary.json", "summary.csv", "requests.csv", "performance.svg"]:
            text = (out/name).read_text(encoding="utf-8-sig")
            self.assertNotIn("secret-test-password", text)
            self.assertNotIn("test0@example.com", text)
            self.assertNotIn("a"*64, text)
        before = self.server.state["calls"]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(load.main(["--out", str(out), "--plot-only"]), 0)
        self.assertEqual(self.server.state["calls"], before)

    def test_probe_failure_sends_no_load(self):
        self.server.state["mode"] = "forbidden"
        out = self.root/"failed"
        self.assertEqual(self.run_cli(out), 2)
        data = json.loads((out/"summary.json").read_text())
        self.assertEqual(data["outcome"], "probe_failed")
        self.assertEqual(data["stages"], [])
        self.assertEqual(self.server.state["calls"], 1)
        self.assertFalse((out/"performance.png").exists())

    def test_error_threshold_stops_next_stage(self):
        self.server.state["mode"] = "fail_after_probe"
        out = self.root/"threshold"
        self.assertEqual(self.run_cli(out, ["--warmup", "0"]), 2)
        data = json.loads((out/"summary.json").read_text())
        self.assertEqual(data["outcome"], "stopped_at_threshold")
        self.assertEqual(len(data["stages"]), 1)
        self.assertEqual(data["stages"][0]["successes"], 0)
        self.assertEqual(self.server.state["calls"], 9)

    def test_invalid_configuration_before_network(self):
        for url in ["http://user:pass@localhost", "ftp://localhost", "https://localhost/?token=secret"]:
            with self.assertRaises(ValueError): load.Target(url, 1)
        duplicate = self.root/"duplicate.jsonl"
        duplicate.write_text(self.accounts_file.read_text()+"\n"+self.accounts_file.read_text().splitlines()[0])
        with self.assertRaises(ValueError): load.load_accounts(duplicate)
        self.assertEqual(self.server.state["calls"], 0)


if __name__ == "__main__":
    unittest.main()
