/// @file ttdcheckpoint.cpp
/// @brief Capture/restore helpers for TTDCheckpoint's CPU and chipset fields.
///
/// These are deliberately boring field copies — no logic, no side effects.
/// The interesting work (paging rebuild, Screen::InitFrame, peripheral
/// load) lives in the restore orchestrator that calls these helpers.

#include "ttdcheckpoint.h"

#include <cstring>

// Pull in the actual struct definitions so we can read/write their fields.
#include "emulator/cpu/z80.h"       // Z80State (inherits Z80Registers)
#include "emulator/platform.h"      // EmulatorState

namespace ttd {

// ---------------------------------------------------------------------------
// CPU capture / restore
// ---------------------------------------------------------------------------

TTDCpuState CaptureCpuState(const Z80State& src)
{
    // Zero the entire struct first so any padding bytes between fields are
    // deterministic. Without this, byte-wise hashing of the captured state
    // (e.g. by the Phase 2 Item 7 divergence oracle) would pick up
    // uninitialized padding and produce non-reproducible hashes. Default
    // member initializers zero the named fields but not the padding between
    // them; this memset closes that gap and matches CaptureSnapshot's pattern.
    TTDCpuState dst;
    std::memset(static_cast<void*>(&dst), 0, sizeof(dst));

    // 16-bit registers (unions in Z80Registers mean we can read either form)
    dst.pc = src.pc;
    dst.sp = src.sp;
    dst.af = src.af;
    dst.bc = src.bc;
    dst.de = src.de;
    dst.hl = src.hl;
    dst.ix = src.ix;
    dst.iy = src.iy;

    // Alternate register set
    dst.alt_af = src.alt.af;
    dst.alt_bc = src.alt.bc;
    dst.alt_de = src.alt.de;
    dst.alt_hl = src.alt.hl;

    // 8-bit / interrupt state
    dst.i = src.i;
    dst.r_low = src.r_low;
    dst.r_hi = src.r_hi;
    dst.iff1 = src.iff1;
    dst.iff2 = src.iff2;
    dst.im = src.im;
    dst.halted = src.halted;

    // Undocumented but observable
    dst.memptr = src.memptr;
    dst.q = src.q;

    // HALT / interrupt bookkeeping
    dst.eipos = src.eipos;
    dst.haltpos = src.haltpos;
    dst.nmi_in_progress = src.nmi_in_progress ? 1 : 0;
    dst.int_pending = src.int_pending ? 1 : 0;
    dst.int_gate = src.int_gate ? 1 : 0;
    dst.halt_cycle = src.halt_cycle;

    return dst;
}

void RestoreCpuState(const TTDCpuState& src, Z80State* dst)
{
    if (!dst) return;

    // 16-bit registers
    dst->pc = src.pc;
    dst->sp = src.sp;
    dst->af = src.af;
    dst->bc = src.bc;
    dst->de = src.de;
    dst->hl = src.hl;
    dst->ix = src.ix;
    dst->iy = src.iy;

    // Alternate register set
    dst->alt.af = src.alt_af;
    dst->alt.bc = src.alt_bc;
    dst->alt.de = src.alt_de;
    dst->alt.hl = src.alt_hl;

    // 8-bit / interrupt state
    dst->i = src.i;
    dst->r_low = src.r_low;
    dst->r_hi = src.r_hi;
    dst->iff1 = src.iff1;
    dst->iff2 = src.iff2;
    dst->im = src.im;
    dst->halted = src.halted;

    // Undocumented but observable
    dst->memptr = src.memptr;
    dst->q = src.q;

    // HALT / interrupt bookkeeping
    dst->eipos = src.eipos;
    dst->haltpos = src.haltpos;
    dst->nmi_in_progress = src.nmi_in_progress != 0;
    dst->int_pending = src.int_pending != 0;
    dst->int_gate = src.int_gate != 0;
    dst->halt_cycle = src.halt_cycle;

    // Deliberately NOT touched (host-side; preserved by caller):
    //   FastMemIf, DbgMemIf, MemIf         — reattached by orchestrator
    //   isDebugMode, cycles_to_capture     — debugger session state
    //   trace_curs/top/mode, mem_curs/top/second, pc_trflags — UI cursors
    //   prev_pc, m1_pc, last_branch, nextpc — debug view / prefetch cache
    //   rate, vm1, outc0, tpi, trpc[]      — CPU variant config (set at init)
    //   prefix, opcode, operand1/2         — transient decode scratch
    //   z80_index                           — instance enumeration
}

// ---------------------------------------------------------------------------
// Chipset capture / restore
// ---------------------------------------------------------------------------

TTDChipsetState CaptureChipsetState(const EmulatorState& src)
{
    // Zero first so padding is deterministic — see CaptureCpuState for the
    // full rationale. The chipset struct has many sub-arrays (wd_shadow,
    // comp_pal, ulaplus_cram, pFFF7, ...) which are explicitly memcpy'd
    // below; the memset ensures any byte NOT covered by an explicit copy
    // is zero rather than indeterminate.
    TTDChipsetState dst;
    std::memset(static_cast<void*>(&dst), 0, sizeof(dst));

    // Counters
    dst.t_states = src.t_states;
    dst.frame_counter = src.frame_counter;

    // Standard 128K port latches
    dst.p7FFD = src.p7FFD;
    dst.pFE = src.pFE;
    dst.pEFF7 = src.pEFF7;
    dst.pBFFD = src.pBFFD;
    dst.pFFFD = src.pFFFD;
    dst.pFF77 = src.pFF77;
    dst.border_attr = src.border_attr;
    dst.flags = src.flags;

    // FDC state
    static_assert(sizeof(dst.wd_shadow) == sizeof(src.wd_shadow),
                  "wd_shadow size mismatch");
    std::memcpy(dst.wd_shadow, src.wd_shadow, sizeof(dst.wd_shadow));

    // Video / palette
    static_assert(sizeof(dst.comp_pal) == sizeof(src.comp_pal),
                  "comp_pal size mismatch");
    std::memcpy(dst.comp_pal, src.comp_pal, sizeof(dst.comp_pal));
    dst.ulaplus_mode = src.ulaplus_mode;
    dst.ulaplus_reg = src.ulaplus_reg;
    static_assert(sizeof(dst.ulaplus_cram) == sizeof(src.ulaplus_cram),
                  "ulaplus_cram size mismatch");
    std::memcpy(dst.ulaplus_cram, src.ulaplus_cram, sizeof(dst.ulaplus_cram));

    // Extended/model-specific ports handled via TTDPeripheralRegistry

    return dst;
}

void RestoreChipsetState(const TTDChipsetState& src, EmulatorState* dst)
{
    if (!dst) return;

    // Counters
    dst->t_states = src.t_states;
    dst->frame_counter = src.frame_counter;

    // Standard 128K port latches
    dst->p7FFD = src.p7FFD;
    dst->pFE = src.pFE;
    dst->pEFF7 = src.pEFF7;
    dst->pBFFD = src.pBFFD;
    dst->pFFFD = src.pFFFD;
    dst->pFF77 = src.pFF77;
    dst->border_attr = src.border_attr;
    dst->flags = src.flags;

    // FDC state
    std::memcpy(dst->wd_shadow, src.wd_shadow, sizeof(dst->wd_shadow));

    // Video / palette
    std::memcpy(dst->comp_pal, src.comp_pal, sizeof(dst->comp_pal));
    dst->ulaplus_mode = src.ulaplus_mode;
    dst->ulaplus_reg = src.ulaplus_reg;
    std::memcpy(dst->ulaplus_cram, src.ulaplus_cram, sizeof(dst->ulaplus_cram));

    // Extended/model-specific ports handled via TTDPeripheralRegistry

    // Deliberately NOT touched (peripheral or host-side):
    //   tape      — pointer-bearing; handled via TTDSerializable (P1.5)
    //   nvram     — peripheral; handled via TTDSerializable when wired in
    //   video_memory_changed — debug-only flag
    //   nmi_in_progress — already in TTDCpuState
    //   ts (TSPORTS_t)   — TS-Conf specific; not supported in v1
    //   cram, sfile      — TS-Conf palette/sprite files; not in v1
    //
    // Caller is responsible for re-running the port decoder to rebuild
    // memory bank mappings from the restored port latches (TDD §8.1 step 2b).
}

} // namespace ttd
