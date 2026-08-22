// Tests for the machine-state hash and divergence harness.
// Sprint 0, Item 0.4 — parent TDD §5, §15.1.
//
// Verifies:
//   * HashBytes is deterministic and sensitive to single-bit changes.
//   * CaptureSnapshot reads architectural state and excludes host-side state.
//   * HashSnapshot is stable across struct copies (no padding garbage).
//   * HashHistory records and CompareHistories finds the first diverging frame.
//
// These tests deliberately use synthetic Z80State / EmulatorState values
// rather than a live emulator — the hash function must be correct on its
// own before we wire it into the corpus runner.

#include "pch.h"

#include <debugger/ttd/machine_state_hash.h>
#include <emulator/cpu/z80.h>
#include <emulator/platform.h>

#include <cstring>
#include <random>

using namespace ttd;

/// region <HashBytes>

TEST(MachineStateHash_Test, HashBytes_IsDeterministic)
{
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    const uint64_t h1 = HashBytes(data, sizeof(data));
    const uint64_t h2 = HashBytes(data, sizeof(data));
    EXPECT_EQ(h1, h2);
}

TEST(MachineStateHash_Test, HashBytes_EmptyInputReturnsZero)
{
    EXPECT_EQ(HashBytes(nullptr, 0), 0u);
    EXPECT_EQ(HashBytes(reinterpret_cast<const uint8_t*>(""), 0), 0u);
}

TEST(MachineStateHash_Test, HashBytes_SensitiveToSingleBit)
{
    uint8_t a[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78};
    uint8_t b[8];
    std::memcpy(b, a, sizeof(a));
    b[3] ^= 0x01;  // Flip one bit

    const uint64_t ha = HashBytes(a, sizeof(a));
    const uint64_t hb = HashBytes(b, sizeof(b));
    EXPECT_NE(ha, hb) << "Single-bit change must produce a different hash";
}

TEST(MachineStateHash_Test, HashBytes_DifferentLengthsDiffer)
{
    const uint8_t data[] = {0xAA, 0xBB, 0xCC};
    const uint64_t h1 = HashBytes(data, 1);
    const uint64_t h2 = HashBytes(data, 2);
    const uint64_t h3 = HashBytes(data, 3);
    EXPECT_NE(h1, h2);
    EXPECT_NE(h2, h3);
    EXPECT_NE(h1, h3);
}

TEST(MachineStateHash_Test, HashCombine_MatchesContiguousHash)
{
    const uint8_t data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    const uint64_t whole = HashBytes(data, sizeof(data));

    // Splitting the input and using HashCombine must yield the same digest.
    const uint64_t first = HashBytes(data, 3);
    const uint64_t combined = HashCombine(first, data + 3, 3);
    EXPECT_EQ(whole, combined);
}

TEST(MachineStateHash_Test, HashCombine_NilSeedActsAsOffsetBasis)
{
    const uint8_t data[] = {0x11, 0x22, 0x33};
    EXPECT_EQ(HashCombine(0, data, sizeof(data)), HashBytes(data, sizeof(data)));
}

/// endregion </HashBytes>

/// region <Snapshot capture and hashing>

namespace
{

/// Build a Z80State with all fields set to recognizable values, so any field
/// dropped by CaptureSnapshot shows up as a missing-difference in tests.
Z80State MakeCanonicalZ80(uint16_t seed)
{
    Z80State z{};
    z.pc = seed + 0x1000;
    z.sp = seed + 0x2000;
    z.af = seed + 0x0001;
    z.bc = seed + 0x0002;
    z.de = seed + 0x0003;
    z.hl = seed + 0x0004;
    z.ix = seed + 0x0005;
    z.iy = seed + 0x0006;
    z.alt.af = seed + 0x0010;
    z.alt.bc = seed + 0x0011;
    z.alt.de = seed + 0x0012;
    z.alt.hl = seed + 0x0013;
    z.i = static_cast<uint8_t>(seed + 0x40);
    z.r_low = static_cast<uint8_t>(seed + 0x41);
    z.r_hi = static_cast<uint8_t>(seed + 0x42);
    z.iff1 = static_cast<uint8_t>(seed + 0x50);
    z.iff2 = static_cast<uint8_t>(seed + 0x51);
    z.im = static_cast<uint8_t>(seed + 0x52);
    z.halted = static_cast<uint8_t>(seed & 1);
    z.memptr = seed + 0x6000;
    z.q = static_cast<uint8_t>(seed + 0x60);
    z.nmi_in_progress = (seed & 2) != 0;
    return z;
}

EmulatorState MakeCanonicalState(uint16_t seed)
{
    EmulatorState s{};
    s.p7FFD = static_cast<uint8_t>(seed + 0x01);
    s.pFE = static_cast<uint8_t>(seed + 0x02);
    s.pEFF7 = static_cast<uint8_t>(seed + 0x03);
    s.pBFFD = static_cast<uint8_t>(seed + 0x04);
    s.pFFFD = static_cast<uint8_t>(seed + 0x05);
    s.pDFFD = static_cast<uint8_t>(seed + 0x06);
    s.pFDFD = static_cast<uint8_t>(seed + 0x07);
    s.p1FFD = static_cast<uint8_t>(seed + 0x08);
    s.pFF77 = static_cast<uint8_t>(seed + 0x09);
    s.p7EFD = static_cast<uint8_t>(seed + 0x0A);
    s.p78FD = static_cast<uint8_t>(seed + 0x0B);
    s.p7AFD = static_cast<uint8_t>(seed + 0x0C);
    s.p7CFD = static_cast<uint8_t>(seed + 0x0D);
    s.gmx_config = static_cast<uint8_t>(seed + 0x0E);
    s.gmx_magic_shift = static_cast<uint8_t>(seed + 0x0F);
    s.p00 = static_cast<uint8_t>(seed + 0x10);
    s.p80FD = static_cast<uint8_t>(seed + 0x11);
    s.border_attr = static_cast<uint8_t>(seed + 0x12);
    s.t_states = seed * 0x1000ULL;
    s.frame_counter = seed;
    return s;
}

} // namespace

TEST(MachineStateHash_Test, CaptureAndHash_IsDeterministic)
{
    Z80State z = MakeCanonicalZ80(0x1234);
    EmulatorState st = MakeCanonicalState(0x1234);

    const uint64_t ramDigest = HashBytes(reinterpret_cast<const uint8_t*>("ram"), 3);
    auto s1 = CaptureSnapshot(z, st, ramDigest);
    auto s2 = CaptureSnapshot(z, st, ramDigest);

    EXPECT_EQ(HashSnapshot(s1), HashSnapshot(s2));
}

TEST(MachineStateHash_Test, Snapshot_HashToStringLowercaseHex)
{
    // 0xDEADBEEFCAFE is a 48-bit value; zero-padded to 16 hex chars on the left.
    const std::string s = HashToString(0xDEADBEEFCAFEULL);
    EXPECT_EQ(s.length(), 16u);
    EXPECT_EQ(s, "0000deadbeefcafe");

    // Full-width 64-bit value uses all 16 hex chars.
    const std::string full = HashToString(0xFEDCBA9876543210ULL);
    EXPECT_EQ(full, "fedcba9876543210");

    // Zero hashes to all zeros.
    EXPECT_EQ(HashToString(0), "0000000000000000");
}

TEST(MachineStateHash_Test, Capture_RAMDigestPropagates)
{
    Z80State z = MakeCanonicalZ80(0);
    EmulatorState st = MakeCanonicalState(0);
    auto s = CaptureSnapshot(z, st, 0xABCDEF0123456789ULL);
    EXPECT_EQ(s.ram_digest, 0xABCDEF0123456789ULL);
}

/// endregion </Snapshot capture and hashing>

/// region <Snapshot sensitivity to architectural state>

TEST(MachineStateHash_Test, Snapshot_PCChangeProducesDifferentHash)
{
    Z80State z1 = MakeCanonicalZ80(0x1000);
    Z80State z2 = z1;
    z2.pc = z1.pc + 1;

    EmulatorState st = MakeCanonicalState(0x1000);
    const uint64_t ram = 0;

    EXPECT_NE(HashSnapshot(CaptureSnapshot(z1, st, ram)),
              HashSnapshot(CaptureSnapshot(z2, st, ram)));
}

TEST(MachineStateHash_Test, Snapshot_AltRegsChangeProducesDifferentHash)
{
    Z80State z1 = MakeCanonicalZ80(0x1000);
    Z80State z2 = z1;
    z2.alt.af ^= 0x0080;  // Flip a bit in alternate AF

    EmulatorState st = MakeCanonicalState(0x1000);
    const uint64_t ram = 0;

    EXPECT_NE(HashSnapshot(CaptureSnapshot(z1, st, ram)),
              HashSnapshot(CaptureSnapshot(z2, st, ram)));
}

TEST(MachineStateHash_Test, Snapshot_MemptrAndQChangeProducesDifferentHash)
{
    // MEMPTR and Q are undocumented but observable via flags; a correct
    // divergence oracle must catch changes in them.
    Z80State z1 = MakeCanonicalZ80(0x1000);
    Z80State z2 = z1;
    z2.memptr ^= 0x0001;
    Z80State z3 = z1;
    z3.q ^= 0x08;

    EmulatorState st = MakeCanonicalState(0x1000);
    const uint64_t ram = 0;

    const uint64_t h1 = HashSnapshot(CaptureSnapshot(z1, st, ram));
    EXPECT_NE(h1, HashSnapshot(CaptureSnapshot(z2, st, ram)));
    EXPECT_NE(h1, HashSnapshot(CaptureSnapshot(z3, st, ram)));
}

TEST(MachineStateHash_Test, Snapshot_PortLatchChangeProducesDifferentHash)
{
    Z80State z = MakeCanonicalZ80(0x1000);
    EmulatorState st1 = MakeCanonicalState(0x1000);
    EmulatorState st2 = st1;
    st2.p7FFD ^= 0x10;  // Flip RAM bank bit

    const uint64_t ram = 0;
    EXPECT_NE(HashSnapshot(CaptureSnapshot(z, st1, ram)),
              HashSnapshot(CaptureSnapshot(z, st2, ram)));
}

TEST(MachineStateHash_Test, Snapshot_CountersChangeProducesDifferentHash)
{
    Z80State z = MakeCanonicalZ80(0x1000);
    EmulatorState st1 = MakeCanonicalState(0x1000);
    EmulatorState st2 = st1;
    st2.frame_counter += 1;
    st2.t_states += 69888;  // One PAL frame

    const uint64_t ram = 0;
    EXPECT_NE(HashSnapshot(CaptureSnapshot(z, st1, ram)),
              HashSnapshot(CaptureSnapshot(z, st2, ram)));
}

/// endregion </Snapshot sensitivity to architectural state>

/// region <Snapshot excludes host-side state>

TEST(MachineStateHash_Test, Snapshot_HostSideFieldsExcluded)
{
    // Mutating fields that MUST be excluded from the hash should NOT change
    // the snapshot hash: memory interface pointers, debugger bookkeeping,
    // trace cursors, decoded opcode cache, debug mode flag.
    Z80State z1 = MakeCanonicalZ80(0x1000);
    Z80State z2 = z1;

    // Mutate everything host-side. These should be invisible to CaptureSnapshot.
    z2.FastMemIf = reinterpret_cast<const MemoryInterface*>(0xDEADBEEF);
    z2.DbgMemIf  = reinterpret_cast<const MemoryInterface*>(0xCAFEBABE);
    z2.MemIf     = reinterpret_cast<const MemoryInterface*>(0x0000BEEF);
    z2.trace_curs = 99999;
    z2.trace_top = 88888;
    z2.trace_mode = 7;
    z2.mem_curs = 0xABCD;
    z2.mem_top = 0x1234;
    z2.mem_second = 0x5678;
    z2.pc_trflags = 0xFFFFFFFFu;
    z2.isDebugMode = !z1.isDebugMode;
    z2.prev_pc = z1.prev_pc + 1;
    z2.m1_pc = z1.m1_pc + 1;
    z2.nextpc = z1.nextpc + 1;
    z2.last_branch = z1.last_branch + 1;

    EmulatorState st = MakeCanonicalState(0x1000);
    const uint64_t ram = 0;

    EXPECT_EQ(HashSnapshot(CaptureSnapshot(z1, st, ram)),
              HashSnapshot(CaptureSnapshot(z2, st, ram)))
        << "Host-side Z80 fields (MemIf pointers, trace_*, debug flag, "
           "prev_pc/m1_pc/nextpc/last_branch) must be excluded from snapshot";
}

/// endregion </Snapshot excludes host-side state>

/// region <Hash history + divergence comparator>

TEST(MachineStateHash_Test, History_AppendReservesAndClears)
{
    HashHistory h;
    EXPECT_EQ(h.Size(), 0u);

    h.Reserve(16);
    h.Append(0xAAAA, 1000);
    h.Append(0xBBBB, 2000);
    EXPECT_EQ(h.Size(), 2u);
    EXPECT_EQ(h.hashes[1], 0xBBBBu);
    EXPECT_EQ(h.t_states[1], 2000u);

    h.Clear();
    EXPECT_EQ(h.Size(), 0u);
}

TEST(MachineStateHash_Test, CompareHistories_IdenticalHistoriesMatch)
{
    HashHistory a, b;
    for (uint64_t i = 0; i < 5; ++i)
    {
        a.Append(0x1000 + i, 1000 * i);
        b.Append(0x1000 + i, 1000 * i);
    }
    auto r = CompareHistories(a, b);
    EXPECT_TRUE(r.match);
    EXPECT_EQ(r.firstDivergingFrame, SIZE_MAX);
    EXPECT_EQ(r.comparedFrames, 5u);
    EXPECT_FALSE(r.expectedLonger);
    EXPECT_FALSE(r.actualLonger);
}

TEST(MachineStateHash_Test, CompareHistories_FindsFirstDivergingFrame)
{
    HashHistory a, b;
    // Frames 0,1,2 identical
    for (uint64_t i = 0; i < 3; ++i)
    {
        a.Append(0x1000 + i, 1000 * i);
        b.Append(0x1000 + i, 1000 * i);
    }
    // Frame 3 diverges
    a.Append(0xDEAD, 3000);
    b.Append(0xBEEF, 3000);
    // Frame 4 also diverges
    a.Append(0xCAFE, 4000);
    b.Append(0xF00D, 4000);

    auto r = CompareHistories(a, b);
    EXPECT_FALSE(r.match);
    EXPECT_EQ(r.firstDivergingFrame, 3u);
    EXPECT_EQ(r.expectedHash, 0xDEADu);
    EXPECT_EQ(r.actualHash, 0xBEEFu);
    EXPECT_EQ(r.expectedTState, 3000u);
    EXPECT_EQ(r.actualTState, 3000u);
    EXPECT_EQ(r.comparedFrames, 5u);
}

TEST(MachineStateHash_Test, CompareHistories_LengthMismatchFlaggedButNotADivergence)
{
    HashHistory a, b;
    a.Append(0x1000, 1000);
    a.Append(0x1001, 2000);
    a.Append(0x1002, 3000);

    b.Append(0x1000, 1000);
    // b is shorter — the common prefix matches.

    auto r = CompareHistories(a, b);
    EXPECT_TRUE(r.match) << "Common prefix did not diverge";
    EXPECT_TRUE(a.Size() > b.Size());
    EXPECT_TRUE(r.expectedLonger);
    EXPECT_FALSE(r.actualLonger);
    EXPECT_EQ(r.comparedFrames, 1u);
}

TEST(MachineStateHash_Test, CompareHistories_BothEmptyMatch)
{
    HashHistory a, b;
    auto r = CompareHistories(a, b);
    EXPECT_TRUE(r.match);
    EXPECT_EQ(r.comparedFrames, 0u);
}

/// endregion </Hash history + divergence comparator>

/// region <Stress: large random RAM digest>

TEST(MachineStateHash_Test, HashBytes_LargeBufferIsDeterministic)
{
    // 64KB is the smallest realistic RAM page size we'll hash per frame.
    // This is just a smoke test that the hash is stable on a non-trivial input.
    std::vector<uint8_t> buf(65536);
    std::mt19937 rng(12345);
    for (auto& b : buf) b = static_cast<uint8_t>(rng());

    const uint64_t h1 = HashBytes(buf.data(), buf.size());
    const uint64_t h2 = HashBytes(buf.data(), buf.size());
    EXPECT_EQ(h1, h2);

    // Flip one bit and confirm sensitivity.
    buf[30000] ^= 0x80;
    const uint64_t h3 = HashBytes(buf.data(), buf.size());
    EXPECT_NE(h1, h3);
}

/// endregion </Stress: large random RAM digest>
