#!/usr/bin/env python3
"""Check a v1-vs-engine benchmark run against the quality bar (engine decision 33).

Reads a Google Benchmark JSON file of the TTD matrix run with both engines
(UNREAL_TTD_BENCH_ENGINE=all) and prints, per configuration and workload, the
three deterministic conditions of decision 33 and the timings:

- recorded bytes per frame: v1's RAM pages + references + device blobs against
  the engine's RAM pieces + references + device blobs + device regions, less the
  regions v1 does not record at all (NeoGS RAM and flash, MoonSound wave RAM,
  the EEPROMs: bm3_device_regions_v1_lacks_bpf);
- memory per frame, the parts both engines have (v1 also holds the write and
  port journals and the coverage index, which the engine gets in Phase 3): v1's
  page store table, RAM payload and its slack, checkpoints, references, device
  blobs against the engine's piece versions, payload, reference tables,
  checkpoints, device blobs and frame table, less the payload and the version
  records of the regions v1 does not record. The engine's delta base (each
  region's latest contents) and its arena slack (at most one chunk) are fixed
  amounts, printed apart;
- counted capture work per frame: bytes walked (scanned), copied (delta base,
  device state) and compressed, the engine's work on memory v1 does not record
  left out.

A condition fails when the engine is larger than v1 (memory: by more than 2%,
the allocator tolerance). Timings are printed, not judged: they depend on the
host's load (run on an idle host, load < 12, twice).

Usage:
    python3 tools/verification/ttd-bench/ttd_engine_d33.py <run.json> [--timings]
Exit status 1 when a condition fails.
"""

import argparse
import json
import sys


def load(path):
    rows = {}
    with open(path) as f:
        for b in json.load(f)["benchmarks"]:
            if b.get("error_occurred"):
                continue
            parts = b["name"].split("/")   # TTDMatrix/<engine>/<config>/<workload>/...
            rows.setdefault(parts[2] + "/" + parts[3], {})[parts[1]] = b
    return rows


def g(b, key):
    return b.get(key, 0.0) or 0.0


def heap(b, parts):
    return sum(g(b, "bm4_heap_" + p + "_bpf") for p in parts)


def check(v, x):
    lacks = g(x, "bm3_device_regions_v1_lacks_bpf")
    bytes_v1 = g(v, "bm3_ram_payload_bpf") + g(v, "bm3_page_refs_bpf") + g(v, "bm3_device_blobs_bpf")
    bytes_engine = (g(x, "bm3_ram_payload_bpf") + g(x, "bm3_page_refs_bpf") + g(x, "bm3_device_blobs_bpf")
                    + g(x, "bm3_device_regions_bpf") - lacks)
    mem_v1 = heap(v, ["page_store_table", "ram_payload", "ram_payload_slack", "checkpoints", "page_refs",
                      "device_blobs"])
    mem_engine = (heap(x, ["piece_versions", "ram_payload", "reference_tables", "checkpoints", "device_blobs",
                           "frame_table"])
                  - lacks - g(x, "bm4_heap_piece_versions_bpf") * g(x, "bm3_versions_v1_lacks_share"))

    def work(b, exempt):
        return (g(b, "bm2_work_scanned_bpf") - exempt + g(b, "bm2_work_compress_input_bpf")
                + g(b, "bm2_work_device_state_bpf") + g(b, "bm2_work_delta_base_bpf"))

    return {
        "bytes": (bytes_v1, bytes_engine, bytes_engine <= bytes_v1 * 1.0005),
        "memory": (mem_v1, mem_engine, mem_engine <= mem_v1 * 1.02),
        "work": (work(v, 0.0), work(x, g(x, "bm2_work_scanned_v1_lacks_bpf")), None),
        "fixed": (g(x, "bm4_heap_delta_base_bpf"), g(x, "bm4_heap_arena_slack_bpf")),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("run", help="Google Benchmark JSON of a TTD matrix run with both engines")
    parser.add_argument("--timings", action="store_true", help="also print capture and memory-restore timings")
    args = parser.parse_args()

    rows = load(args.run)
    failed = 0
    header = (f"{'configuration':28s} {'bytes/frame v1 -> engine':>26s} {'memory/frame':>18s} "
              f"{'work/frame':>22s} {'fixed: base, slack':>20s}")
    if args.timings:
        header += f" {'capture p50 us':>16s} {'p99':>6s} {'restore p50 us':>16s} {'p99':>6s}"
    print(header)
    for cfg in sorted(rows):
        v, x = rows[cfg].get("v1"), rows[cfg].get("engine")
        if not v or not x:
            continue
        c = check(v, x)
        bv, be, bok = c["bytes"]
        mv, me, mok = c["memory"]
        wv, we, _ = c["work"]
        wok = we <= wv
        ok = bok and mok and wok
        failed += 0 if ok else 1
        mark = lambda good: "" if good else " X"
        line = (f"{cfg:28s} {bv:11.0f} -> {be:8.0f}{mark(bok):2s} {mv:7.0f} -> {me:6.0f}{mark(mok):2s} "
                f"{wv:9.0f} -> {we:8.0f}{mark(wok):2s} {c['fixed'][0]:9.0f} {c['fixed'][1]:6.0f}")
        if args.timings:
            line += (f" {g(v, 'bm2_capture_us_p50'):7.1f} -> {g(x, 'bm2_capture_us_p50'):5.1f} "
                     f"{g(x, 'bm2_capture_us_p99'):6.1f} {g(v, 'bm6_restore_memory_us_p50'):7.0f} -> "
                     f"{g(x, 'bm6_restore_memory_us_p50'):5.0f} {g(x, 'bm6_restore_memory_us_p99'):6.0f}")
        print(line)
    print(f"\n{len(rows)} configurations, {failed} failing a deterministic condition")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
