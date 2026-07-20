/// @file ttd_checkpoint.cpp
/// @brief Capture/restore helpers for TTDCheckpoint's CPU and chipset fields.
///
/// These are deliberately boring field copies — no logic, no side effects.
/// The interesting work (paging rebuild, Screen::InitFrame, peripheral
/// load) lives in the restore orchestrator that calls these helpers.

#include "ttd_checkpoint.h"

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
    TTDCpuState dst;

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
    TTDChipsetState dst;

    // Counters
    dst.t_states = src.t_states;
    dst.frame_counter = src.frame_counter;

    // Standard port latches
    dst.p7FFD = src.p7FFD;
    dst.pFE = src.pFE;
    dst.pEFF7 = src.pEFF7;
    dst.pXXXX = src.pXXXX;
    dst.pBFFD = src.pBFFD;
    dst.pFFFD = src.pFFFD;
    dst.pDFFD = src.pDFFD;
    dst.pFDFD = src.pFDFD;
    dst.p1FFD = src.p1FFD;
    dst.pFF77 = src.pFF77;
    dst.border_attr = src.border_attr;
    dst.flags = src.flags;

    // Extended port latches
    dst.p7EFD = src.p7EFD;
    dst.p78FD = src.p78FD;
    dst.p7AFD = src.p7AFD;
    dst.p7CFD = src.p7CFD;
    dst.gmx_config = src.gmx_config;
    dst.gmx_magic_shift = src.gmx_magic_shift;
    dst.p00 = src.p00;
    dst.p80FD = src.p80FD;
    dst.aFE = src.aFE;
    dst.aFB = src.aFB;
    dst.aFF77 = static_cast<uint8_t>(src.aFF77);
    dst.active_ay = static_cast<uint8_t>(src.active_ay);
    dst.pBD = src.pBD;
    dst.pBE = src.pBE;
    dst.pBF = src.pBF;
    dst.pFFBA = src.pFFBA;
    dst.p7FBA = src.p7FBA;
    dst.p0F = src.p0F;
    dst.p1F = src.p1F;
    dst.p4F = src.p4F;
    dst.p5F = src.p5F;
    dst.pLSY256 = src.pLSY256;
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

    // ATM 7.10 / ATM3 memory mapping
    static_assert(sizeof(dst.pFFF7) == sizeof(src.pFFF7),
                  "pFFF7 size mismatch");
    std::memcpy(dst.pFFF7, src.pFFF7, sizeof(dst.pFFF7));

    return dst;
}

void RestoreChipsetState(const TTDChipsetState& src, EmulatorState* dst)
{
    if (!dst) return;

    // Counters
    dst->t_states = src.t_states;
    dst->frame_counter = src.frame_counter;

    // Standard port latches
    dst->p7FFD = src.p7FFD;
    dst->pFE = src.pFE;
    dst->pEFF7 = src.pEFF7;
    dst->pXXXX = src.pXXXX;
    dst->pBFFD = src.pBFFD;
    dst->pFFFD = src.pFFFD;
    dst->pDFFD = src.pDFFD;
    dst->pFDFD = src.pFDFD;
    dst->p1FFD = src.p1FFD;
    dst->pFF77 = src.pFF77;
    dst->border_attr = src.border_attr;
    dst->flags = src.flags;

    // Extended port latches
    dst->p7EFD = src.p7EFD;
    dst->p78FD = src.p78FD;
    dst->p7AFD = src.p7AFD;
    dst->p7CFD = src.p7CFD;
    dst->gmx_config = src.gmx_config;
    dst->gmx_magic_shift = src.gmx_magic_shift;
    dst->p00 = src.p00;
    dst->p80FD = src.p80FD;
    dst->aFE = src.aFE;
    dst->aFB = src.aFB;
    dst->aFF77 = src.aFF77;
    dst->active_ay = src.active_ay;
    dst->pBD = src.pBD;
    dst->pBE = src.pBE;
    dst->pBF = src.pBF;
    dst->pFFBA = src.pFFBA;
    dst->p7FBA = src.p7FBA;
    dst->p0F = src.p0F;
    dst->p1F = src.p1F;
    dst->p4F = src.p4F;
    dst->p5F = src.p5F;
    dst->pLSY256 = src.pLSY256;
    std::memcpy(dst->wd_shadow, src.wd_shadow, sizeof(dst->wd_shadow));

    // Video / palette
    std::memcpy(dst->comp_pal, src.comp_pal, sizeof(dst->comp_pal));
    dst->ulaplus_mode = src.ulaplus_mode;
    dst->ulaplus_reg = src.ulaplus_reg;
    std::memcpy(dst->ulaplus_cram, src.ulaplus_cram, sizeof(dst->ulaplus_cram));

    // ATM 7.10 / ATM3 memory mapping
    std::memcpy(dst->pFFF7, src.pFFF7, sizeof(dst->pFFF7));

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
