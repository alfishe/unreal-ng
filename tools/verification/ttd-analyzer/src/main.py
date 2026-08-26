"""Command-line entry point for the TTD analyzer.

Subcommands
-----------
``info FILE``
    Print the parsed header and a one-line-per-checkpoint summary.

``validate FILE``
    Run integrity checks only. Exits 0 if no errors, 1 otherwise.
    Suitable for CI gates.

``analyze FILE``
    Full run: integrity + anomaly + timeline table. Human-readable.

``report FILE -o report.md``
    Write the markdown report (see ``timeline_report.py``) to ``-o``.

``render FILE --frame N -o out.png``
    Render one checkpoint's screen to a PNG (requires Pillow) or PPM (no deps).

``render-all FILE -o OUTDIR``
    Render every checkpoint to ``OUTDIR/frame_NNNN.png``. Skips frames that
    have no dirty pages (would be identical to the previous render).

``heatmap FILE -o out.png``
    Render a one-pixel-per-checkpoint-wide heatmap of dirty pages.

Examples
--------
    #!/bin/sh
    ./run.sh info testdata/ttd/active_demo.ttd
    ./run.sh validate testdata/ttd/active_demo.ttd && echo "OK"
    ./run.sh analyze testdata/ttd/active_demo.ttd | less
    ./run.sh report testdata/ttd/active_demo.ttd -o report.md
    ./run.sh render testdata/ttd/active_demo.ttd --frame 100 -o cp100.png
    ./run.sh heatmap testdata/ttd/active_demo.ttd -o heatmap.png

Exit codes
----------
* 0 — success.
* 1 — integrity errors (only for ``validate``).
* 2 — file could not be parsed (bad magic, truncated, unsupported schema).
* 3 — bad CLI arguments.
* 4 — I/O error (could not write output file).
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import List, Optional

from . import __version__
from .anomaly_detector import detect_anomalies
from .framebuffer_renderer import RenderError, render_checkpoint, render_dirty_heatmap
from .integrity_check import check_integrity
from .timeline_report import generate_markdown_report, ReportOptions
from .ttd_format import (
    ENCODING_FULL,
    ENCODING_XOR_PREV,
    ENCODING_ZERO,
    NEVER_TOUCHED_SLOT,
    SUB_PAGES_PER_EMU_PAGE,
    TtdFormatError,
    parse_file,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _format_size(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n / (1024 * 1024):.2f} MB"


def _parse_dump_or_die(path: str):
    try:
        return parse_file(path)
    except TtdFormatError as e:
        print(f"error: cannot parse {path}: {e}", file=sys.stderr)
        sys.exit(2)
    except OSError as e:
        print(f"error: cannot read {path}: {e}", file=sys.stderr)
        sys.exit(4)


def _model_name(model_id: int) -> str:
    # Local copy to avoid coupling CLI to the report module's table.
    from .timeline_report import _MODEL_NAMES
    return _MODEL_NAMES.get(model_id, f"Unknown model id {model_id}")


# ---------------------------------------------------------------------------
# Subcommand implementations
# ---------------------------------------------------------------------------


def cmd_info(args: argparse.Namespace) -> int:
    dump = _parse_dump_or_die(args.file)
    h = dump.header
    print(f"=== {args.file} ===")
    print(f"file size: {_format_size(os.path.getsize(args.file))}")
    print(f"schema: v{h.schema_version}, flags=0x{h.flags:04X} "
          f"({'LE' if h.flags & 1 else 'BE'})")
    # model_ram_pages is an exclusive page-index BOUND, not a page count, so it
    # does not translate to an RAM size: a 48K machine reports 6 because its
    # three pages are numbered 0, 2 and 5.
    print(f"model: {_model_name(h.model_id)} "
          f"(RAM page bound {h.model_ram_pages}: pages 0..{h.model_ram_pages - 1})")
    print(f"cpu_state_size={h.cpu_state_size}, "
          f"chipset_state_size={h.chipset_state_size}")
    # v2: slots are variable-size and compressed. Report live payload bytes
    # (sum of compressed payloads) and the codec ratio rather than the v1
    # "slots × 16 KB" estimate, which no longer reflects reality.
    live_bytes = dump.live_payload_bytes
    raw_bytes = sum(
        4096 for s in dump.slots
        if s.encoding != ENCODING_ZERO
    )
    n_zero = sum(1 for s in dump.slots if s.encoding == ENCODING_ZERO)
    n_full = sum(1 for s in dump.slots if s.encoding == ENCODING_FULL)
    n_xor  = sum(1 for s in dump.slots if s.encoding == ENCODING_XOR_PREV)
    print(f"page_store: {h.page_store_count} live slots "
          f"({_format_size(live_bytes)} compressed payload, "
          f"{_format_size(raw_bytes)} raw → "
          f"{dump.compression_ratio * 100:.1f}% of raw)")
    print(f"  slot breakdown: {n_full} Full, {n_xor} XorPrev, {n_zero} Zero")
    # The journal is usually the largest section in the file - 71% of a demo
    # recording even after block compression - so it belongs in the summary
    # rather than hidden behind a flag bit.
    if dump.journal is not None:
        j = dump.journal
        if j.record_count:
            print(f"write journal: {j.record_count:,} records in {len(j.blocks)} blocks "
                  f"({_format_size(j.section_bytes)} on disk, "
                  f"{_format_size(j.verbatim_bytes)} verbatim -> "
                  f"{j.compression_ratio:.1f}x)")
            print(f"  {j.section_bytes / max(j.record_count, 1):.2f} B/record, "
                  f"globalT {j.blocks[0].first_global_t} … {j.blocks[-1].last_global_t}")
        else:
            print("write journal: present but empty")
    elif h.flags & 0x0002:
        print("write journal: flagged but unreadable")
    else:
        print("write journal: absent")
    cov = getattr(dump, "coverage", None)
    if cov is not None and cov.kinds:
        total = cov.compressed_bytes
        print(f"coverage index: {_format_size(total)} "
              f"({_format_size(cov.raw_bytes)} raw -> "
              f"{cov.raw_bytes / total:.1f}x)" if total else "coverage index: empty")
        for kind in cov.kinds:
            rng = kind.covered_range
            span = f"frames {rng[0]}..{rng[1]}" if rng else "no frames"
            print(f"  {kind.name:<9} {kind.frame_count:>5} frames, "
                  f"{len(kind.blocks):>3} blocks, "
                  f"{_format_size(kind.compressed_bytes):>9} ({span})")
    elif h.flags & 0x0004:
        print("coverage index: flagged but unreadable")
    else:
        print("coverage index: absent (reverse queries fall back to replay)")
    print(f"checkpoints: {h.checkpoint_count}")
    print(f"frame range: {h.session_start_frame} … {h.session_end_frame}")
    print(f"emulator_id: {h.emulator_id!r}")
    if dump.checkpoints:
        n_key = sum(1 for cp in dump.checkpoints if cp.is_keyframe)
        print(f"frame kinds:   {n_key} I-frame, "
              f"{len(dump.checkpoints) - n_key} P-frame")
        print(f"\nFirst checkpoint: frame={dump.checkpoints[0].frame}, "
              f"kind={'I' if dump.checkpoints[0].is_keyframe else 'P'}, "
              f"PC=0x{dump.checkpoints[0].cpu.pc:04X}, "
              f"SP=0x{dump.checkpoints[0].cpu.sp:04X}")
        print(f"Last checkpoint:  frame={dump.checkpoints[-1].frame}, "
              f"kind={'I' if dump.checkpoints[-1].is_keyframe else 'P'}, "
              f"PC=0x{dump.checkpoints[-1].cpu.pc:04X}, "
              f"SP=0x{dump.checkpoints[-1].cpu.sp:04X}")
    return 0


def cmd_validate(args: argparse.Namespace) -> int:
    dump = _parse_dump_or_die(args.file)
    report = check_integrity(dump)
    if report.ok and not report.warnings:
        print(f"{args.file}: OK ({len(dump.checkpoints)} checkpoints, "
              f"{dump.page_count} pages)")
        return 0
    if report.ok:
        print(f"{args.file}: OK with {len(report.warnings)} warning(s).")
        for w in report.warnings:
            print(f"  WARN [{w.code}] {w.message}")
        return 0
    print(f"{args.file}: FAIL ({len(report.errors)} error(s), "
          f"{len(report.warnings)} warning(s))")
    for issue in report.issues:
        scope = (f"cp{issue.checkpoint_index}"
                 if issue.checkpoint_index >= 0 else "file")
        print(f"  {issue.severity.upper():7} [{issue.code}] {scope}: "
              f"{issue.message}")
    return 1


def cmd_analyze(args: argparse.Namespace) -> int:
    dump = _parse_dump_or_die(args.file)
    integrity = check_integrity(dump)
    anomalies = detect_anomalies(dump)

    print(f"=== Analysis of {args.file} ===\n")

    print(f"INTEGRITY: {'OK' if integrity.ok else 'FAIL'} "
          f"({len(integrity.errors)} err, {len(integrity.warnings)} warn)")
    for issue in integrity.issues[:20]:
        scope = (f"cp{issue.checkpoint_index}"
                 if issue.checkpoint_index >= 0 else "file")
        print(f"  [{issue.severity:7}] [{issue.code}] {scope}: {issue.message}")
    if len(integrity.issues) > 20:
        print(f"  … ({len(integrity.issues) - 20} more — use 'report' for full list)")

    print(f"\nANOMALIES: {len(anomalies.critical)} critical, "
          f"{len(anomalies.warnings)} warning, "
          f"{len(anomalies.findings) - len(anomalies.critical) - len(anomalies.warnings)} info")
    for f in anomalies.findings[:20]:
        cp = f"cp{f.checkpoint_index}" if f.checkpoint_index >= 0 else "—"
        print(f"  [{f.severity:8}] [{f.category}/{f.code}] {cp}: {f.message}")
    if len(anomalies.findings) > 20:
        print(f"  … ({len(anomalies.findings) - 20} more)")

    if dump.checkpoints:
        print("\nTIMELINE:")
        print(f"  checkpoints: {len(dump.checkpoints)}")
        print(f"  first frame: {dump.checkpoints[0].frame}")
        print(f"  last frame:  {dump.checkpoints[-1].frame}")
        # Quick stats. v2: ram_sub_slots is the flat 4×model_ram_pages list;
        # report per-checkpoint dirty sub-slot count.
        dirty_counts = [
            sum(1 for r in cp.ram_sub_slots if r != NEVER_TOUCHED_SLOT)
            for cp in dump.checkpoints
        ]
        if dirty_counts:
            print(f"  dirty sub-slots/checkpoint: "
                  f"min={min(dirty_counts)}, max={max(dirty_counts)}, "
                  f"avg={sum(dirty_counts) / len(dirty_counts):.1f} "
                  f"(of {dump.header.model_ram_pages * SUB_PAGES_PER_EMU_PAGE} possible)")
        n_key = sum(1 for cp in dump.checkpoints if cp.is_keyframe)
        print(f"  I-frames: {n_key} / {len(dump.checkpoints)} "
              f"({n_key * 100 / max(1, len(dump.checkpoints)):.1f}%)")

    return 0


def cmd_report(args: argparse.Namespace) -> int:
    dump = _parse_dump_or_die(args.file)
    opts = ReportOptions(
        include_checkpoint_table=True,
        checkpoint_table_limit=args.table_limit,
        include_anomaly_details=True,
        include_integrity_details=True,
    )
    md = generate_markdown_report(dump, opts=opts)
    try:
        if args.output:
            with open(args.output, "w", encoding="utf-8") as f:
                f.write(md)
            print(f"wrote markdown report to {args.output} "
                  f"({len(md)} bytes)", file=sys.stderr)
        else:
            sys.stdout.write(md)
            if not md.endswith("\n"):
                sys.stdout.write("\n")
    except OSError as e:
        print(f"error: cannot write report: {e}", file=sys.stderr)
        return 4
    return 0


def _resolve_checkpoint(dump, frame: Optional[int], index: Optional[int]):
    """Resolve a checkpoint by frame number or index. Errors out if neither
    or both are given, or if the requested value is out of range."""
    if frame is None and index is None:
        print("error: --frame or --index is required", file=sys.stderr)
        sys.exit(3)
    if frame is not None and index is not None:
        print("error: --frame and --index are mutually exclusive",
              file=sys.stderr)
        sys.exit(3)

    if index is not None:
        if index < 0 or index >= len(dump.checkpoints):
            print(f"error: checkpoint index {index} out of range "
                  f"(0..{len(dump.checkpoints) - 1})", file=sys.stderr)
            sys.exit(3)
        return dump.checkpoints[index]

    # frame: find exact match or nearest-before
    matches = [cp for cp in dump.checkpoints if cp.frame == frame]
    if matches:
        return matches[0]
    # Find nearest-before
    nearest = None
    for cp in dump.checkpoints:
        if cp.frame <= frame:
            nearest = cp
        else:
            break
    if nearest is None:
        print(f"error: no checkpoint at or before frame {frame} "
              f"(first frame is {dump.checkpoints[0].frame})", file=sys.stderr)
        sys.exit(3)
    if nearest.frame != frame:
        print(f"note: exact frame {frame} not found; using nearest-before "
              f"frame {nearest.frame} (cp index {nearest.index})",
              file=sys.stderr)
    return nearest


def cmd_render(args: argparse.Namespace) -> int:
    dump = _parse_dump_or_die(args.file)
    cp = _resolve_checkpoint(dump, args.frame, args.index)
    try:
        render_checkpoint(
            dump, cp, args.output,
            border_px=args.border,
            fmt=args.format,
        )
    except RenderError as e:
        print(f"error: {e}", file=sys.stderr)
        return 4
    print(f"rendered cp{cp.index} (frame {cp.frame}) to {args.output}",
          file=sys.stderr)
    return 0


def cmd_render_all(args: argparse.Namespace) -> int:
    dump = _parse_dump_or_die(args.file)
    if not dump.checkpoints:
        print("error: no checkpoints to render", file=sys.stderr)
        return 1
    try:
        os.makedirs(args.outdir, exist_ok=True)
    except OSError as e:
        print(f"error: cannot create {args.outdir}: {e}", file=sys.stderr)
        return 4

    ext = "png" if args.format in ("auto", "png") else "ppm"
    skipped = 0
    written = 0
    prev_ram_hash = None
    for cp in dump.checkpoints:
        # Skip checkpoints whose materialized RAM matches the previous —
        # they would render identically and just bloat the output.
        try:
            ram = dump.materialize_ram(cp)
        except Exception as e:
            print(f"warn: cp{cp.index}: cannot materialize RAM: {e}",
                  file=sys.stderr)
            continue
        import hashlib
        h = hashlib.sha256(ram).hexdigest()
        if args.skip_identical and h == prev_ram_hash:
            skipped += 1
            continue
        prev_ram_hash = h

        try:
            from .framebuffer_renderer import decode_screen_rgb
            img = decode_screen_rgb(cp, ram, border_px=args.border)
            out = os.path.join(args.outdir,
                               f"frame_{cp.frame:08d}.{ext}")
            if ext == "png":
                img.write_png(out)
            else:
                img.write_ppm(out)
            written += 1
        except RenderError as e:
            print(f"warn: cp{cp.index} frame {cp.frame}: {e}",
                  file=sys.stderr)

    print(f"wrote {written} frames to {args.outdir}/ "
          f"({skipped} identical frames skipped)", file=sys.stderr)
    return 0


def cmd_heatmap(args: argparse.Namespace) -> int:
    dump = _parse_dump_or_die(args.file)
    try:
        render_dirty_heatmap(dump, args.output, fmt=args.format)
    except RenderError as e:
        print(f"error: {e}", file=sys.stderr)
        return 4
    print(f"wrote dirty-sub-page heatmap "
          f"({len(dump.checkpoints)} × "
          f"{dump.header.model_ram_pages * SUB_PAGES_PER_EMU_PAGE} px) "
          f"to {args.output}", file=sys.stderr)
    return 0


# ---------------------------------------------------------------------------
# Argument parser
# ---------------------------------------------------------------------------


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="ttd-analyzer",
        description="Inspector and analyzer for Unreal-NG .ttd recordings.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--version", action="version",
                   version=f"ttd-analyzer {__version__}")
    sub = p.add_subparsers(dest="cmd", required=True, metavar="<command>")

    # info
    sp = sub.add_parser("info", help="Print header + checkpoint overview.")
    sp.add_argument("file", help="Path to .ttd file")
    sp.set_defaults(func=cmd_info)

    # validate
    sp = sub.add_parser("validate", help="Run integrity checks (CI-friendly).")
    sp.add_argument("file", help="Path to .ttd file")
    sp.set_defaults(func=cmd_validate)

    # analyze
    sp = sub.add_parser("analyze",
                        help="Run integrity + anomaly + summary to stdout.")
    sp.add_argument("file", help="Path to .ttd file")
    sp.set_defaults(func=cmd_analyze)

    # report
    sp = sub.add_parser("report",
                        help="Write a markdown report.")
    sp.add_argument("file", help="Path to .ttd file")
    sp.add_argument("-o", "--output", default=None,
                    help="Output .md path (default: stdout)")
    sp.add_argument("--table-limit", type=int, default=50,
                    help="Max checkpoints in timeline table (default 50)")
    sp.set_defaults(func=cmd_report)

    # render
    sp = sub.add_parser("render",
                        help="Render one checkpoint's screen to an image.")
    sp.add_argument("file", help="Path to .ttd file")
    grp = sp.add_mutually_exclusive_group(required=True)
    grp.add_argument("--frame", type=int, help="Frame number to render")
    grp.add_argument("--index", type=int, help="Checkpoint index to render")
    sp.add_argument("-o", "--output", required=True, help="Output image path")
    sp.add_argument("--border", type=int, default=32,
                    help="Border thickness in pixels (default 32, 0=none)")
    sp.add_argument("--format", choices=("auto", "png", "ppm"), default="auto",
                    help="Output format (auto: by extension)")
    sp.set_defaults(func=cmd_render)

    # render-all
    sp = sub.add_parser("render-all",
                        help="Render every checkpoint to OUTDIR/frame_NNNN.png.")
    sp.add_argument("file", help="Path to .ttd file")
    sp.add_argument("-o", "--outdir", required=True, help="Output directory")
    sp.add_argument("--border", type=int, default=32,
                    help="Border thickness in pixels (default 32)")
    sp.add_argument("--format", choices=("auto", "png", "ppm"), default="auto",
                    help="Output format")
    sp.add_argument("--skip-identical", action="store_true", default=True,
                    help="Skip frames whose RAM is identical to the previous "
                         "(default: enabled)")
    sp.add_argument("--include-identical", dest="skip_identical",
                    action="store_false",
                    help="Render every frame, even if identical to the previous")
    sp.set_defaults(func=cmd_render_all)

    # heatmap
    sp = sub.add_parser("heatmap",
                        help="Render a one-pixel-per-checkpoint heatmap.")
    sp.add_argument("file", help="Path to .ttd file")
    sp.add_argument("-o", "--output", required=True, help="Output image path")
    sp.add_argument("--format", choices=("auto", "png", "ppm"), default="auto",
                    help="Output format (auto: by extension)")
    sp.set_defaults(func=cmd_heatmap)

    return p


def main(argv: Optional[List[str]] = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
