"""Small, bounded closed-loop login test. No device/WS requests, retries or ticket logging."""
from __future__ import annotations

import argparse
import csv
import http.client
import json
import math
import platform
import re
import socket
import ssl
import statistics
import sys
import threading
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlsplit

TICKET = re.compile(r"[0-9a-f]{64}\Z")
SAFE_CODE = re.compile(r"[A-Za-z0-9_-]{1,64}\Z")
MAX_RESPONSE = 65536


@dataclass
class Sample:
    phase: str
    concurrency: int
    request_id: int
    worker: int
    account_index: int
    started_s: float
    finished_s: float
    latency_ms: float
    http_status: int
    success: bool
    error: str
    business_code: str
    response_bytes: int
    edge_request_id: str


def load_accounts(path, limit=1000):
    """Pre-encode request bodies outside measured time. Never print input records."""
    accounts, seen = [], set()
    with Path(path).open(encoding="utf-8-sig") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
                email, password = row["email"], row["password"]
                if not isinstance(email, str) or not email.strip() or not isinstance(password, str) or not password:
                    raise ValueError()
                if email.lower() in seen:
                    raise ValueError()
                seen.add(email.lower())
                accounts.append(json.dumps({"email": email, "password": password}, ensure_ascii=False).encode("utf-8"))
            except (ValueError, KeyError, TypeError):
                raise ValueError(f"Invalid/duplicate account at line {number}; input content omitted") from None
            if len(accounts) >= limit:
                break
    if not accounts:
        raise ValueError("No accounts found")
    return accounts


class Target:
    def __init__(self, base_url, timeout, keep_alive=False, ca_bundle=None):
        parsed = urlsplit(base_url)
        if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password:
            raise ValueError("Use an http(s) base URL without credentials")
        if parsed.query or parsed.fragment or parsed.path not in ("", "/"):
            raise ValueError("Base URL must contain only scheme, hostname and optional port")
        self.scheme, self.host = parsed.scheme, parsed.hostname
        self.port = parsed.port or (443 if self.scheme == "https" else 80)
        self.timeout, self.keep_alive = timeout, keep_alive
        self.url = base_url.rstrip("/") + "/login_by_email"
        self.context = ssl.create_default_context(cafile=ca_bundle) if self.scheme == "https" else None
        if self.context:
            self.context.set_alpn_protocols(["http/1.1"])

    def connection(self):
        if self.context:
            return http.client.HTTPSConnection(self.host, self.port, timeout=self.timeout, context=self.context)
        return http.client.HTTPConnection(self.host, self.port, timeout=self.timeout)

    def request(self, connection, body, phase, concurrency, request_id, worker, account_index, epoch):
        started = time.perf_counter()
        status, success, error, code, size, edge = 0, False, "", "", 0, ""
        try:
            connection.request("POST", "/login_by_email", body=body, headers={
                "Content-Type": "application/json; charset=utf-8", "Accept": "application/json",
                "Connection": "keep-alive" if self.keep_alive else "close",
                "User-Agent": "BackendLoginSmallLoadTest/1.0"})
            response = connection.getresponse()
            status = response.status
            raw_edge = response.getheader("CF-Ray", "")
            edge = raw_edge if re.fullmatch(r"[A-Za-z0-9-]{1,80}", raw_edge) else ""
            raw = response.read(MAX_RESPONSE + 1)
            size = len(raw)
            if size > MAX_RESPONSE:
                error = "response_too_large"
                connection.close()
            else:
                try:
                    data = json.loads(raw)
                except (ValueError, UnicodeError):
                    data = None
                if not isinstance(data, dict):
                    error = "invalid_json" if status == 200 else "http_non_json"
                else:
                    nested = data.get("error")
                    raw_code = data.get("code", nested.get("code", "") if isinstance(nested, dict) else "")
                    code = raw_code if isinstance(raw_code, str) and SAFE_CODE.fullmatch(raw_code) else ""
                    ticket = data.get("login_ticket")
                    success = status == 200 and code == "login_success" and isinstance(ticket, str) and bool(TICKET.fullmatch(ticket))
                    if not success:
                        error = "http_status" if status != 200 else "business_code" if code != "login_success" else "invalid_ticket_response"
        except (socket.timeout, TimeoutError):
            error = "timeout"; connection.close()
        except ssl.SSLError:
            error = "tls_error"; connection.close()
        except socket.gaierror:
            error = "dns_error"; connection.close()
        except OSError:
            error = "network_error"; connection.close()
        except http.client.HTTPException:
            error = "http_protocol_error"; connection.close()
        finally:
            if not self.keep_alive:
                connection.close()
        finished = time.perf_counter()
        return Sample(phase, concurrency, request_id, worker, account_index,
                      started-epoch, finished-epoch, (finished-started)*1000,
                      status, success, error, code, size, edge)


def run_stage(target, accounts, concurrency, requests, max_seconds, phase):
    """At most C in flight. Stop admission at time/count bound, then drain in-flight calls."""
    records, lock, stop = [], threading.Lock(), threading.Event()
    state = {"next": 0, "started": 0.0}
    gate = threading.Barrier(concurrency + 1, action=lambda: state.update(started=time.perf_counter()))
    cpu_started = time.process_time()

    def worker(number):
        connection = target.connection()
        try:
            gate.wait(timeout=30)
            while not stop.is_set():
                with lock:
                    if state["next"] >= requests or time.perf_counter()-state["started"] >= max_seconds:
                        return
                    request_id = state["next"]
                    state["next"] += 1
                index = request_id % len(accounts)
                sample = target.request(connection, accounts[index], phase, concurrency,
                    request_id, number, index+1, state["started"])
                with lock:
                    records.append(sample)
                if sample.http_status in (401, 403, 429):
                    stop.set()  # Do not hammer rejected credentials, WAF or rate limits.
        finally:
            connection.close()

    interrupted = False
    pool = ThreadPoolExecutor(max_workers=concurrency)
    futures = [pool.submit(worker, number) for number in range(concurrency)]
    try:
        gate.wait(timeout=30)
        for future in futures:
            future.result()
    except KeyboardInterrupt:
        interrupted = True; stop.set()
    except BaseException:
        stop.set(); gate.abort()
        raise
    finally:
        pool.shutdown(wait=True)
    elapsed = time.perf_counter()-state["started"]
    reason = "interrupted" if interrupted else "auth_or_rate_limit" if stop.is_set() else "request_budget" if len(records) == requests else "admission_deadline"
    return records, elapsed, time.process_time()-cpu_started, reason


def percentile(values, percent):
    if not values:
        return None
    values = sorted(values)
    rank = (len(values)-1)*percent/100
    lower, upper = math.floor(rank), math.ceil(rank)
    return values[lower] + (values[upper]-values[lower])*(rank-lower)


def summarize(records, elapsed, cpu, reason, requested):
    ok = [row.latency_ms for row in records if row.success]
    all_latencies = [row.latency_ms for row in records]
    errors = Counter((f"http_{row.http_status}:" if row.http_status else "") + row.error +
                     (":" + row.business_code if row.business_code and row.business_code != "login_success" else "")
                     for row in records if not row.success)
    return {"concurrency": records[0].concurrency if records else 0, "requested": requested,
            "completed": len(records), "successes": len(ok), "failures": len(records)-len(ok),
            "elapsed_s": elapsed, "success_rps": len(ok)/elapsed if elapsed else 0,
            "total_rps": len(records)/elapsed if elapsed else 0,
            "error_rate": (len(records)-len(ok))/len(records) if records else 1.0,
            "success_mean_ms": statistics.mean(ok) if ok else None,
            "success_p50_ms": percentile(ok, 50), "success_p95_ms": percentile(ok, 95),
            "success_p99_ms": percentile(ok, 99), "all_p95_ms": percentile(all_latencies, 95),
            "client_cpu_s": cpu, "stop_reason": reason, "errors": dict(errors),
            "percentile_note": "Small sample: tail percentiles are exploratory" if len(ok) < 100 else "Linear interpolation percentiles"}


def write_csv(path, rows, names):
    with Path(path).open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=names)
        writer.writeheader(); writer.writerows(rows)


def save_results(out, config, stages, samples, outcome):
    data = {"config": config, "outcome": outcome, "stages": stages,
            "notes": ["Client-observed HTTP/1.1 closed-loop finite-batch test, not fixed arrival rate or a server capacity certification.",
                      "Only measured phases contribute to stage metrics; probe/warmup/cooldown excluded.",
                      "Throughput denominator includes draining all admitted requests; socket timeout is not a hard total deadline.",
                      "Success percentiles exclude errors; failures and all_p95_ms are reported separately.",
                      "Server CPU/RAM/database waits were not collected. Observe them separately.",
                      "No password, account email, response body or ticket is stored in results."]}
    (out/"summary.json").write_text(json.dumps(data, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    write_csv(out/"requests.csv", [asdict(row) for row in samples], list(Sample.__dataclass_fields__))
    names = ["concurrency", "requested", "completed", "successes", "failures", "elapsed_s",
             "success_rps", "total_rps", "error_rate", "success_mean_ms", "success_p50_ms", "success_p95_ms",
             "success_p99_ms", "all_p95_ms", "client_cpu_s", "stop_reason"]
    write_csv(out/"summary.csv", [{name: row[name] for name in names} for row in stages], names)


def plot_results(out):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    data = json.loads((out/"summary.json").read_text(encoding="utf-8"))
    stages = data["stages"]
    if not stages:
        return False
    x = [row["concurrency"] for row in stages]
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.6), layout="constrained")
    axes[0].plot(x, [r["success_rps"] for r in stages], "o-", label="Successful logins/s")
    axes[0].plot(x, [r["total_rps"] for r in stages], "s--", label="All completions/s", alpha=.7)
    axes[0].set(title="Completed throughput", ylabel="Requests / second")
    for key, label in [("success_p50_ms", "P50"), ("success_p95_ms", "P95")]:
        axes[1].plot(x, [r[key] if r[key] is not None else float("nan") for r in stages], "o-", label=label)
    axes[1].set(title="Successful-login latency", ylabel="Milliseconds")
    axes[2].plot(x, [r["error_rate"]*100 for r in stages], "o-", color="#bf4242", label="Failed / completed")
    axes[2].set(title="Error rate", ylabel="Percent")
    for axis in axes:
        axis.set_xlabel("Concurrent clients"); axis.set_xticks(x); axis.set_ylim(bottom=0)
        axis.grid(alpha=.25); axis.legend(fontsize=8)
    if not any(r["failures"] for r in stages):
        axes[2].set_ylim(0, 1)
    counts = ", ".join(f"C{r['concurrency']}: {r['successes']}/{r['completed']} OK" for r in stages)
    fig.suptitle("Login baseline | " + data["config"]["url"] + "\n" + counts, fontsize=11)
    fig.savefig(out/"performance.png", dpi=160)
    fig.savefig(out/"performance.svg")
    plt.close(fig)
    return True


def positive_float(value):
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError("Must be a finite positive number")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--accounts", help="accounts.jsonl from generate_load_fixtures.py")
    parser.add_argument("--base-url", default="https://aittelegramapi.uk")
    parser.add_argument("--out", required=True, help="Output folder (auto-suffixed if occupied), or existing results for --plot-only")
    parser.add_argument("--concurrency", default="1,2,4,8", help="Unique ascending levels in 1..64, e.g. 16,24,32,48,64")
    parser.add_argument("--requests", type=int, default=60, help="Measured request budget PER stage")
    parser.add_argument("--warmup", type=int, default=4, help="Extra requests PER stage, excluded from metrics")
    parser.add_argument("--max-seconds", type=positive_float, default=30, help="Stop admitting requests after this many seconds per phase; then drain")
    parser.add_argument("--timeout", type=positive_float, default=10, help="Socket operation timeout, seconds")
    parser.add_argument("--cooldown", type=float, default=3, help="Seconds between measured stages")
    parser.add_argument("--account-limit", type=int, default=1000)
    parser.add_argument("--ca-bundle", help="Optional trusted PEM CA file; certificate/hostname verification stays enabled")
    parser.add_argument("--keep-alive", action="store_true", help="Reuse one client connection per worker where supported")
    parser.add_argument("--stop-error-rate", type=float, default=.10, help="Do not advance if a stage reaches this failure fraction")
    parser.add_argument("--stop-p95-ms", type=positive_float, default=5000)
    parser.add_argument("--plot-only", action="store_true", help="Redraw saved summary; SENDS NO REQUESTS")
    args = parser.parse_args(argv)
    out = Path(args.out)
    try:
        import matplotlib  # Check plotting dependency before any network calls.
    except ImportError:
        parser.error("Install plotting dependency: python -m pip install -r tests/load/requirements-load.txt")
    if args.plot_only:
        print("Charts updated." if plot_results(out) else "No measured stages to plot.")
        return 0
    if not args.accounts:
        parser.error("--accounts is required for a live test")
    try:
        levels = [int(value) for value in args.concurrency.split(",")]
        if not levels or levels != sorted(set(levels)) or min(levels) < 1 or max(levels) > 64:
            raise ValueError("Use unique ascending concurrency levels in 1..64")
        if not 1 <= args.requests <= 1000 or not 0 <= args.warmup <= 100 or not 1 <= args.account_limit <= 10000:
            raise ValueError("requests: 1..1000; warmup: 0..100; account-limit: 1..10000")
        if not math.isfinite(args.cooldown) or args.cooldown < 0 or not 0 < args.stop_error_rate <= 1:
            raise ValueError("Invalid cooldown/error threshold")
        accounts = load_accounts(args.accounts, args.account_limit)
        target = Target(args.base_url, args.timeout, args.keep_alive, args.ca_bundle)
        requested_out = out
        suffix = 0
        while True:
            try:
                out.mkdir(parents=True, exist_ok=False)
                break
            except FileExistsError:
                suffix += 1
                out = requested_out.with_name(f"{requested_out.name}_{suffix:03d}")
        if out != requested_out:
            print(f"Output path already exists; using: {out.resolve()}", flush=True)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    config = {"started_at": datetime.now(timezone.utc).isoformat(), "url": target.url,
              "account_count": len(accounts), "concurrency": levels, "requests_per_stage": args.requests,
              "warmup_per_stage": args.warmup, "max_admission_seconds": args.max_seconds,
              "socket_timeout_s": args.timeout, "cooldown_s": args.cooldown,
              "connection_mode": "keep-alive" if args.keep_alive else "fresh TCP/TLS per request",
              "python": platform.python_version(), "client_platform": platform.system(),
              "stop_error_rate": args.stop_error_rate, "stop_p95_ms": args.stop_p95_ms,
              "max_request_budget": min(3, len(accounts)) + len(levels)*(args.requests+args.warmup)}
    stages, samples, outcome = [], [], "completed"
    print(f"Target: {target.url}\nMaximum in flight: {max(levels)}; total request cap: {config['max_request_budget']}", flush=True)
    print("Checking up to 3 accounts before load. No retries; no credentials saved.", flush=True)
    try:
        probe, elapsed, cpu, reason = run_stage(target, accounts, 1, min(3, len(accounts)), args.max_seconds, "probe")
        samples.extend(probe)
        if len(probe) != min(3, len(accounts)) or not all(row.success for row in probe) or reason == "interrupted":
            outcome = "probe_failed" if reason != "interrupted" else "interrupted"
            print("Preflight failed; no load stages started. Check requests.csv status/error fields.", flush=True)
            for row in probe:
                if not row.success:
                    print(f"  HTTP={row.http_status}, error={row.error}, code={row.business_code or 'N/A'}", flush=True)
        else:
            for concurrency in levels:
                if args.warmup:
                    warmup, _, _, reason = run_stage(target, accounts, concurrency, args.warmup, args.max_seconds, "warmup")
                    samples.extend(warmup)
                    if reason == "interrupted" or len(warmup) != args.warmup or not all(r.success for r in warmup):
                        outcome = "warmup_failed" if reason != "interrupted" else "interrupted"
                        break
                print(f"Starting concurrency={concurrency}, request budget={args.requests} ...", flush=True)
                records, elapsed, cpu, reason = run_stage(target, accounts, concurrency, args.requests, args.max_seconds, "measured")
                samples.extend(records)
                summary = summarize(records, elapsed, cpu, reason, args.requests)
                summary["concurrency"] = concurrency
                stages.append(summary)
                p95 = summary["success_p95_ms"]
                p95_text = f"{p95:.1f}" if p95 is not None else "N/A"
                print(f"  OK={summary['successes']}/{summary['completed']}, successful RPS={summary['success_rps']:.2f}, P95={p95_text} ms, error={summary['error_rate']:.1%}", flush=True)
                if reason in ("interrupted", "auth_or_rate_limit") or summary["error_rate"] >= args.stop_error_rate or (p95 is not None and p95 >= args.stop_p95_ms):
                    outcome = "interrupted" if reason == "interrupted" else "stopped_at_threshold"
                save_results(out, config, stages, samples, outcome)
                if outcome != "completed":
                    break
                if concurrency != levels[-1]:
                    time.sleep(args.cooldown)
    except KeyboardInterrupt:
        outcome = "interrupted"
    finally:
        save_results(out, config, stages, samples, outcome)
        if stages:
            plot_results(out)
    print(f"Outcome: {outcome}\nResults: {out.resolve()}", flush=True)
    return 0 if outcome == "completed" else 2


if __name__ == "__main__":
    sys.exit(main())
