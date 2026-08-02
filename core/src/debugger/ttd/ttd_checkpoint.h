#pragma once

/// @file ttd_checkpoint.h
/// @brief TTD checkpoint state structs and capture/restore helpers.
///
/// Per parent TDD §6.1, a checkpoint is a complete, self-sufficient machine
/// state captured at a frame boundary. This header defines the POD struct
/// types that hold that state and the field-copy helpers that move data
/// between the live emulator structs (Z80State, EmulatorState) and the
/// checkpoint representation.
///
/// Layering:
///   - Capture/Restore functions in this file are *pure field copies*. They
///     do not touch Memory, PortDecoder, Screen, or any peripheral.
///   - Higher-level orchestration (paging rebuild via PortDecoder,
///     Screen::InitFrame, peripheral TTDLoadState) happens in the restore
///     orchestrator, not here. See TDD §8.1 (SeekTo algorithm step 2).
///   - RAM pages are referenced via TTDPageRef placeholders; the COW page
///     store itself is item 3 of P1 and lands in ttd_page_store.h.
///
/// Excluded by design (host-side, preserved by caller on restore):
///   - Z80State::FastMemIf / DbgMemIf / MemIf (pointers)
///   - Z80State::isDebugMode, trace_curs/top/mode, mem_curs/top/second,
///     pc_trflags, prev_pc, m1_pc, last_branch, nextpc
///   - Z80State::rate, vm1, outc0, tpi, trpc[] (CPU config, not state)
///   - Z80State::cycles_to_capture (debugger trigger state)
///   - EmulatorState::video_memory_changed (debug-only flag)
///   - EmulatorState::tape (pointer-bearing; handled via TTDSerializable)

#include <cstdint>
#include <cstddef>
#include <vector>

#include "ttd_serializable.h"

// Forward declarations only — we don't want to pull the full emulator headers
// into every TTD translation unit. The capture/restore .cpp includes them.
struct Z80State;
struct EmulatorState;

namespace ttd {

/// @brief Position within the recorded timeline.
/// Per TDD §5.1. Checkpoints always sit at frame boundaries (tInFrame == 0).
struct TTDTimePoint
{
    uint64_t frame = 0;       ///< Frame index since session start
    uint32_t tInFrame = 0;    ///< T-states within the frame (0 = frame start)

    bool operator==(const TTDTimePoint& o) const
    { return frame == o.frame && tInFrame == o.tInFrame; }
    bool operator!=(const TTDTimePoint& o) const { return !(*this == o); }
    bool operator<(const TTDTimePoint& o) const
    {
        return frame < o.frame || (frame == o.frame && tInFrame < o.tInFrame);
    }
};

/// @brief Architectural Z80 state — everything a deterministic re-execution
/// must reproduce. Mirrors the Z80 register file plus the undocumented but
/// observable internal registers (MEMPTR, Q) documented in z80.h.
///
/// Layout is plain POD (no unions, no packing pragmas) so it can be copied
/// byte-wise into a checkpoint and memcmp'd in tests. Size is ~36 bytes.
struct TTDCpuState
{
    // ---- 16-bit registers ----
    uint16_t pc = 0;
    uint16_t sp = 0;
    uint16_t af = 0, bc = 0, de = 0, hl = 0;
    uint16_t ix = 0, iy = 0;

    // ---- Alternate register set ----
    uint16_t alt_af = 0, alt_bc = 0, alt_de = 0, alt_hl = 0;

    // ---- 8-bit registers / interrupt state ----
    uint8_t  i = 0;             ///< Interrupt vector
    uint8_t  r_low = 0;         ///< Refresh register (low 7 bits)
    uint8_t  r_hi = 0;          ///< Undocumented high bit of R
    uint8_t  iff1 = 0;          ///< Interrupt enable flip-flop 1
    uint8_t  iff2 = 0;          ///< Interrupt enable flip-flop 2
    uint8_t  im = 0;            ///< Interrupt mode (0/1/2)
    uint8_t  halted = 0;        ///< CPU HALT state (0/1)

    // ---- Undocumented but observable ----
    uint16_t memptr = 0;        ///< MEMPTR / WZ — affects BIT n,(HL) undocumented flags
    uint8_t  q = 0;             ///< Q register — affects CCF/SCF undocumented flag behavior

    // ---- HALT / interrupt bookkeeping ----
    /// EI instruction position. Restoring this matters because the Z80
    /// disables interrupts for one instruction after EI; an incomplete
    /// restore would let an interrupt fire one instruction too early.
    uint16_t eipos = 0;
    /// HALT instruction position — used for HALT timing/accounting.
    uint16_t haltpos = 0;

    uint8_t  nmi_in_progress = 0;
    uint8_t  int_pending = 0;   ///< INT line state (latched)
    uint8_t  int_gate = 1;      ///< External interrupts gate (1 = enabled)
    uint32_t halt_cycle = 0;   ///< Cycle at which HALT became active
};

/// @brief Chipset state — the port-latch subset of EmulatorState plus
/// counters. Captured at frame boundaries so the restore path can rebuild
/// memory paging by re-running the port decoder (TDD §8.1 step 2b).
///
/// Peripheral state (AY, tape, FDC, Covox) is NOT here — those devices
/// implement TTDSerializable and land in TTDCheckpoint as blobs.
///
/// For v1 only the standard port latches are actively populated (48K,
/// 128K, Pentagon). Extended fields (GMX, ATM, Quorum, SMUC, Soundrive)
/// are present in the struct so future models don't change its layout;
/// they read as zero on models that don't use them.
struct TTDChipsetState
{
    // ---- Counters ----
    uint64_t t_states = 0;
    uint64_t frame_counter = 0;

    // ---- Standard Spectrum 128K port latches ----
    uint8_t p7FFD = 0;       ///< 128K banking / screen / ROM select
    uint8_t pFE = 0;         ///< Beeper / EAR / border color / mic
    uint8_t pEFF7 = 0;       ///< Beta Disk interface control
    uint8_t pXXXX = 0;       ///< Reserved / model-specific
    uint8_t pBFFD = 0;       ///< AY-3-8912 register select
    uint8_t pFFFD = 0;       ///< AY-3-8912 data
    uint8_t pDFFD = 0;       ///< Pentagon 512K / Profi extension banking
    uint8_t pFDFD = 0;       ///< Profi extension banking
    uint8_t p1FFD = 0;       ///< +3 / Pentagon 1024 banking
    uint8_t pFF77 = 0;       ///< TurboSound chip select

    uint8_t border_attr = 0;
    uint8_t flags = 0;       ///< Runtime execution flags (CF_TRDOS etc.)

    // ---- Extended port latches (populated only on relevant models) ----
    uint8_t p7EFD = 0, p78FD = 0, p7AFD = 0, p7CFD = 0;  ///< GMX
    uint8_t gmx_config = 0, gmx_magic_shift = 0;
    uint8_t p00 = 0, p80FD = 0;                            ///< Quorum
    uint8_t aFE = 0, aFB = 0;                              ///< ATM 4.50 system
    uint8_t aFF77 = 0;                                     ///< ATM TurboSound
    uint8_t active_ay = 0;                                 ///< Active AY chip index
    uint8_t pBD = 0, pBE = 0, pBF = 0;                     ///< ATM3
    uint8_t pFFBA = 0, p7FBA = 0;                          ///< SMUC
    uint8_t p0F = 0, p1F = 0, p4F = 0, p5F = 0;            ///< Soundrive
    uint8_t pLSY256 = 0;
    uint8_t wd_shadow[4] = {0, 0, 0, 0};                   ///< 2F, 4F, 6F, 8F

    // ---- Video / palette ----
    uint8_t comp_pal[16] = {0};        ///< Hardware palette registers
    uint8_t ulaplus_mode = 0;
    uint8_t ulaplus_reg = 0;
    uint8_t ulaplus_cram[64] = {0};    ///< ULAplus palette entries

    // ---- ATM 7.10 / ATM3 memory mapping ----
    uint32_t pFFF7[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    // ---- TS-Conf palette/sprite files (allocated but unused in v1) ----
    // cram[256] and sfile[256] are deliberately omitted for v1 — they add
    // 2 KB per checkpoint and no v1-supported model populates them. Adding
    // them later is a session-internal layout change (no on-disk format
    // compatibility to preserve in v1).
};

/// @brief Frame kind discriminator (I-frame / P-frame).
///
/// Mirrors video codec terminology:
///   - I-frame (key frame): every RAM page is captured as a Full snapshot.
///     Restore is O(1) — just decompress the slots. Emitted every
///     kKeyFrameInterval (default 50 frames = 1 second @ 50 Hz).
///   - P-frame (delta frame): only dirty pages are captured, encoded as
///     XOR deltas against their previous slot. Restore requires walking
///     the delta chain back to the nearest I-frame.
enum class TTDFrameKind : uint8_t {
    KeyFrame  = 0,   ///< I-frame: all pages stored as Full
    DeltaFrame = 1,  ///< P-frame: only dirty pages stored as Xor
};

/// @brief Reference to one 16 KB emulator RAM page in the codec page store.
///
/// Each 16 KB page is split into 4 × 4 KB sub-pages internally (see PoC
/// results doc — 92.9 % of dirty 16K pages have only one dirty 4K sub-page,
/// so 4K granularity saves ~50 % storage on real workloads).
///
/// The special value #kNeverTouched in all four slots marks a page that was
/// never written during the session up to this checkpoint; restore leaves
/// live RAM as-is for it (its content IS the historical content).
struct TTDPageRef
{
    /// 4 sub-page slot indices, one per 4 KB chunk of the 16 KB page.
    /// pageSlots[0] covers bytes [0..4095], pageSlots[1] covers [4096..8191], etc.
    uint32_t pageSlots[4] = {0, 0, 0, 0};

    /// Sentinel for "page never touched in session up to this checkpoint".
    static constexpr uint32_t kNeverTouched = 0xFFFFFFFFu;

    bool IsNeverTouched() const
    {
        return pageSlots[0] == kNeverTouched && pageSlots[1] == kNeverTouched
            && pageSlots[2] == kNeverTouched && pageSlots[3] == kNeverTouched;
    }

    /// Mark this ref as never-touched (initial state for untouched pages).
    void SetNeverTouched()
    {
        pageSlots[0] = pageSlots[1] = pageSlots[2] = pageSlots[3] = kNeverTouched;
    }
};

/// @brief A complete, self-sufficient machine state at a frame boundary.
///
/// Per TDD §6.1. This is the in-memory representation of one timeline
/// entry. Items P1.2 (dirty tracker) and P1.3 (codec page store) populate
/// #ramPages; items P1.5 (peripheral serializers) populate the
/// peripheral blob vectors.
struct TTDCheckpoint
{
    TTDTimePoint time;
    uint64_t     globalT = 0;  ///< Denormalized sort key (== time.frame at frame boundary)

    /// I-frame or P-frame? Determines restore strategy.
    TTDFrameKind frameKind = TTDFrameKind::KeyFrame;

    /// For P-frames: the frame index of the nearest preceding I-frame.
    /// Restore walks deltas from keyFrameAnchor to time.frame.
    /// For I-frames: equal to time.frame.
    uint64_t keyFrameAnchor = 0;

    TTDCpuState     cpu;
    TTDChipsetState chipset;

    // --- Peripheral state blobs (populated by P1.5) ---
    /// Empty until the corresponding device's TTDSerializable implementation
    /// lands. Devices that have not been wired in yet simply contribute an
    /// empty vector — the round-trip remains valid (capture writes nothing,
    /// restore reads nothing).
    std::vector<uint8_t> ayState;
    std::vector<uint8_t> fdcState;
    std::vector<uint8_t> tapeState;
    std::vector<uint8_t> covoxState;

    // --- RAM pages (populated by P1.3) ---
    /// One entry per physical RAM page (16 KB) of the active model. Each
    /// entry references 4 codec-store sub-page slots (4 KB each).
    std::vector<TTDPageRef> ramPages;

    // --- Journal offsets (populated by P2 and P4) ---
    uint64_t inputJournalOffset = 0;
    uint64_t writeJournalOffset = 0;
};

// ---------------------------------------------------------------------------
// Capture / restore helpers (pure field copies; no side effects).
// ---------------------------------------------------------------------------

/// @brief Snapshot the architectural subset of a Z80State.
/// Host-side fields (MemIf pointers, trace cursors, debug flag) are ignored.
TTDCpuState CaptureCpuState(const Z80State& src);

/// @brief Restore the architectural subset of a Z80State in place.
/// Host-side fields (MemIf pointers, trace cursors, isDebugMode, rate/tpi,
/// cycles_to_capture, prev_pc/m1_pc/last_branch/nextpc) are PRESERVED —
/// the caller already has them set up correctly for the live emulator.
void RestoreCpuState(const TTDCpuState& src, Z80State* dst);

/// @brief Snapshot the port-latch + counter subset of an EmulatorState.
/// Peripheral-bearing sub-structs (tape, nvram) are ignored — those are
/// handled via TTDSerializable at the checkpoint-orchestration layer.
TTDChipsetState CaptureChipsetState(const EmulatorState& src);

/// @brief Restore the port-latch + counter subset of an EmulatorState.
/// Does NOT re-run the port decoder — that is the caller's responsibility
/// (TDD §8.1 step 2b: "re-run paging decode to rebuild bank mappings").
/// The caller must invoke Memory::SetRomPage-family / PortDecoder after
/// this returns for the live memory mapping to reflect the restored ports.
void RestoreChipsetState(const TTDChipsetState& src, EmulatorState* dst);

} // namespace ttd
