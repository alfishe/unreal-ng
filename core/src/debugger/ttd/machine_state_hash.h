#pragma once
//
// machine_state_hash.h — deterministic fingerprint of the architectural
// machine state at a frame boundary. Used by the TTD divergence oracle
// (parent TDD §5, §15.1) to verify that a restored+replayed execution
// produces bit-identical per-frame state to the original live run.
//
// Sprint 0, Item 0.4 — ships the hash primitive, the snapshot struct,
// the per-frame history collector, and the divergence comparator. The
// corpus (BASIC idle, scroller, AccuracyCoinZX, TR-DOS loader, ...) is
// wired in a later sprint; this header is the foundation that corpus
// tests will build on.
//
// Design constraints:
//   * Deterministic across runs, build configs, host pointer layouts.
//   * Excludes ALL host-side / transient state: MemoryInterface pointers,
//     trace_curs / mem_curs / isDebugMode, decoded opcode, EmulatorState
//     pointer members (tape.play_pointer, vdbase, ...).
//   * Fast enough to call at every frame boundary on a hot path. The
//     RAM digest dominates; everything else is O(1).
//

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Forward declarations to avoid heavy includes in the header.
struct Z80State;
struct EmulatorState;

namespace ttd {

/// region <Hash primitives>

/// 64-bit FNV-1a hash of a byte range.
///
/// Properties:
///   * Same bytes → same hash, across runs / platforms / build configs.
///   * Avalanche: a single-bit change in input flips ~50% of output bits.
///   * Not cryptographic — optimized for speed and determinism only.
///
/// @param data Pointer to the first byte (may be nullptr if size == 0).
/// @param size Number of bytes to hash.
/// @return 64-bit hash. The zero value is reserved for "empty input".
uint64_t HashBytes(const uint8_t* data, size_t size);

/// Combine an existing 64-bit hash with a new byte range.
/// Useful for incrementally building a digest over non-contiguous state.
uint64_t HashCombine(uint64_t seed, const uint8_t* data, size_t size);

/// endregion </Hash primitives>

/// region <Architectural snapshot>

/// Architectural state of the Z80 + standard port latches + counters.
///
/// This is the *observable machine state* at a frame boundary — everything
/// a deterministic re-execution must reproduce. Host-side fields (memory
/// interface pointers, debugger bookkeeping, decoded opcode cache) are
/// deliberately excluded so that two functionally-identical emulator
/// instances produce the same snapshot even if their host-side state
/// (heap addresses, trace window scroll position, debug mode flag) differs.
///
/// Layout note: the struct is intentionally a POD-ish value type so it can
/// be hashed byte-wise, copied, and compared with memcmp. Do NOT add
/// pointers or non-trivial types here.
struct MachineStateSnapshot
{
    // ---- Z80 architectural registers ----
    uint16_t pc = 0;
    uint16_t sp = 0;
    uint16_t af = 0, bc = 0, de = 0, hl = 0;
    uint16_t ix = 0, iy = 0;
    uint16_t alt_af = 0, alt_bc = 0, alt_de = 0, alt_hl = 0;
    uint8_t  i = 0;             // Interrupt vector
    uint8_t  r_low = 0;         // Refresh register (low 7 bits = refresh counter)
    uint8_t  r_hi = 0;          // Undocumented high bit of R (incremented every NMI)
    uint8_t  iff1 = 0;          // Interrupt enable flip-flop 1
    uint8_t  iff2 = 0;          // Interrupt enable flip-flop 2
    uint8_t  im = 0;            // Interrupt mode (0/1/2)
    uint8_t  halted = 0;        // CPU HALT state (0/1)
    uint16_t memptr = 0;        // MEMPTR / WZ — undocumented but observable via flags
    uint8_t  q = 0;             // Q register — affects undocumented CCF/SCF flag behavior
    uint8_t  nmi_in_progress = 0;

    // ---- Port latches (Spectrum / Pentagon / Scorpion / Profi / ATM / GMX / Quorum) ----
    // Hashed as a flat byte block; unused ports are zero in models that don't
    // touch them, which keeps the snapshot stable across model switches *if*
    // the model initializes those bytes to zero on reset.
    uint8_t p7FFD = 0;          // 128K banking / screen select / ROM select
    uint8_t pFE = 0;            // Border / beep / mic / ula
    uint8_t pEFF7 = 0;          // Profi banking
    uint8_t pBFFD = 0;          // AY select
    uint8_t pFFFD = 0;          // AY data
    uint8_t pDFFD = 0;          // Pentagon 512K / Profi banking
    uint8_t pFDFD = 0;          // Profi banking
    uint8_t p1FFD = 0;          // +2A / +3 banking
    uint8_t pFF77 = 0;          // TS / turbo sound
    uint8_t p7EFD = 0;          // GMX
    uint8_t p78FD = 0;          // GMX
    uint8_t p7AFD = 0;          // GMX
    uint8_t p7CFD = 0;          // GMX
    uint8_t gmx_config = 0;
    uint8_t gmx_magic_shift = 0;
    uint8_t p00 = 0;            // Quorum
    uint8_t p80FD = 0;          // Quorum
    uint8_t border_attr = 0;    // Last border color written

    // ---- Counters (frame-aligned) ----
    // Included so the divergence comparator can locate the exact frame at
    // which two runs first differ. Both must be equal across deterministic
    // re-runs of the same workload.
    uint64_t t_states = 0;
    uint64_t frame_counter = 0;

    // ---- RAM digest ----
    // 64-bit FNV-1a over all RAM pages in use. Pre-computed by the caller
    // (typically via HashBytes(memory->RAMBase(), ramBytesInUse)) and passed
    // in via CaptureSnapshot(). Keeping the digest separate from the CPU /
    // port snapshot lets us change the RAM hashing strategy (e.g. per-page
    // digests for cheaper incremental updates) without touching the snapshot
    // struct layout.
    uint64_t ram_digest = 0;
};

/// Capture the architectural snapshot from the live emulator state.
///
/// Reads ONLY architectural fields from @p cpu and @p state. Never touches
/// pointers, debugger bookkeeping, or transient decoding state.
///
/// @param cpu Z80State (architectural registers read; host-side fields skipped).
/// @param state EmulatorState (port latches + counters read; pointers skipped).
/// @param ram_digest Pre-computed digest of all RAM pages in use.
MachineStateSnapshot CaptureSnapshot(const Z80State& cpu,
                                      const EmulatorState& state,
                                      uint64_t ram_digest);

/// Hash a snapshot into a single 64-bit value.
/// Same snapshot bytes → same hash, deterministically.
uint64_t HashSnapshot(const MachineStateSnapshot& snapshot);

/// Format a 64-bit hash as a fixed-width lowercase hex string.
/// Useful for log output, snapshots-on-disk, and stable test fixtures.
std::string HashToString(uint64_t hash);

/// endregion </Architectural snapshot>

/// region <Per-frame history + divergence comparator>

/// A recorded sequence of per-frame hashes (live run or replay).
struct HashHistory
{
    std::vector<uint64_t> hashes;       // One per frame boundary
    std::vector<uint64_t> t_states;     // Frame N's t_state at capture time

    void Reserve(size_t frames);
    void Append(uint64_t hash, uint64_t t_state);
    void Clear();
    size_t Size() const;
};

/// Result of comparing two histories up to their common length.
struct DivergenceResult
{
    bool     match = true;
    size_t   firstDivergingFrame = SIZE_MAX;  // SIZE_MAX when match == true
    uint64_t expectedHash = 0;
    uint64_t actualHash = 0;
    uint64_t expectedTState = 0;
    uint64_t actualTState = 0;
    size_t   comparedFrames = 0;
    bool     expectedLonger = false;    // histories differ in length
    bool     actualLonger = false;
};

/// Compare two histories up to min(expected.Size(), actual.Size()).
/// Returns DivergenceResult with firstDivergingFrame set to the earliest
/// frame at which the hashes differ (SIZE_MAX if they match through the
/// common prefix). Length mismatch is flagged separately — shortening one
/// history does not count as a divergence in the common prefix.
DivergenceResult CompareHistories(const HashHistory& expected,
                                  const HashHistory& actual);

/// endregion </Per-frame history + divergence comparator>

} // namespace ttd
