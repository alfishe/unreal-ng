#!/usr/bin/env python3
"""Validate the .tzx fixtures through the emulator's own tape loader (WebAPI).

Requires a running emulator with WebAPI on localhost:8090 (the Qt app or the
automation build): every fixture in testdata/loaders/tzx/ is loaded through
POST /emulator/:id/tape/load and the resulting block catalog + fast-load
verdict are printed from GET /emulator/:id/tape. The emulator's loader is the
authoritative parser — this is the final gate after tzx-blockscan's fast
structural pre-vet.

Usage: python3 tools/verification/tape/tzx/tzx-fixture-verify.py [--model 128k]
Exit status: 0 = every fixture loaded and produced a block catalog;
             1 = any load failure or empty catalog; 2 = environment error
             (no fixtures found, WebAPI unreachable, no emulator id).
"""
import glob
import json
import os
import sys
import urllib.error
import urllib.request

BASE = "http://localhost:8090/api/v1"
_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))


def call(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        return {"http_error": e.code, "detail": e.read().decode()[:200]}
    except urllib.error.URLError as e:
        return {"http_error": "unreachable", "detail": str(e.reason)}


def main():
    model = "128k"
    args = sys.argv[1:]
    if "--model" in args:
        model = args[args.index("--model") + 1]

    root = os.path.join(_REPO_ROOT, "testdata", "loaders", "tzx")
    files = sorted(glob.glob(os.path.join(root, "*.tzx")))
    if not files:
        print(f"no .tzx files found under {root}")
        return 2

    start = call("POST", "/emulator/start", {"model": model})
    emu = start.get("id")
    if not emu:
        print("could not create emulator (is WebAPI running on :8090?):", start)
        return 2
    print(f"emulator: {emu} (model {model})")

    ok = True
    try:
        for path in files:
            name = os.path.basename(path)
            res = call("POST", f"/emulator/{emu}/tape/load", {"path": path})
            if "http_error" in res:
                print(f"{name}: LOAD FAILED {res}")
                ok = False
                continue
            info = call("GET", f"/emulator/{emu}/tape")
            blocks = info.get("blocks")
            verdict = (info.get("fast_load") or {}).get("verdict")
            if not isinstance(blocks, list) or not blocks:
                print(f"{name}: no block catalog {info}")
                ok = False
                continue
            print(f"{name}: blocks={len(blocks)} verdict={verdict}")
            for b in blocks[:3]:
                print(f"    [{b.get('index')}] {b.get('kind')} {b.get('type', '')} "
                      f"len={b.get('raw_size')} {str(b.get('name', ''))[:20]}")
    finally:
        try:
            call("POST", f"/emulator/{emu}/stop")
        except Exception:
            pass
        print("done ok" if ok else "done WITH FAILURES")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
