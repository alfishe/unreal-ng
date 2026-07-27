"""Structural integrity checks for parsed .ttd dumps.

These verify that the *file format itself* is sound: every page reference
resolves, the timeline is monotonically increasing, peripheral blob sizes
are within expected bounds, and the page store is the right size.

If a dump fails integrity checks, the analyzer output is unreliable — the
file is corrupt or was produced by a buggy writer. Report and stop.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List

from .ttd_format import Checkpoint, TtdDump, NEVER_TOUCHED_PAGE_REF, PAGE_SIZE


@dataclass
class Issue:
    severity: str  # "error" | "warning" | "info"
    code: str      # short stable identifier, e.g. "dangling_page_ref"
    message: str
    checkpoint_index: int = -1  # -1 = file-level


@dataclass
class IntegrityReport:
    issues: List[Issue] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not any(i.severity == "error" for i in self.issues)

    @property
    def errors(self) -> List[Issue]:
        return [i for i in self.issues if i.severity == "error"]

    @property
    def warnings(self) -> List[Issue]:
        return [i for i in self.issues if i.severity == "warning"]


def check_integrity(dump: TtdDump) -> IntegrityReport:
    """Run all structural checks. Returns a report; .ok tells you if it passed."""
    rep = IntegrityReport()
    h = dump.header

    # ---- Page store sanity ----
    actual_page_bytes = len(dump.page_store)
    expected_page_bytes = h.page_store_count * PAGE_SIZE
    if actual_page_bytes != expected_page_bytes:
        rep.issues.append(Issue(
            severity="error",
            code="page_store_size_mismatch",
            message=(
                f"page store is {actual_page_bytes} bytes, header claims "
                f"{h.page_store_count} pages * {PAGE_SIZE} = {expected_page_bytes}"
            ),
        ))

    # ---- Checkpoint count sanity ----
    if len(dump.checkpoints) != h.checkpoint_count:
        rep.issues.append(Issue(
            severity="error",
            code="checkpoint_count_mismatch",
            message=(
                f"parsed {len(dump.checkpoints)} checkpoints, header claims "
                f"{h.checkpoint_count}"
            ),
        ))

    # ---- Per-checkpoint page reference checks ----
    page_count = dump.page_count
    for cp in dump.checkpoints:
        # Each checkpoint should have exactly model_ram_pages refs
        if len(cp.ram_page_refs) != h.model_ram_pages:
            rep.issues.append(Issue(
                severity="error",
                code="wrong_ram_page_count",
                message=(
                    f"checkpoint {cp.index} has {len(cp.ram_page_refs)} "
                    f"ram_page_refs, expected {h.model_ram_pages}"
                ),
                checkpoint_index=cp.index,
            ))

        # Every non-NEVER_TOUCHED ref must point at a valid slot
        for page_idx, ref in enumerate(cp.ram_page_refs):
            if ref == NEVER_TOUCHED_PAGE_REF:
                continue
            if ref >= page_count:
                rep.issues.append(Issue(
                    severity="error",
                    code="dangling_page_ref",
                    message=(
                        f"checkpoint {cp.index} page {page_idx} references "
                        f"slot {ref}, but page store has only {page_count} slots"
                    ),
                    checkpoint_index=cp.index,
                ))

        # Peripheral blob size sanity (peripheral blobs are tiny)
        for blob_name, blob, max_reasonable in (
            ("ay",    cp.ay_blob,    4096),
            ("fdc",   cp.fdc_blob,   4096),
            ("tape",  cp.tape_blob,  4096),
            ("covox", cp.covox_blob, 16),
        ):
            if len(blob) > max_reasonable:
                rep.issues.append(Issue(
                    severity="warning",
                    code="oversize_peripheral_blob",
                    message=(
                        f"checkpoint {cp.index} {blob_name} blob is "
                        f"{len(blob)} bytes (>{max_reasonable} expected)"
                    ),
                    checkpoint_index=cp.index,
                ))

    # ---- Timeline monotonicity ----
    # The timeline MUST be sorted by frame (strictly increasing). If it isn't,
    # the engine's binary-search SeekTo would return wrong results.
    last_frame = -1
    for cp in dump.checkpoints:
        if cp.frame <= last_frame:
            rep.issues.append(Issue(
                severity="error",
                code="timeline_not_monotonic",
                message=(
                    f"checkpoint {cp.index} frame {cp.frame} is not strictly "
                    f"greater than previous frame {last_frame}"
                ),
                checkpoint_index=cp.index,
            ))
        last_frame = cp.frame

    # ---- Frame range matches header ----
    if dump.checkpoints:
        first = dump.checkpoints[0].frame
        last = dump.checkpoints[-1].frame
        if first != h.session_start_frame:
            rep.issues.append(Issue(
                severity="warning",
                code="start_frame_mismatch",
                message=(
                    f"first checkpoint frame={first}, header session_start_frame="
                    f"{h.session_start_frame}"
                ),
            ))
        if last != h.session_end_frame:
            rep.issues.append(Issue(
                severity="warning",
                code="end_frame_mismatch",
                message=(
                    f"last checkpoint frame={last}, header session_end_frame="
                    f"{h.session_end_frame}"
                ),
            ))

    return rep
