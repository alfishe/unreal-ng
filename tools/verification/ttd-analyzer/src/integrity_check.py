"""Structural integrity checks for parsed .ttd dumps (v2).

These verify that the *file format itself* is sound: every page reference
resolves, the timeline is monotonically increasing, peripheral blob sizes
are within expected bounds, the codec page store is internally consistent,
and every CRC32C recomputes correctly.

If a dump fails integrity checks, the analyzer output is unreliable — the
file is corrupt or was produced by a buggy writer. Report and stop.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List

from .ttd_format import (
    Checkpoint,
    PageSlot,
    TtdDump,
    ENCODING_FULL,
    ENCODING_XOR_PREV,
    ENCODING_ZERO,
    FRAME_KIND_KEY_FRAME,
    FRAME_KIND_DELTA_FRAME,
    NEVER_TOUCHED_SLOT,
    NEVER_TOUCHED_PAGE_REF,  # backward-compat alias
    SUB_PAGES_PER_EMU_PAGE,
)


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

    # ---- Page store count sanity (v2: variable-size slots, no byte-size check) ----
    actual_slot_count = len(dump.slots)
    if actual_slot_count != h.page_store_count:
        rep.issues.append(Issue(
            severity="error",
            code="page_store_count_mismatch",
            message=(
                f"page store has {actual_slot_count} slots, header claims "
                f"{h.page_store_count}"
            ),
        ))

    # ---- Per-slot codec sanity (cross-field rules + chain topology) ----
    for slot in dump.slots:
        _check_slot(rep, slot)

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

    # ---- Per-checkpoint checks ----
    expected_refs = h.model_ram_pages * SUB_PAGES_PER_EMU_PAGE
    slot_count = len(dump.slots)
    for cp in dump.checkpoints:
        # Each checkpoint must have exactly 4 * model_ram_pages sub-slot refs.
        if len(cp.ram_sub_slots) != expected_refs:
            rep.issues.append(Issue(
                severity="error",
                code="wrong_ram_sub_slot_count",
                message=(
                    f"checkpoint {cp.index} has {len(cp.ram_sub_slots)} "
                    f"ram_sub_slots, expected {expected_refs} "
                    f"({h.model_ram_pages} pages × {SUB_PAGES_PER_EMU_PAGE} sub)"
                ),
                checkpoint_index=cp.index,
            ))

        # Every non-NEVER_TOUCHED ref must point at a valid slot index.
        for i, ref in enumerate(cp.ram_sub_slots):
            page_idx = i // SUB_PAGES_PER_EMU_PAGE
            sub_idx = i % SUB_PAGES_PER_EMU_PAGE
            if ref == NEVER_TOUCHED_SLOT:
                continue
            if ref >= slot_count:
                rep.issues.append(Issue(
                    severity="error",
                    code="dangling_sub_slot_ref",
                    message=(
                        f"checkpoint {cp.index} page {page_idx} sub {sub_idx} "
                        f"references slot {ref}, but page store has only "
                        f"{slot_count} slots"
                    ),
                    checkpoint_index=cp.index,
                ))

        # Frame-kind consistency with the timeline.
        if cp.index == 0 and cp.frame_kind != FRAME_KIND_KEY_FRAME:
            rep.issues.append(Issue(
                severity="error",
                code="first_checkpoint_not_keyframe",
                message=(
                    f"checkpoint 0 (baseline) must be a KeyFrame; got "
                    f"frame_kind={cp.frame_kind}"
                ),
                checkpoint_index=cp.index,
            ))
        if cp.frame_kind == FRAME_KIND_KEY_FRAME and cp.keyframe_anchor != cp.frame:
            rep.issues.append(Issue(
                severity="error",
                code="keyframe_anchor_mismatch",
                message=(
                    f"KeyFrame checkpoint {cp.index} (frame={cp.frame}) must "
                    f"have keyframe_anchor == frame; got {cp.keyframe_anchor}"
                ),
                checkpoint_index=cp.index,
            ))

        # Peripheral blob size sanity (peripheral blobs are tiny).
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

    # ---- Optional: CRC verification (only if no errors so far) ----
    # Decompression + CRC recompute is expensive (walks every XorPrev chain);
    # we skip it if structural checks already failed since results would be
    # unreliable. The integrity surface is still meaningful because each
    # decompression already validates the zstd framing.
    if rep.ok:
        _verify_crcs(rep, dump)

    _check_journal(dump, rep)

    return rep


def _check_slot(rep: IntegrityReport, slot: PageSlot) -> None:
    """Per-slot cross-field consistency rules. Cheap; runs on every slot."""
    # Encoding-specific payload rules (parser enforces these too — this is
    # the analyzer's defense-in-depth check).
    if slot.encoding == ENCODING_ZERO:
        if slot.payload:
            rep.issues.append(Issue(
                severity="error",
                code="zero_slot_has_payload",
                message=(
                    f"slot {slot.index} is Zero encoding but has "
                    f"{len(slot.payload)} bytes of payload (must be empty)"
                ),
            ))
        if slot.prev_slot != NEVER_TOUCHED_SLOT:
            rep.issues.append(Issue(
                severity="warning",
                code="zero_slot_has_prev",
                message=(
                    f"slot {slot.index} is Zero encoding but prev_slot="
                    f"{slot.prev_slot} (should be NEVER_TOUCHED)"
                ),
            ))
    elif slot.encoding == ENCODING_FULL:
        if not slot.payload:
            rep.issues.append(Issue(
                severity="error",
                code="full_slot_empty_payload",
                message=(
                    f"slot {slot.index} is Full encoding but payload is empty"
                ),
            ))
        if slot.prev_slot != NEVER_TOUCHED_SLOT:
            rep.issues.append(Issue(
                severity="warning",
                code="full_slot_has_prev",
                message=(
                    f"slot {slot.index} is Full encoding but prev_slot="
                    f"{slot.prev_slot} (should be NEVER_TOUCHED)"
                ),
            ))
    elif slot.encoding == ENCODING_XOR_PREV:
        if not slot.payload:
            rep.issues.append(Issue(
                severity="error",
                code="xorprev_slot_empty_payload",
                message=(
                    f"slot {slot.index} is XorPrev encoding but payload is empty"
                ),
            ))
        if slot.prev_slot == NEVER_TOUCHED_SLOT:
            rep.issues.append(Issue(
                severity="error",
                code="xorprev_slot_missing_prev",
                message=(
                    f"slot {slot.index} is XorPrev encoding but prev_slot is "
                    f"NEVER_TOUCHED sentinel"
                ),
            ))
        # Chain topology: prev_slot must point earlier in the store (forward-only).
        # This catches cycles and back-references that would infinite-loop on
        # decompression.
        elif slot.prev_slot >= slot.index:
            rep.issues.append(Issue(
                severity="error",
                code="xorprev_backward_or_self_ref",
                message=(
                    f"slot {slot.index} has prev_slot={slot.prev_slot} "
                    f"(must be strictly less than {slot.index} — chains "
                    f"are forward-only)"
                ),
            ))


def _verify_crcs(rep: IntegrityReport, dump: TtdDump) -> None:
    """Re-decompress every slot referenced by any checkpoint and verify CRC.

    Skips unreferenced slots (they're dead code in the dump and don't
    contribute to integrity for any visible checkpoint).
    """
    referenced = set()
    for cp in dump.checkpoints:
        for ref in cp.ram_sub_slots:
            if ref != NEVER_TOUCHED_SLOT:
                referenced.add(ref)

    for slot_idx in sorted(referenced):
        try:
            # get_sub_page recompresses, decompresses, walks XorPrev chain.
            # Any tampering post-write would surface here.
            dump.get_sub_page(slot_idx)
        except Exception as e:
            rep.issues.append(Issue(
                severity="error",
                code="slot_decompress_failed",
                message=(
                    f"slot {slot_idx}: decompress/CRC verify failed: {e}"
                ),
            ))


def _check_journal(dump: TtdDump, rep: IntegrityReport) -> None:
    """Validate the write-journal section against its own directory.

    The journal is the largest section in a typical file - 71% of a demo
    recording - and is stored as compressed blocks that nothing else reads, so
    a truncated or mis-sized block would otherwise go unnoticed. These checks
    compare the directory's numbers for internal consistency; they do not
    decompress payloads.
    """
    journal = getattr(dump, "journal", None)
    if journal is None or not journal.record_count:
        return

    counted = sum(b.record_count for b in journal.blocks)
    if counted != journal.record_count:
        rep.issues.append(Issue(
            "error", "journal_count_mismatch",
            f"journal header declares {journal.record_count} records but its "
            f"blocks account for {counted}"))

    if len(journal.payloads) != len(journal.blocks):
        rep.issues.append(Issue(
            "error", "journal_truncated",
            f"journal has {len(journal.blocks)} block descriptors but "
            f"{len(journal.payloads)} payloads"))

    for i, block in enumerate(journal.blocks):
        if block.record_count and block.compressed_size == 0:
            rep.issues.append(Issue(
                "error", "journal_empty_block",
                f"journal block {i} claims {block.record_count} records in 0 bytes"))
        if block.last_global_t < block.first_global_t:
            rep.issues.append(Issue(
                "error", "journal_time_inverted",
                f"journal block {i} spans globalT {block.first_global_t} to "
                f"{block.last_global_t}, which runs backwards"))

    # Blocks are appended in time order. A gap is legal (the ring drops old
    # records), an overlap means two blocks claim the same instant.
    for i in range(1, len(journal.blocks)):
        if journal.blocks[i].first_global_t < journal.blocks[i - 1].last_global_t:
            rep.issues.append(Issue(
                "warning", "journal_blocks_overlap",
                f"journal blocks {i - 1} and {i} overlap in globalT"))
            break
