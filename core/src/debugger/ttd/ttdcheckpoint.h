#pragma once

/// @file ttdcheckpoint.h
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
#include <unordered_map>

#include "ttdserializable.h"

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
/// byte-wise into a checkpoint and memcmp'd in tests. Size is 48 bytes.
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

    // Padding at offset 31 (before memptr). Must be a NAMED member: implicit
    // padding is not copied by member-wise copy/move, so checkpoints moved
    // into the timeline keep heap garbage here and the byte-wise divergence
    // oracle hashes mismatch. memset at capture alone cannot fix this.
    uint8_t  reserved0 = 0;

    // ---- Undocumented but observable ----
    uint16_t memptr = 0;        ///< MEMPTR / WZ — affects BIT n,(HL) undocumented flags
    uint8_t  q = 0;             ///< Q register — affects CCF/SCF undocumented flag behavior

    // Padding at offset 35 (before eipos) — see reserved0.
    uint8_t  reserved1 = 0;

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
    // Padding at offset 43 (before halt_cycle) — see reserved0.
    uint8_t  reserved2 = 0;

    uint32_t halt_cycle = 0;   ///< Cycle at which HALT became active
};

// The named reservedN members above must occupy exactly the former implicit
// padding offsets — the byte-wise hash covers the whole struct, so the layout
// must never drift silently.
//
// Two complementary families of check: the absolute offsets pin the layout
// outright, while the relative ones state the actual invariant (each reservedN
// immediately precedes the member whose alignment gap it fills) and catch
// trailing padding, which absolute offsets alone cannot see.
static_assert(sizeof(TTDCpuState) == 48, "TTDCpuState layout must stay stable (hashed byte-wise)");
static_assert(offsetof(TTDCpuState, memptr) == 32, "reserved0 must sit at pad offset 31");
static_assert(offsetof(TTDCpuState, eipos) == 36, "reserved1 must sit at pad offset 35");
static_assert(offsetof(TTDCpuState, halt_cycle) == 44, "reserved2 must sit at pad offset 43");
static_assert(offsetof(TTDCpuState, halt_cycle) + sizeof(uint32_t) == sizeof(TTDCpuState),
              "TTDCpuState has implicit trailing padding - check reserved0/1/2 placement");
static_assert(offsetof(TTDCpuState, memptr) == offsetof(TTDCpuState, reserved0) + 1,
              "TTDCpuState reserved0 does not fill the alignment gap before memptr");
static_assert(offsetof(TTDCpuState, eipos) == offsetof(TTDCpuState, reserved1) + 1,
              "TTDCpuState reserved1 does not fill the alignment gap before eipos");
static_assert(offsetof(TTDCpuState, halt_cycle) == offsetof(TTDCpuState, reserved2) + 1,
              "TTDCpuState reserved2 does not fill the alignment gap before halt_cycle");

/// @brief Chipset state — the port-latch subset of EmulatorState plus
/// counters. Captured at frame boundaries so the restore path can rebuild
/// memory paging by re-running the port decoder (TDD §8.1 step 2b).
///
/// Peripheral state (AY, tape, FDC, Covox) is NOT here — those devices
/// implement TTDSerializable and land in TTDCheckpoint as blobs.
///
/// Extended/model-specific port latches use TTDPeripheralRegistry serializers.
/// This common struct contains only standard Spectrum 128K ports.
struct TTDChipsetState
{
    // ---- Counters ----
    uint64_t t_states = 0;
    uint64_t frame_counter = 0;

    // ---- Standard Spectrum 128K port latches ----
    uint8_t p7FFD = 0;       ///< 128K banking / screen / ROM select
    uint8_t pFE = 0;         ///< Beeper / EAR / border color / mic
    uint8_t pEFF7 = 0;       ///< Beta Disk interface control
    uint8_t pBFFD = 0;       ///< AY-3-8912 register select
    uint8_t pFFFD = 0;       ///< AY-3-8912 data
    uint8_t pFF77 = 0;       ///< TurboSound chip select
    uint8_t border_attr = 0;
    uint8_t flags = 0;       ///< Runtime execution flags (CF_TRDOS etc.)

    // ---- FDC state (common to Beta Disk models) ----
    uint8_t wd_shadow[4] = {0, 0, 0, 0};

    // ---- Video / palette ----
    uint8_t comp_pal[16] = {0};
    uint8_t ulaplus_mode = 0;
    uint8_t ulaplus_reg = 0;
    uint8_t ulaplus_cram[64] = {0};

    /// Explicit tail filler. MUST keep the struct free of implicit padding:
    /// these objects are copied by member-wise assignment (which leaves padding
    /// bytes untouched) and then hashed byte-wise, so any implicit padding
    /// would carry uninitialized garbage into the hash and the divergence
    /// oracle would report phantom mismatches.
    ///
    /// master hit exactly this on the pre-cleanup 176-byte layout (named
    /// reserved3/reserved4 around pFFF7, observed as Chipset[137..139] and
    /// [172..174] diffs). Those members are gone with the extended ports they
    /// guarded; this branch saw the same failure at [114..119] instead.
    uint8_t reserved[10] = {};
};

static_assert(sizeof(TTDChipsetState) == 120, "TTDChipsetState layout must stay stable (hashed byte-wise)");
static_assert(offsetof(TTDChipsetState, reserved) == 110, "reserved must sit at pad offset 110");
static_assert(offsetof(TTDChipsetState, reserved) + sizeof(TTDChipsetState::reserved)
                  == sizeof(TTDChipsetState),
              "TTDChipsetState has implicit trailing padding - resize reserved[]");

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

    // --- Peripheral state (TTDPeripheralRegistry) ---
    /// Map of PeripheralId → serialized state. Only devices actually connected
    /// on the active model appear; everything else costs nothing. Devices
    /// register themselves through TTDPeripheralRegistry, so neither this
    /// struct nor the capture path names any specific device or machine.
    std::unordered_map<uint8_t, std::vector<uint8_t>> peripheralBlobs;

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
