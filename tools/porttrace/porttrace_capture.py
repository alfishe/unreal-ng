#!/usr/bin/env python3
"""porttrace_capture.py — one-command Port Diagnostic Recorder capture session.

Drives a running Unreal-NG instance over the WebAPI (default http://localhost:8090):
enables the `porttrace` runtime feature, configures the filter, captures for a
duration (or until Enter), saves the canonical trace server-side, and converts
it to any requested formats with tools/porttrace/porttrace_convert.py.

Requires the emulator's WebAPI transport (the automation binary or unreal-qt
with automation enabled) running on the same machine — the server-side save
path must be readable by this script for conversion.

Examples:
  # Capture everything for 5 seconds, produce trace.json + trace.csv + trace.txt
  porttrace_capture.py --duration 5 -o /tmp/trace --to json,csv,text

  # AY-only OUTs interactively: live event counter, any key stops; markdown report
  porttrace_capture.py --include port=FFFD,direction=out \\
                       --include port=BFFD,direction=out \\
                       --wait-key -o /tmp/ay-session --to json,markdown

  # Boot debugging: keep the START of the run, big buffer
  porttrace_capture.py --capacity 262144 --overflow stop --duration 10 -o /tmp/boot

Design: docs/inprogress/2026-08-24-diagnostic-observability/
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import porttrace_convert  # noqa: E402  (sibling module: readers/writers)


class ApiError(RuntimeError):
    pass


def wait_for_keypress(poll_status):
    """Block until ANY key is pressed, showing a live capture ticker.

    poll_status() -> dict with 'events'/'total_evicted'/'total_filtered'
    (or None to skip the ticker update this round). Falls back to a plain
    Enter-terminated read when stdin is not a terminal.
    """
    if not sys.stdin.isatty():
        print("Capturing... press Enter to stop")
        try:
            input()
        except (EOFError, KeyboardInterrupt):
            pass
        return

    def ticker_line():
        try:
            status = poll_status()
        except ApiError:
            status = None
        if not status:
            return "Capturing... press any key to stop"
        line = f"Capturing... {status['events']} events"
        if status.get("total_evicted"):
            line += f" ({status['total_evicted']} evicted)"
        if status.get("total_filtered"):
            line += f" ({status['total_filtered']} filtered out)"
        if status.get("auto_stopped"):
            line += "  [BUFFER FULL - auto-stopped]"
        line += "  - press any key to stop"
        return line

    if sys.platform == "win32":
        import msvcrt
        while True:
            print("\r" + ticker_line().ljust(78), end="", flush=True)
            deadline = time.monotonic() + 0.5
            while time.monotonic() < deadline:
                if msvcrt.kbhit():
                    msvcrt.getch()
                    print()
                    return
                time.sleep(0.05)
    else:
        import select
        import termios
        import tty
        fd = sys.stdin.fileno()
        saved = termios.tcgetattr(fd)
        try:
            tty.setcbreak(fd)
            while True:
                print("\r" + ticker_line().ljust(78), end="", flush=True)
                ready, _, _ = select.select([sys.stdin], [], [], 0.5)
                if ready:
                    sys.stdin.read(1)
                    break
        except KeyboardInterrupt:
            pass
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, saved)
            print()


class WebApi:
    def __init__(self, base_url: str):
        self.base = base_url.rstrip("/")

    def request(self, method: str, path: str, body: dict = None) -> dict:
        url = self.base + path
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                payload = resp.read()
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode(errors="replace")
            try:
                detail = json.loads(detail).get("message", detail)
            except (ValueError, AttributeError):
                pass
            raise ApiError(f"{method} {path} -> HTTP {exc.code}: {detail}") from None
        except urllib.error.URLError as exc:
            raise ApiError(f"Cannot reach {url}: {exc.reason}. "
                           f"Is the emulator running with the WebAPI transport?") from None
        return json.loads(payload) if payload else {}

    def get(self, path: str) -> dict:
        return self.request("GET", path)

    def post(self, path: str, body: dict = None) -> dict:
        return self.request("POST", path, body if body is not None else {})

    def put(self, path: str, body: dict) -> dict:
        return self.request("PUT", path, body)


def resolve_emulator(api: WebApi, wanted: str) -> str:
    listing = api.get("/api/v1/emulator")
    emulators = listing.get("emulators", [])
    if wanted:
        for emu in emulators:
            if emu["id"] == wanted or emu["id"].startswith(wanted):
                return emu["id"]
        raise ApiError(f"Emulator '{wanted}' not found. Known: "
                       + ", ".join(e["id"] for e in emulators))
    if not emulators:
        raise ApiError("No emulator instances exist. Create/start one first "
                       "(POST /api/v1/emulator/start or via the CLI/GUI).")
    running = [e for e in emulators if e.get("is_running")]
    pick = (running or emulators)[0]
    if len(emulators) > 1:
        print(f"Multiple emulators; using {pick['id']} (override with --emulator)")
    return pick["id"]


def parse_rule(spec: str) -> dict:
    """'port=FFFD,direction=out,pc=3D00-3FFF,unmapped' -> WebAPI rule object."""
    rule = {}
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if part == "unmapped":
            rule["unmapped"] = True
            continue
        if "=" not in part:
            raise ValueError(f"Bad rule condition '{part}' "
                             f"(expected key=value or 'unmapped')")
        key, value = part.split("=", 1)
        key = key.strip().lower()
        value = value.strip()
        if key in ("port", "raw"):
            rule[key] = f"0x{int(value, 16):04X}"
        elif key == "device":
            rule["device"] = value
        elif key == "direction":
            rule["direction"] = value.lower()
        elif key == "pc":
            lo, hi = value.split("-")
            rule["pc"] = [f"0x{int(lo, 16):04X}", f"0x{int(hi, 16):04X}"]
        else:
            raise ValueError(f"Unknown rule key '{key}' (port/raw/device/direction/pc/unmapped)")
    return rule


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Capture a port trace over the WebAPI and convert it",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Examples:")[1] if "Examples:" in __doc__ else None)
    parser.add_argument("--url", default="http://localhost:8090", help="WebAPI base URL")
    parser.add_argument("--emulator", default="", help="Emulator id (or unique prefix); default: the running one")
    parser.add_argument("--duration", type=float, help="Capture length in seconds")
    parser.add_argument("--wait-key", action="store_true",
                        help="Interactive mode: capture until any key is pressed, "
                             "showing a live event counter")
    parser.add_argument("--preset", help="Filter preset: all|ay-only|fdc-only|no-fdc|no-fe|sound|paging|outs-only|ins-only|unmapped")
    parser.add_argument("--include", action="append", default=[], metavar="RULE",
                        help="Compound include rule, e.g. 'port=FFFD,direction=out' (repeatable; rules OR)")
    parser.add_argument("--exclude", action="append", default=[], metavar="RULE",
                        help="Compound exclude rule (repeatable; exclude wins)")
    parser.add_argument("--capacity", type=int,
                        help="Ring buffer capacity in events (default: auto-sized — "
                             "duration x 250k events/s x1.5 for --duration, 4M for --wait-key; "
                             "clamped to 1M..8M)")
    parser.add_argument("--overflow", choices=["ring", "stop"],
                        help="ring = keep newest (default), stop = keep start of run")
    parser.add_argument("-o", "--output", default="porttrace",
                        help="Output base path (extension added per format)")
    parser.add_argument("--to", default="json",
                        help="Comma-separated output formats: json,csv,text,markdown,bin")
    parser.add_argument("--summary", action="store_true", help="Print the trace summary when done")
    parser.add_argument("--no-enable", action="store_true",
                        help="Do not auto-enable the porttrace feature (fail if it is off)")
    args = parser.parse_args()

    if not args.duration and not args.wait_key:
        parser.error("Specify --duration SECONDS or --wait-key")

    formats = [f.strip().lower() for f in args.to.split(",") if f.strip()]
    for fmt in formats:
        if fmt not in ("json", "csv", "text", "txt", "markdown", "md", "bin", "binary", "binz"):
            parser.error(f"Unknown format '{fmt}'")

    api = WebApi(args.url)
    emu = resolve_emulator(api, args.emulator)
    base = f"/api/v1/emulator/{emu}/profiler/porttrace"

    # 1. Runtime feature gate
    if not args.no_enable:
        api.put(f"/api/v1/emulator/{emu}/feature/porttrace", {"enabled": True})
        print("Feature 'porttrace' enabled")

    # 2. Buffer configuration (only valid while stopped). A previous session may
    # have left the recorder capturing (crashed tool, manual CLI start) — stop it
    # first so config/start begin from a clean state.
    api.post(f"{base}/stop")
    # Unfiltered capture runs at 50k-200k events/s, so auto-size the ring when
    # the user did not pick a capacity: from --duration at an assumed worst-case
    # rate with headroom, or a generous interactive default for --wait-key.
    # 24 bytes/event: 1M = 24 MB, 4M = 96 MB, cap 8M = 192 MB.
    EVENTS_PER_SECOND = 250_000
    capacity = args.capacity
    if not capacity:
        if args.duration:
            capacity = int(args.duration * EVENTS_PER_SECOND * 1.5)
        else:
            capacity = 4 * 1024 * 1024  # interactive: unknown length
        capacity = max(1024 * 1024, min(capacity, 8 * 1024 * 1024))
        print(f"Auto-sized buffer: {capacity} events (~{capacity * 24 // (1024*1024)} MB); "
              f"override with --capacity")

    config = {"capacity": capacity}
    if args.overflow:
        config["overflow"] = args.overflow
    result = api.post(f"{base}/config", config)
    print(f"Buffer: capacity={result['capacity']} overflow={result['overflow']}")

    # 3. Filter
    if args.preset:
        result = api.post(f"{base}/filter", {"preset": args.preset})
        print(f"Filter: {result['filter']}")
    elif args.include or args.exclude:
        body = {"include": [parse_rule(r) for r in args.include],
                "exclude": [parse_rule(r) for r in args.exclude]}
        result = api.post(f"{base}/filter", body)
        print(f"Filter: {result['filter']}")

    # 4. Capture
    api.post(f"{base}/start")
    if args.wait_key:
        wait_for_keypress(lambda: api.get(f"{base}/status").get("session"))
    else:
        print(f"Capturing for {args.duration:g}s...")
        time.sleep(args.duration)
    status = api.post(f"{base}/stop")
    print(f"Captured {status['events']} events "
          f"(produced {status['total_produced']}, evicted {status['total_evicted']}, "
          f"filtered out {status['total_filtered']}"
          + (", AUTO-STOPPED: buffer full" if status.get("auto_stopped") else "") + ")")

    # 5. Canonical server-side save (JSON carries the decode-rule table)
    out_base = Path(args.output).expanduser().resolve()
    out_base.parent.mkdir(parents=True, exist_ok=True)
    canonical = out_base.with_suffix(".json")
    api.post(f"{base}/save", {"path": str(canonical), "format": "json"})
    print(f"Saved: {canonical}")

    if "bin" in formats or "binary" in formats:
        bin_path = out_base.with_suffix(".bin")
        api.post(f"{base}/save", {"path": str(bin_path), "format": "bin"})
        print(f"Saved: {bin_path}")

    if "binz" in formats:
        binz_path = out_base.with_suffix(".binz")
        api.post(f"{base}/save", {"path": str(binz_path), "format": "binz"})
        print(f"Saved: {binz_path}")

    # 6. Local conversion via porttrace_convert (same machine as the server)
    if not canonical.exists():
        print(f"NOTE: {canonical} is not visible from this machine - the WebAPI "
              f"server runs elsewhere. Copy it over and convert with "
              f"tools/porttrace/porttrace_convert.py", file=sys.stderr)
        return 0

    session, events = porttrace_convert.read_json(canonical)
    writers = {"csv": (".csv", porttrace_convert.write_csv),
               "text": (".txt", porttrace_convert.write_text),
               "txt": (".txt", porttrace_convert.write_text),
               "markdown": (".md", porttrace_convert.write_markdown),
               "md": (".md", porttrace_convert.write_markdown)}
    for fmt in formats:
        if fmt not in writers:
            continue  # json/bin already written server-side
        suffix, writer = writers[fmt]
        path = out_base.with_suffix(suffix)
        with open(path, "w", encoding="utf-8", newline="") as f:
            writer(session, events, f)
        print(f"Saved: {path}")

    if args.summary:
        print()
        porttrace_convert.write_summary(session, events, sys.stdout)

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ApiError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
