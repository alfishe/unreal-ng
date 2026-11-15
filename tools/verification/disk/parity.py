#!/usr/bin/env python3
"""Byte parity between unreal-disk (the C++ loaders, through `zxdisk`) and tools/diskconverter (pure Python).

For every image (default: every floppy fixture under testdata/: .trd .scl .fdi .udi .td0 .mb2 .mbd .d40 .d80 .dsk
.img .ima .mgt .pro and Hobeta .$x):

  dump     `zxdisk dump <image>` against `python3 -m diskconverter dump <image>` (computed in-process): the format,
           the geometry, then per track the encoding, length, raw stream, clock bitmap, weak bitmap and every
           indexed sector (C/H/R/N, IDAM/DAM/data offsets, size, flags, stored CRCs). The loader warnings are
           compared on their own (a warnings-only difference is reported as such). A refusal must be mutual, with
           the same message.
  convert  `zxdisk convert <image> out.<ext>` against the Python writer, for ext in udi fdi td0 trd scl mb2 mbd
           d40 d80 $c dsk img ima mgt pro: the output bytes (first differing offset), refusals (both refuse, same
           reason), and the warning zxdisk prints (the writer's first warning) compared on its own.

The first difference of each kind is reported: the track (c/h), the field, and for raw / clock / weak the byte
offset with both values. Exit 0 when everything matches, 1 on any difference, 2 on bad usage (zxdisk missing).

    python3 tools/verification/disk/parity.py                          # all of testdata/
    python3 tools/verification/disk/parity.py scratch/zxf-originals --json
    python3 tools/verification/disk/parity.py --zxdisk path/to/zxdisk --no-convert a.td0 b.udi

Report: scratch/disk-parity/report.json (always; --report names another file) and a text summary; --json prints the
report on stdout instead of the summary. Converted files are written to a temporary folder under
scratch/disk-parity/ and removed afterwards.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "diskconverter"))

from diskconverter import formats  # noqa: E402
from diskconverter.disk_image import DiskConversionError  # noqa: E402
from diskconverter.dump import dump  # noqa: E402

EXTENSIONS = (".trd", ".scl", ".fdi", ".udi", ".td0", ".mb2", ".mbd", ".d40", ".d80",
              ".dsk", ".img", ".ima", ".mgt", ".pro", ".hfe", ".scp")
TARGETS = ("udi", "fdi", "td0", "trd", "scl", "mb2", "mbd", "d40", "d80", "$c", "dsk", "img", "ima", "mgt", "pro", "hfe", "scp")
ZXDISK_CANDIDATES = ("cmake-build-agent-release/bin/zxdisk", "cmake-build-release/bin/zxdisk",
                     "cmake-build-debug/bin/zxdisk", "build/bin/zxdisk")
HEX_FIELDS = ("raw", "clock", "weak")
TIMEOUT = 120


def find_zxdisk(explicit: str | None) -> str | None:
    """The zxdisk binary: --zxdisk, else $ZXDISK, else the usual build folders, else PATH"""
    explicit = explicit or os.environ.get("ZXDISK")
    if explicit:
        return explicit if os.path.isfile(explicit) and os.access(explicit, os.X_OK) else None
    roots = [REPO]
    # A worktree under scratch/ has no build folder of its own: try the main checkout's too
    marker = os.sep + "scratch" + os.sep
    if marker in REPO:
        roots.append(REPO.split(marker)[0])
    for root in roots:
        for candidate in ZXDISK_CANDIDATES:
            path = os.path.join(root, candidate)
            if os.path.isfile(path) and os.access(path, os.X_OK):
                return path
    found = shutil.which("zxdisk")
    return found


def is_hobeta_name(name: str) -> bool:
    """A Hobeta file: a two-character extension starting with '$' (.$b, .$c, ...)"""
    return formats.is_hobeta_extension(formats.extension(name))


def find_images(paths: list[str]) -> list[str]:
    """Image files among paths (directories searched recursively, hidden folders skipped), sorted"""
    found = []
    for path in paths:
        if os.path.isdir(path):
            for root, dirs, files in os.walk(path):
                dirs[:] = [d for d in dirs if not d.startswith(".")]
                found += [os.path.join(root, f) for f in files
                          if f.lower().endswith(EXTENSIONS) or is_hobeta_name(f)]
        else:
            found.append(path)
    return sorted(found)


# ----------------------------------------------------------------------------------------------- comparison

def _first_hex_difference(a: str, b: str) -> dict:
    """First differing byte of two hex strings (offsets in bytes)"""
    ab, bb = bytes.fromhex(a), bytes.fromhex(b)
    n = min(len(ab), len(bb))
    for i in range(n):
        if ab[i] != bb[i]:
            return {"offset": i, "zxdisk": f"{ab[i]:02x}", "python": f"{bb[i]:02x}",
                    "zxdisk_length": len(ab), "python_length": len(bb)}
    return {"offset": n, "zxdisk": "<end>" if n == len(ab) else f"{ab[n]:02x}",
            "python": "<end>" if n == len(bb) else f"{bb[n]:02x}", "zxdisk_length": len(ab), "python_length": len(bb)}


def compare_dumps(cpp: dict, py: dict) -> dict | None:
    """The first structural difference between two dumps (warnings excluded), or None"""
    for key in ("format", "cylinders", "sides"):
        if cpp.get(key) != py.get(key):
            return {"field": key, "zxdisk": cpp.get(key), "python": py.get(key)}
    ct, pt = cpp.get("tracks", []), py.get("tracks", [])
    for i in range(max(len(ct), len(pt))):
        if i >= len(ct) or i >= len(pt):
            return {"field": "tracks", "zxdisk": len(ct), "python": len(pt)}
        a, b = ct[i], pt[i]
        where = {"c": a.get("c"), "h": a.get("h")}
        for key in ("c", "h", "encoding", "length") + HEX_FIELDS:
            if a.get(key) != b.get(key):
                diff = dict(where, field=key)
                if key in HEX_FIELDS:
                    diff.update(_first_hex_difference(a.get(key, ""), b.get(key, "")))
                else:
                    diff.update(zxdisk=a.get(key), python=b.get(key))
                return diff
        sa, sb = a.get("sectors", []), b.get("sectors", [])
        if len(sa) != len(sb):
            return dict(where, field="sector count", zxdisk=len(sa), python=len(sb))
        for s, (x, y) in enumerate(zip(sa, sb)):
            for key in x.keys() | y.keys():
                if x.get(key) != y.get(key):
                    return dict(where, field=f"sectors[{s}].{key}", r=x.get("r"), zxdisk=x.get(key),
                                python=y.get(key))
    return None


def compare_warnings(cpp: list[str], py: list[str]) -> dict | None:
    if cpp == py:
        return None
    for i in range(max(len(cpp), len(py))):
        a = cpp[i] if i < len(cpp) else None
        b = py[i] if i < len(py) else None
        if a != b:
            return {"index": i, "zxdisk": a, "python": b, "zxdisk_count": len(cpp), "python_count": len(py)}
    return None


def _first_byte_difference(a: bytes, b: bytes) -> dict:
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return {"offset": i, "zxdisk": f"{a[i]:02x}", "python": f"{b[i]:02x}", "zxdisk_size": len(a),
                    "python_size": len(b)}
    return {"offset": n, "zxdisk": "<end>" if n == len(a) else f"{a[n]:02x}",
            "python": "<end>" if n == len(b) else f"{b[n]:02x}", "zxdisk_size": len(a), "python_size": len(b)}


def _zxdisk_message(stderr: str) -> str:
    """The text after 'zxdisk: ' (or 'zxdisk: warning: ') of zxdisk's last stderr line"""
    lines = [line for line in stderr.splitlines() if line.strip()]
    if not lines:
        return ""
    line = lines[-1]
    for prefix in ("zxdisk: warning: ", "zxdisk: "):
        if line.startswith(prefix):
            return line[len(prefix):]
    return line


# ------------------------------------------------------------------------------------------------ one image

def check_image(image: str, zxdisk: str, convert: bool, work_root: str) -> dict:
    result = {"image": image, "dump": "ok", "warnings": "ok", "convert": {}}
    problems = []

    proc = subprocess.run([zxdisk, "dump", image], capture_output=True, timeout=TIMEOUT)
    cpp_ok = proc.returncode == 0
    cpp_text = proc.stdout.decode("utf-8", "replace")
    cpp_error = _zxdisk_message(proc.stderr.decode("utf-8", "replace"))
    try:
        disk = formats.load(image)
        py_text, py_error = dump(disk), ""
    except DiskConversionError as error:
        disk, py_text, py_error = None, "", str(error)
    except Exception as error:  # a crash is a difference, not the end of the run
        disk, py_text, py_error = None, "", f"Python exception: {type(error).__name__}: {error}"

    if not cpp_ok or disk is None:
        if cpp_ok != (disk is not None):
            result["dump"] = "refusal mismatch"
            result["dump_difference"] = {"zxdisk": "loaded" if cpp_ok else f"refused: {cpp_error}",
                                         "python": "loaded" if disk is not None else f"refused: {py_error}"}
            problems.append("dump")
        elif cpp_error != py_error:
            result["dump"] = "refusal message mismatch"
            result["dump_difference"] = {"zxdisk": cpp_error, "python": py_error}
            problems.append("dump")
        else:
            result["dump"] = "both refuse"
            result["refusal"] = cpp_error
        result["problems"] = problems
        return result

    cpp_dump = json.loads(cpp_text)
    py_dump = json.loads(py_text)
    difference = compare_dumps(cpp_dump, py_dump)
    if difference is not None:
        result["dump"] = "difference"
        result["dump_difference"] = difference
        problems.append("dump")
    warnings = compare_warnings(cpp_dump.get("warnings", []), py_dump.get("warnings", []))
    if warnings is not None:
        result["warnings"] = "difference"
        result["warnings_difference"] = warnings
        problems.append("warnings")
    if difference is None and warnings is None and cpp_text != py_text:
        result["dump"] = "text difference"
        result["dump_difference"] = _first_byte_difference(cpp_text.encode(), py_text.encode())
        problems.append("dump")

    if convert:
        work = tempfile.mkdtemp(prefix="conv-", dir=work_root)
        try:
            for ext in TARGETS:
                entry = check_convert(image, disk, zxdisk, ext, work)
                result["convert"][ext] = entry
                if entry["status"] not in ("ok", "both refuse"):
                    problems.append(f"convert.{ext}")
        finally:
            shutil.rmtree(work, ignore_errors=True)
    result["problems"] = problems
    return result


def check_convert(image: str, disk, zxdisk: str, ext: str, work: str) -> dict:
    out_path = os.path.join(work, "out." + ext)
    proc = subprocess.run([zxdisk, "convert", image, out_path], capture_output=True, timeout=TIMEOUT)
    cpp_ok = proc.returncode == 0
    cpp_message = _zxdisk_message(proc.stderr.decode("utf-8", "replace"))
    cpp_bytes = b""
    if cpp_ok and os.path.exists(out_path):
        with open(out_path, "rb") as f:
            cpp_bytes = f.read()
    try:
        py_bytes, py_warnings = formats.serialize(disk, out_path)
        py_ok, py_message = True, (py_warnings[0] if py_warnings else "")
    except DiskConversionError as error:
        py_bytes, py_ok, py_message = b"", False, str(error)
    except Exception as error:
        py_bytes, py_ok, py_message = b"", False, f"Python exception: {type(error).__name__}: {error}"

    entry = {"status": "ok"}
    if cpp_ok != py_ok:
        entry["status"] = "refusal mismatch"
        entry["zxdisk"] = "saved" if cpp_ok else f"refused: {cpp_message}"
        entry["python"] = "saved" if py_ok else f"refused: {py_message}"
        return entry
    if not cpp_ok:
        if cpp_message != py_message:
            entry.update(status="refusal message mismatch", zxdisk=cpp_message, python=py_message)
        else:
            entry.update(status="both refuse", reason=cpp_message)
        return entry
    if cpp_bytes != py_bytes:
        entry["status"] = "bytes differ"
        entry["difference"] = _first_byte_difference(cpp_bytes, py_bytes)
        return entry
    if cpp_message != py_message:
        entry.update(status="warning differs", zxdisk=cpp_message, python=py_message)
    return entry


# ------------------------------------------------------------------------------------------------------ main

def summarize(results: list[dict]) -> str:
    lines = []
    bad = [r for r in results if r["problems"]]
    for r in bad:
        lines.append(f"DIFF {r['image']}: {', '.join(r['problems'])}")
        if "dump_difference" in r:
            lines.append(f"     dump ({r['dump']}): {json.dumps(r['dump_difference'])}")
        if "warnings_difference" in r:
            lines.append(f"     warnings: {json.dumps(r['warnings_difference'])}")
        for ext, entry in r.get("convert", {}).items():
            if entry["status"] not in ("ok", "both refuse"):
                lines.append(f"     convert {ext}: {json.dumps(entry)}")
    refused = sum(1 for r in results if r["dump"] == "both refuse")
    conversions = sum(len(r.get("convert", {})) for r in results)
    mutual = sum(1 for r in results for e in r.get("convert", {}).values() if e["status"] == "both refuse")
    lines.append(f"{len(results)} image(s): {len(results) - len(bad)} identical, {len(bad)} with differences, "
                 f"{refused} refused by both; {conversions} conversion(s), {mutual} refused by both")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("images", nargs="*", help="images or directories (default: testdata/)")
    parser.add_argument("--zxdisk", metavar="PATH", help="the zxdisk binary (default: the agent / release build)")
    parser.add_argument("--json", action="store_true", help="print the JSON report instead of the summary")
    parser.add_argument("--no-convert", action="store_true", help="compare dumps only")
    parser.add_argument("--report", metavar="FILE", help="report path (default scratch/disk-parity/report.json)")
    parser.add_argument("-j", "--jobs", type=int, default=min(8, os.cpu_count() or 1), help="parallel images")
    args = parser.parse_args(argv)

    zxdisk = find_zxdisk(args.zxdisk)
    if zxdisk is None:
        print("error: zxdisk not found (build it with tools/build/build.sh zxdisk or pass --zxdisk)", file=sys.stderr)
        return 2
    usage = subprocess.run([zxdisk], capture_output=True, text=True, timeout=TIMEOUT)
    if "zxdisk dump" not in usage.stdout + usage.stderr:
        print(f"error: {zxdisk} has no dump verb (an older build): rebuild it with tools/build/build.sh zxdisk",
              file=sys.stderr)
        return 2
    images = find_images(args.images or [os.path.join(REPO, "testdata")])
    if not images:
        print("error: no images found", file=sys.stderr)
        return 2

    report_path = args.report or os.path.join(REPO, "scratch", "disk-parity", "report.json")
    os.makedirs(os.path.dirname(os.path.abspath(report_path)), exist_ok=True)
    scratch = os.path.join(REPO, "scratch", "disk-parity")
    os.makedirs(scratch, exist_ok=True)
    work_root = tempfile.mkdtemp(prefix="work-", dir=scratch)
    try:
        results: list[dict] = []
        with concurrent.futures.ProcessPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            futures = [pool.submit(check_image, image, zxdisk, not args.no_convert, work_root) for image in images]
            for future in futures:
                results.append(future.result())
    finally:
        shutil.rmtree(work_root, ignore_errors=True)

    report = {"zxdisk": zxdisk, "images": len(results),
              "differences": sum(1 for r in results if r["problems"]), "results": results}
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    if args.json:
        print(json.dumps(report, indent=1))
    else:
        print(summarize(results))
        print(f"report: {report_path}")
    return 1 if report["differences"] else 0


if __name__ == "__main__":
    sys.exit(main())
