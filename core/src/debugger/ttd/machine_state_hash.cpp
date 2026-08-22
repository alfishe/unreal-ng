//
// machine_state_hash.cpp — see header for design notes.
//

#include "debugger/ttd/machine_state_hash.h"

#include "emulator/cpu/z80.h"
#include "emulator/platform.h"

#include <cstdio>
#include <cstring>

namespace ttd {

/// region <Hash primitives>

// 64-bit FNV-1a parameters (https://datatracker.ietf.org/doc/html/draft-eastlake-fnv).
// Chosen for: deterministic, public-domain, fast on modern CPUs, good avalanche.
namespace {
constexpr uint64_t kFNV1aOffsetBasis = 0xcbf29ce484222325ULL;
constexpr uint64_t kFNV1aPrime       = 0x100000001b3ULL;
} // namespace

uint64_t HashBytes(const uint8_t* data, size_t size)
{
    if (!data || size == 0)
    {
        return 0;
    }
    uint64_t h = kFNV1aOffsetBasis;
    for (size_t i = 0; i < size; ++i)
    {
        h ^= static_cast<uint64_t>(data[i]);
        h *= kFNV1aPrime;
    }
    return h;
}

uint64_t HashCombine(uint64_t seed, const uint8_t* data, size_t size)
{
    // FNV-1a continuation: seed replaces the offset basis, then mix in the new bytes.
    // This is the canonical "continue hashing more data" pattern for FNV-1a.
    if (!data || size == 0)
    {
        return seed;
    }
    uint64_t h = seed ? seed : kFNV1aOffsetBasis;
    for (size_t i = 0; i < size; ++i)
    {
        h ^= static_cast<uint64_t>(data[i]);
        h *= kFNV1aPrime;
    }
    return h;
}

/// endregion </Hash primitives>

/// region <Snapshot capture>

MachineStateSnapshot CaptureSnapshot(const Z80State& cpu,
                                      const EmulatorState& state,
                                      uint64_t ram_digest)
{
    MachineStateSnapshot s;

    // Zero the entire struct first so any padding bytes between fields are
    // deterministic. Without this, byte-wise hashing of the snapshot would
    // pick up uninitialized padding and produce non-reproducible hashes.
    // (Default member initializers zero the named fields but not the padding
    // between them; this memset closes that gap.)
    std::memset(static_cast<void*>(&s), 0, sizeof(s));

    // ---- Z80 architectural registers ----
    s.pc = cpu.pc;
    s.sp = cpu.sp;
    s.af = cpu.af;
    s.bc = cpu.bc;
    s.de = cpu.de;
    s.hl = cpu.hl;
    s.ix = cpu.ix;
    s.iy = cpu.iy;
    s.alt_af = cpu.alt.af;
    s.alt_bc = cpu.alt.bc;
    s.alt_de = cpu.alt.de;
    s.alt_hl = cpu.alt.hl;
    s.i = cpu.i;
    s.r_low = cpu.r_low;
    s.r_hi = cpu.r_hi;
    s.iff1 = cpu.iff1;
    s.iff2 = cpu.iff2;
    s.im = cpu.im;
    s.halted = cpu.halted;
    s.memptr = cpu.memptr;
    s.q = cpu.q;
    s.nmi_in_progress = cpu.nmi_in_progress ? 1 : 0;

    // ---- Port latches (EmulatorState) ----
    s.p7FFD = state.p7FFD;
    s.pFE = state.pFE;
    s.pEFF7 = state.pEFF7;
    s.pBFFD = state.pBFFD;
    s.pFFFD = state.pFFFD;
    s.pDFFD = state.pDFFD;
    s.pFDFD = state.pFDFD;
    s.p1FFD = state.p1FFD;
    s.pFF77 = state.pFF77;
    s.p7EFD = state.p7EFD;
    s.p78FD = state.p78FD;
    s.p7AFD = state.p7AFD;
    s.p7CFD = state.p7CFD;
    s.gmx_config = state.gmx_config;
    s.gmx_magic_shift = state.gmx_magic_shift;
    s.p00 = state.p00;
    s.p80FD = state.p80FD;
    s.border_attr = state.border_attr;

    // ---- Counters ----
    s.t_states = state.t_states;
    s.frame_counter = state.frame_counter;

    // ---- RAM digest (pre-computed by caller) ----
    s.ram_digest = ram_digest;

    return s;
}

/// endregion </Snapshot capture>

/// region <Snapshot hash>

uint64_t HashSnapshot(const MachineStateSnapshot& snapshot)
{
    // Hash the POD-ish struct byte-wise. Default member initializers and
    // padding bytes are zeroed in C++ for value-initialized aggregates, and
    // CaptureSnapshot() writes every field, so the byte image is stable.
    //
    // To make this robust against future struct growth / padding changes,
    // we hash field-by-field via FNV-1a rather than the raw memory image.
    // The cost is small (the struct is ~64 bytes of payload).
    uint64_t h = HashBytes(reinterpret_cast<const uint8_t*>(&snapshot),
                           sizeof(snapshot));
    return h;
}

std::string HashToString(uint64_t hash)
{
    // 16 hex digits, lowercase, zero-padded. Matches the conventional UUID
    // segment formatting and is easy to grep in logs.
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buf, 16);
}

/// endregion </Snapshot hash>

/// region <Hash history>

void HashHistory::Reserve(size_t frames)
{
    hashes.reserve(frames);
    t_states.reserve(frames);
}

void HashHistory::Append(uint64_t hash, uint64_t t_state)
{
    hashes.push_back(hash);
    t_states.push_back(t_state);
}

void HashHistory::Clear()
{
    hashes.clear();
    t_states.clear();
}

size_t HashHistory::Size() const
{
    // hashes.size() and t_states.size() are always kept in lock-step by Append().
    return hashes.size();
}

/// endregion </Hash history>

/// region <Divergence comparator>

DivergenceResult CompareHistories(const HashHistory& expected,
                                  const HashHistory& actual)
{
    DivergenceResult r;
    const size_t n = std::min(expected.Size(), actual.Size());
    r.comparedFrames = n;

    for (size_t i = 0; i < n; ++i)
    {
        if (expected.hashes[i] != actual.hashes[i])
        {
            r.match = false;
            r.firstDivergingFrame = i;
            r.expectedHash = expected.hashes[i];
            r.actualHash = actual.hashes[i];
            r.expectedTState = expected.t_states[i];
            r.actualTState = actual.t_states[i];
            // Length flags are about the overall histories, not the prefix.
            r.expectedLonger = expected.Size() > actual.Size();
            r.actualLonger = actual.Size() > expected.Size();
            return r;
        }
    }

    // Common prefix matches. Length mismatch is still reported, but `match`
    // stays true because the common prefix did not diverge.
    r.match = true;
    r.expectedLonger = expected.Size() > actual.Size();
    r.actualLonger = actual.Size() > expected.Size();
    return r;
}

/// endregion </Divergence comparator>

} // namespace ttd
