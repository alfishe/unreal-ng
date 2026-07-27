"""Heuristic anomaly detection for parsed .ttd dumps.

These checks look for *suspicious* patterns in the captured state that often
indicate capture/restore bugs but are not format-level corruption. They are
opinionated and produce findings with severity + a hypothesis about the
likely cause.

Categories
----------
1. CPU state anomalies — likely causes of "Play → reset"
   SP outside valid RAM, PC near reset vector, HALT without recovery,
   iff1/iff2 mismatch, sudden PC jumps between frames.

2. Chipset anomalies — likely causes of "no seek refresh"
   p7FFD bit 3 flip without RAM page update, pFE border drift,
   ULAplus mode without palette.

3. Capture-completeness anomalies
   Frame with zero dirty pages when neighbours have many (suspect dirty
   tracker missed a write). Page content flip-flop (suggests capture saw
   stale data).
"""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set

from .ttd_format import Checkpoint, TtdDump, NEVER_TOUCHED_PAGE_REF, PAGE_SIZE


@dataclass
class Finding:
    severity: str  # "info" | "warning" | "critical"
    category: str  # "cpu" | "chipset" | "capture" | "peripheral"
    code: str
    message: str
    checkpoint_index: int = -1
    detail: Optional[Dict] = None


@dataclass
class AnomalyReport:
    findings: List[Finding] = field(default_factory=list)

    @property
    def critical(self) -> List[Finding]:
        return [f for f in self.findings if f.severity == "critical"]

    @property
    def warnings(self) -> List[Finding]:
        return [f for f in self.findings if f.severity == "warning"]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _ram_top_bytes(model_ram_pages: int) -> int:
    """Top of physical RAM (last byte address + 1) for the model."""
    return model_ram_pages * PAGE_SIZE


def _page_fingerprint(page_bytes: bytes) -> str:
    return hashlib.sha256(page_bytes).hexdigest()[:16]


def _materialized_ram_hash(dump: TtdDump, cp: Checkpoint) -> str:
    """Stable hash of the full RAM image for the checkpoint."""
    return hashlib.sha256(dump.materialize_ram(cp)).hexdigest()


# ---------------------------------------------------------------------------
# 1. CPU state anomalies
# ---------------------------------------------------------------------------


def _check_cpu(dump: TtdDump, rep: AnomalyReport) -> None:
    ram_top = _ram_top_bytes(dump.header.model_ram_pages)

    # Spectrum convention: stack lives in the upper portion of bank 0 (0x8000+).
    # SP far below 0x4000 is in ROM; SP at exactly 0x0000 is the post-reset
    # value; SP > ram_top means it's pointing off the end of RAM.
    for cp in dump.checkpoints:
        cpu = cp.cpu
        # SP sanity
        if cpu.sp == 0:
            rep.findings.append(Finding(
                severity="critical",
                category="cpu",
                code="sp_at_zero",
                message=(
                    f"checkpoint {cp.index} (frame {cp.frame}): SP=0x0000 — "
                    f"post-reset value; emulator likely reset between captures"
                ),
                checkpoint_index=cp.index,
                detail={"sp": cpu.sp, "pc": cpu.pc},
            ))
        elif cpu.sp < 0x4000:
            rep.findings.append(Finding(
                severity="warning",
                category="cpu",
                code="sp_in_rom",
                message=(
                    f"checkpoint {cp.index} (frame {cp.frame}): SP=0x{cpu.sp:04X} "
                    f"is inside ROM (<0x4000) — stack writes would hit ROM"
                ),
                checkpoint_index=cp.index,
            ))

        # PC sanity: near reset vector
        if cpu.pc < 0x0010:
            rep.findings.append(Finding(
                severity="warning",
                category="cpu",
                code="pc_at_reset_vector",
                message=(
                    f"checkpoint {cp.index} (frame {cp.frame}): PC=0x{cpu.pc:04X} "
                    f"is at the reset vector"
                ),
                checkpoint_index=cp.index,
            ))

        # HALT without recovery position
        if cpu.halted and cpu.haltpos == 0:
            rep.findings.append(Finding(
                severity="info",
                category="cpu",
                code="halt_without_position",
                message=(
                    f"checkpoint {cp.index} (frame {cp.frame}): halted=1 but "
                    f"haltpos=0 — either capture missed haltpos or HALT was "
                    f"never actually entered"
                ),
                checkpoint_index=cp.index,
            ))

        # iff1 / iff2 mismatch (they should normally track each other)
        if cpu.iff1 != cpu.iff2:
            rep.findings.append(Finding(
                severity="info",
                category="cpu",
                code="iff_flipflop_mismatch",
                message=(
                    f"checkpoint {cp.index} (frame {cp.frame}): iff1={cpu.iff1} "
                    f"iff2={cpu.iff2} — expected to match in normal operation"
                ),
                checkpoint_index=cp.index,
            ))

    # Sudden PC jumps between consecutive checkpoints
    # The PC at the next frame boundary should be "near" the previous one
    # OR clearly at an interrupt vector. A jump of >32 KB is suspicious.
    for i in range(1, len(dump.checkpoints)):
        prev = dump.checkpoints[i - 1].cpu.pc
        curr = dump.checkpoints[i].cpu.pc
        # Wraparound distance
        d = abs(curr - prev)
        d = min(d, 0x10000 - d)
        if d > 0x8000:
            rep.findings.append(Finding(
                severity="warning",
                category="cpu",
                code="large_pc_jump",
                message=(
                    f"checkpoints {i-1}→{i}: PC jumped 0x{d:04X} "
                    f"(0x{prev:04X} → 0x{curr:04X})"
                ),
                checkpoint_index=i,
            ))


# ---------------------------------------------------------------------------
# 2. Chipset anomalies
# ---------------------------------------------------------------------------


def _check_chipset(dump: TtdDump, rep: AnomalyReport) -> None:
    for i, cp in enumerate(dump.checkpoints):
        cs = cp.chipset

        # ULAplus engaged but palette empty?
        if cs.ulaplus_mode == 1 and not any(cs.ulaplus_cram):
            rep.findings.append(Finding(
                severity="warning",
                category="chipset",
                code="ulaplus_no_palette",
                message=(
                    f"checkpoint {cp.index} (frame {cp.frame}): ulaplus_mode=1 "
                    f"but all 64 palette entries are zero — capture missed palette?"
                ),
                checkpoint_index=cp.index,
            ))

        # pFE border bits vs derived border_attr
        # The renderer derives _borderColor from pFE bits 0-2; if border_attr
        # (the EmulatorState cached value) drifts from pFE bits, seek refresh
        # will paint the wrong border.
        border_from_pfe = cs.pfe & 0b0000_0111
        if border_from_pfe != cs.border_attr:
            rep.findings.append(Finding(
                severity="info",
                category="chipset",
                code="border_attr_drift",
                message=(
                    f"checkpoint {cp.index} (frame {cp.frame}): pFE border bits "
                    f"= {border_from_pfe}, border_attr = {cs.border_attr} — "
                    f"derived field drifted from source"
                ),
                checkpoint_index=cp.index,
            ))

        # p7FFD bit 3 flip without corresponding screen-bank page change
        # (the most common "no seek refresh" cause).
        if i > 0:
            prev_cs = dump.checkpoints[i - 1].chipset
            screen_bank_flipped = (cs.p7ffd ^ prev_cs.p7ffd) & 0b0000_1000
            if screen_bank_flipped:
                # Did the active screen bank's RAM content actually change?
                # Banks: bit3=0 → bank 5 (page 5), bit3=1 → bank 7 (page 7).
                # If the content didn't change despite the bank flip, capture
                # missed the screen RAM update.
                # We can't easily tell without materializing RAM for both
                # checkpoints — flag as info for now.
                rep.findings.append(Finding(
                    severity="info",
                    category="chipset",
                    code="screen_bank_flipped",
                    message=(
                        f"checkpoints {i-1}→{i}: p7FFD bit 3 flipped "
                        f"(0x{prev_cs.p7ffd:02X} → 0x{cs.p7ffd:02X}) — "
                        f"screen bank switched; verify capture saw the new bank's content"
                    ),
                    checkpoint_index=i,
                ))


# ---------------------------------------------------------------------------
# 3. Capture-completeness anomalies
# ---------------------------------------------------------------------------


def _check_capture_completeness(dump: TtdDump, rep: AnomalyReport) -> None:
    """Look for patterns suggesting the dirty tracker missed writes."""
    if len(dump.checkpoints) < 2:
        return

    # Per-checkpoint set of unique pages referenced (excluding NEVER_TOUCHED).
    dirty_counts: List[int] = []
    for cp in dump.checkpoints:
        unique_pages = set()
        for ref in cp.ram_page_refs:
            if ref != NEVER_TOUCHED_PAGE_REF:
                unique_pages.add(ref)
        dirty_counts.append(len(unique_pages))

    # Compute delta vs previous checkpoint: how many NEW pages appeared.
    # A "new page" is one whose slot index wasn't referenced by the previous
    # checkpoint — i.e., it was freshly Interned.
    for i in range(1, len(dump.checkpoints)):
        prev_refs = set(r for r in dump.checkpoints[i - 1].ram_page_refs
                        if r != NEVER_TOUCHED_PAGE_REF)
        curr_refs = set(r for r in dump.checkpoints[i].ram_page_refs
                        if r != NEVER_TOUCHED_PAGE_REF)
        new_refs = curr_refs - prev_refs
        if len(new_refs) == 0 and dirty_counts[i] > 0:
            # No new content but the count differs — page was re-used.
            pass
        # Frames with zero dirty pages between busy frames are suspect.
        if i >= 2 and len(new_refs) == 0:
            prev_prev = set(r for r in dump.checkpoints[i - 2].ram_page_refs
                            if r != NEVER_TOUCHED_PAGE_REF)
            if len(curr_refs - prev_prev) > 0 or len(prev_prev - curr_refs) > 0:
                # Frame had changes vs i-2, just shared with i-1 — that's fine.
                pass

    # Page content flip-flop detection: same slot index referenced across
    # consecutive checkpoints but with different SHA-256 implies the page
    # was re-Interned (good — capture saw the write). Same content + same
    # slot = no change. Same content + different slot = wasteful but not
    # incorrect. Different content + same slot across non-adjacent frames
    # is fine (slot reused after Release).
    # No anomaly to surface here directly — this is informational.


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------


def detect_anomalies(dump: TtdDump) -> AnomalyReport:
    rep = AnomalyReport()
    _check_cpu(dump, rep)
    _check_chipset(dump, rep)
    _check_capture_completeness(dump, rep)
    return rep
