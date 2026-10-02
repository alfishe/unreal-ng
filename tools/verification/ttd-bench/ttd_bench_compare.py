#!/usr/bin/env python3
"""Compare, summarize and export TTD benchmark matrix runs (PLAN #40 Phase 0, Step 2).

Input is the Google Benchmark JSON that core-benchmarks writes for the
TTDMatrix/<engine>/<configuration>/<workload> benchmarks
(core/benchmarks/debugger/ttd/ttd_matrix_benchmark.cpp).

    compare BASE.json NEW.json   byte metrics must match exactly, timings are
                                 reported as a change in percent
    summary RUN.json             one table of the key metrics per case
    export-gate RUN.json OUT     the byte baseline the core-tests CI gate reads
                                 (testdata/ttd/bench/v1-ci-gate.txt)
    export-baseline RUN.json OUT a run as a stored baseline (testdata/ttd/bench/):
                                 the TTDMatrix entries, no local paths

Cases are matched by <configuration>/<workload>, so two engines compare
directly: pick them with --base-engine / --new-engine when a file holds more
than one. Exit codes: 0 = no byte difference (and, with --fail-on-time, no
timing past the threshold), 1 = a difference, 2 = bad input.
"""

import argparse
import json
import re
import sys

NAME_RE = re.compile(r"^TTDMatrix/(?P<engine>[^/]+)/(?P<case>.+?)(?:/iterations:\d+)?$")

# Keys Google Benchmark adds to every entry; everything else is a TTD metric
BENCHMARK_KEYS = {
    "name", "family_index", "per_family_instance_index", "run_name", "run_type",
    "repetitions", "repetition_index", "threads", "iterations", "real_time",
    "cpu_time", "time_unit", "error_occurred", "error_message", "aggregate_name",
    "aggregate_unit",
}

# Byte metrics measured as container capacity: they follow the allocator's
# growth policy (libc++ and libstdc++ double, MSVC grows by half), so they are
# exact on one platform but not across platforms
HEAP_METRICS = {"bm3_coverage_bpf", "bm3_total_bpf", "bm4_resident_bytes", "bm4_resident_bpf"}
# BM-4 split (bm4_heap_<part>_bpf): allocator-dependent parts, compared with the
# heap tolerance; the CI gate keeps only their total (bm4_resident_*)
HEAP_SPLIT_PREFIX = "bm4_heap_"


def is_heap_metric(name: str) -> bool:
    return name in HEAP_METRICS or name.startswith(HEAP_SPLIT_PREFIX)

# Timings where larger is better (none today); everything else: larger = worse
HIGHER_IS_BETTER = set()


def is_byte_metric(name):
    """Mirror of ttd::bench::IsByteMetric (ttdbench.cpp)"""
    return (name.endswith("_bytes") or name.endswith("_bpf") or name.endswith("_opf")
            or name in ("frames", "checkpoints"))


def load(path, engine=None):
    """Returns ({case: {metric: value}}, {case: error}, context, engine)"""
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as exc:
        sys.exit(f"error: cannot read {path}: {exc}")

    by_engine = {}
    for bench in data.get("benchmarks", []):
        match = NAME_RE.match(bench.get("name", ""))
        if not match:
            continue
        cases = by_engine.setdefault(match["engine"], ({}, {}))
        if bench.get("error_occurred"):
            cases[1][match["case"]] = bench.get("error_message", "error")
            continue
        cases[0][match["case"]] = {k: float(v) for k, v in bench.items()
                                   if k not in BENCHMARK_KEYS and isinstance(v, (int, float))}

    if not by_engine:
        sys.exit(f"error: {path} holds no TTDMatrix benchmarks")
    if engine is None:
        if len(by_engine) > 1:
            sys.exit(f"error: {path} holds engines {sorted(by_engine)}; pick one with --base-engine/--new-engine")
        engine = next(iter(by_engine))
    if engine not in by_engine:
        sys.exit(f"error: {path} has no engine '{engine}' (has {sorted(by_engine)})")
    metrics, errors = by_engine[engine]
    return metrics, errors, data.get("context", {}), engine


def fmt(value):
    if value == int(value) and abs(value) < 1e15:
        return str(int(value))
    return f"{value:.2f}" if abs(value) >= 1 else f"{value:.4f}"


def same(a, b):
    return abs(a - b) <= 1e-9 * max(1.0, abs(a), abs(b))


def describe(context, engine):
    return (f"engine {engine}, set {context.get('ttd_bench_set', '?')}, "
            f"{context.get('git_branch', '?')}@{context.get('git_commit', '?')[:10]}, "
            f"{context.get('unreal_build_type', '?')}, {context.get('host_name', '?')}")


def cmd_compare(args):
    base, base_errors, base_ctx, base_engine = load(args.base, args.base_engine)
    new, new_errors, new_ctx, new_engine = load(args.new, args.new_engine)
    pattern = re.compile(args.filter) if args.filter else None

    print(f"base: {args.base} ({describe(base_ctx, base_engine)})")
    print(f"new:  {args.new} ({describe(new_ctx, new_engine)})")
    print(f"timing threshold: {args.threshold:.0f}%  heap tolerance: {args.heap_tolerance:.0f}%\n")

    byte_diffs = 0
    time_regressions = 0
    problems = []

    for case in sorted(set(base) | set(new) | set(base_errors) | set(new_errors)):
        if pattern and not pattern.search(case):
            continue
        if case in base_errors or case in new_errors:
            problems.append(f"{case}: failed ({base_errors.get(case) or new_errors.get(case)})")
            continue
        if case not in base or case not in new:
            problems.append(f"{case}: only in {'base' if case in base else 'new'}")
            continue

        rows = []
        b, n = base[case], new[case]
        for metric in sorted(set(b) | set(n)):
            if metric not in b or metric not in n:
                rows.append((metric, b.get(metric), n.get(metric), "", "MISSING"))
                continue
            bv, nv = b[metric], n[metric]
            change = (nv / bv - 1.0) * 100.0 if bv else (0.0 if nv == 0 else float("inf"))
            flag = ""
            if is_byte_metric(metric):
                tolerance = args.heap_tolerance if is_heap_metric(metric) else 0.0
                if not same(bv, nv) and abs(change) > tolerance:
                    flag = "BYTES"
                    byte_diffs += 1
            elif "_us" in metric or "_ms" in metric or metric.endswith("_pct") or "s_per_gb" in metric:
                worse = -change if metric in HIGHER_IS_BETTER else change
                if worse > args.threshold:
                    flag = "slower"
                    time_regressions += 1
                elif worse < -args.threshold:
                    flag = "faster"
            if flag or args.all:
                rows.append((metric, bv, nv, f"{change:+.1f}%", flag))

        if rows:
            print(case)
            width = max(len(r[0]) for r in rows)
            for metric, bv, nv, change, flag in rows:
                bs = fmt(bv) if bv is not None else "-"
                ns = fmt(nv) if nv is not None else "-"
                print(f"  {metric:<{width}}  {bs:>14}  {ns:>14}  {change:>9}  {flag}")
            print()

    for p in problems:
        print(f"! {p}")
    print(f"byte differences: {byte_diffs}   timing past {args.threshold:.0f}%: {time_regressions}   "
          f"case problems: {len(problems)}")

    failed = byte_diffs or problems or (args.fail_on_time and time_regressions)
    return 1 if failed else 0


SUMMARY_COLUMNS = [
    ("bm2 p50 us", "bm2_capture_us_p50"),
    ("bm2 p99 us", "bm2_capture_us_p99"),
    ("bm3 B/fr", "bm3_total_bpf"),
    ("ram B/fr", "bm3_ram_payload_bpf"),
    ("bm4 MB", "bm4_resident_bytes"),
    ("seek p99 ms", "bm5_offset_nopresent_us_p99"),
    ("+present p99", "bm5_offset_us_p99"),
    ("replay p99", "bm5_offset_replay_us_p99"),
    ("restore p99", "bm5_offset_restore_us_p99"),
    ("bm1 cov %", "bm1_overhead_journal_cov_pct"),
    ("turbo", "turbo_ratio"),
]


def cmd_summary(args):
    metrics, errors, context, engine = load(args.run, args.engine)
    print(f"{args.run} ({describe(context, engine)})\n")
    header = ["case"] + [c[0] for c in SUMMARY_COLUMNS]
    rows = []
    for case in sorted(metrics):
        m = metrics[case]
        row = [case]
        for _, key in SUMMARY_COLUMNS:
            if key not in m:
                row.append("-")
            elif key == "bm4_resident_bytes":
                row.append(f"{m[key] / 1e6:.1f}")
            elif key.startswith("bm5_") and key.endswith("_us_p99"):
                row.append(f"{m[key] / 1000:.2f}")
            else:
                row.append(fmt(round(m[key], 1)))
        rows.append(row)
    for case, error in sorted(errors.items()):
        rows.append([case, f"FAILED: {error}"] + [""] * (len(header) - 2))

    if args.markdown:
        print("| " + " | ".join(header) + " |")
        print("|" + "|".join("---" for _ in header) + "|")
        for row in rows:
            print("| " + " | ".join(row) + " |")
    else:
        widths = [max(len(str(r[i])) for r in rows + [header]) for i in range(len(header))]
        for row in [header] + rows:
            print("  ".join(str(v).rjust(w) if i else str(v).ljust(w) for i, (v, w) in enumerate(zip(row, widths))))
    return 1 if errors else 0


def cmd_export_gate(args):
    metrics, errors, context, engine = load(args.run, args.engine)
    if errors:
        sys.exit(f"error: failed cases in {args.run}: {sorted(errors)}")
    lines = [
        "# TTD CI gate baseline (PLAN #40 Phase 0, Step 2, requirements BR-7 / BR-8 / BR-9).",
        "# Read by core/tests/debugger/ttd/bench/ttdbench_test.cpp.",
        f"# Exported by tools/verification/ttd-bench/ttd_bench_compare.py from a run of",
        f"# {describe(context, engine)}.",
        "# Deterministic metrics only (bytes, bytes and operations per frame): timings do not",
        "# belong in a stored baseline.",
        "# Columns: <configuration>/<workload> <metric> <value> <tolerance percent>",
        "# Tolerance 0 = exact; heap metrics follow the allocator's growth policy.",
    ]
    for case in sorted(metrics):
        for metric in sorted(metrics[case]):
            if not is_byte_metric(metric) or metric.startswith(HEAP_SPLIT_PREFIX):
                continue
            tolerance = args.heap_tolerance if is_heap_metric(metric) else 0.0
            lines.append(f"{case} {metric} {metrics[case][metric]:.6f} {tolerance:g}")
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {args.out}: {sum(1 for l in lines if not l.startswith('#'))} metrics from {len(metrics)} cases")
    return 0


def cmd_export_baseline(args):
    try:
        with open(args.run, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as exc:
        sys.exit(f"error: cannot read {args.run}: {exc}")
    context = data.get("context", {})
    context.pop("executable", None)  # an absolute path on the machine that ran it
    benchmarks = [b for b in data.get("benchmarks", []) if NAME_RE.match(b.get("name", ""))]
    if not benchmarks:
        sys.exit(f"error: {args.run} holds no TTDMatrix benchmarks")
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"context": context, "benchmarks": benchmarks}, f, indent=1)
        f.write("\n")
    print(f"wrote {args.out}: {len(benchmarks)} cases")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("compare", help="compare two runs")
    p.add_argument("base")
    p.add_argument("new")
    p.add_argument("--base-engine")
    p.add_argument("--new-engine")
    p.add_argument("--filter", help="regex on <configuration>/<workload>")
    p.add_argument("--threshold", type=float, default=25.0, help="timing change reported, percent (default 25)")
    p.add_argument("--heap-tolerance", type=float, default=0.0,
                   help="allowed change of capacity-based byte metrics, percent (default 0; "
                        "use ~25 across platforms)")
    p.add_argument("--fail-on-time", action="store_true", help="timings past the threshold fail the run")
    p.add_argument("--all", action="store_true", help="print every metric, not only flagged ones")
    p.set_defaults(func=cmd_compare)

    p = sub.add_parser("summary", help="key metrics of one run")
    p.add_argument("run")
    p.add_argument("--engine")
    p.add_argument("--markdown", action="store_true")
    p.set_defaults(func=cmd_summary)

    p = sub.add_parser("export-gate", help="write the core-tests gate baseline")
    p.add_argument("run")
    p.add_argument("out")
    p.add_argument("--engine")
    p.add_argument("--heap-tolerance", type=float, default=25.0)
    p.set_defaults(func=cmd_export_gate)

    p = sub.add_parser("export-baseline", help="store a run as a baseline (no local paths)")
    p.add_argument("run")
    p.add_argument("out")
    p.set_defaults(func=cmd_export_baseline)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
