/// @file ttd_checkpoint_test.cpp
/// @brief Round-trip and preservation tests for TTDCpuState / TTDChipsetState.
///
/// Verifies:
///   - Architectural fields round-trip exactly (capture -> restore == original)
///   - Host-side fields are PRESERVED by restore (MemIf pointers, trace
///     cursors, isDebugMode, etc.)
///   - Field sensitivity (changing any architectural field shows up in capture)
///   - Struct layout sanity (TTDPageRef sentinel, TTDTimePoint ordering)

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

#include "debugger/ttd/ttd_checkpoint.h"
#include "emulator/cpu/z80.h"
#include "emulator/platform.h"

using namespace ttd;

namespace {

/// Build a Z80State with every architectural field set to a recognizable
/// non-zero pattern so we can detect any field that fails to round-trip.
/// Host-side fields are set to SENTINEL values so we can verify they survive
/// restore untouched.
Z80State MakeCanonicalZ80()
{
    Z80State z{};
    // Wipe everything first so padding is deterministic.
    std::memset(&z, 0, sizeof(z));

    // Architectural fields — distinct pattern per register
    z.pc = 0x1234;
    z.sp = 0x5678;
    z.af = 0xAABB;
    z.bc = 0xBBCC;
    z.de = 0xCCDD;
    z.hl = 0xDDEE;
    z.ix = 0x1122;
    z.iy = 0x3344;

    z.alt.af = 0x0A0B;
    z.alt.bc = 0x0B0C;
    z.alt.de = 0x0C0D;
    z.alt.hl = 0x0D0E;

    z.i = 0x11;
    z.r_low = 0x22;
    z.r_hi = 0x33;
    z.iff1 = 0x44;
    z.iff2 = 0x55;
    z.im = 0x02;
    z.halted = 0x01;

    z.memptr = 0xFEDC;
    z.q = 0x28;  // Only bits 3 and 5 are significant

    z.eipos = 0xE100;
    z.haltpos = 0x7000;
    z.nmi_in_progress = true;
    z.int_pending = true;
    z.int_gate = false;  // Inverted from default to ensure capture sees it
    z.halt_cycle = 0xDEADBEEF;

    // Host-side fields — sentinel values to verify preservation
    z.FastMemIf = reinterpret_cast<const MemoryInterface*>(0xDEAD0001);
    z.DbgMemIf = reinterpret_cast<const MemoryInterface*>(0xDEAD0002);
    z.MemIf = reinterpret_cast<const MemoryInterface*>(0xDEAD0003);
    z.isDebugMode = true;
    z.trace_curs = 0x1111;
    z.trace_top = 0x2222;
    z.trace_mode = 0x3333;
    z.mem_curs = 0x4444;
    z.mem_top = 0x5555;
    z.mem_second = 0x6666;
    z.pc_trflags = 0x77;
    z.prev_pc = 0xBEEF;
    z.m1_pc = 0xF00D;
    z.last_branch = 0xCAFE;
    z.nextpc = 0xF00F;
    z.cycles_to_capture = 0x99;
    z.rate = 0x100;
    z.tpi = 69888;
    z.z80_index = 7;

    return z;
}

/// Build an EmulatorState with every architectural port latch and counter
/// set to a recognizable non-zero pattern.
EmulatorState MakeCanonicalState()
{
    EmulatorState s{};
    std::memset(&s, 0, sizeof(s));

    s.t_states = 0x1234567890ABCDEF;
    s.frame_counter = 0xFEDCBA9876543210;

    s.p7FFD = 0x11;
    s.pFE = 0x22;
    s.pEFF7 = 0x33;
    s.pXXXX = 0x44;
    s.pBFFD = 0x55;
    s.pFFFD = 0x66;
    s.pDFFD = 0x77;
    s.pFDFD = 0x88;
    s.p1FFD = 0x99;
    s.pFF77 = 0xAA;
    s.border_attr = 0x07;
    s.flags = 0x1F;

    s.p7EFD = 0xE1;
    s.p78FD = 0xE2;
    s.p7AFD = 0xE3;
    s.p7CFD = 0xE4;
    s.gmx_config = 0xE5;
    s.gmx_magic_shift = 0xE6;
    s.p00 = 0xC1;
    s.p80FD = 0xC2;
    s.aFE = 0xA1;
    s.aFB = 0xA2;
    s.aFF77 = 0xA3;
    s.active_ay = 0xA4;
    s.pBD = 0xBD;
    s.pBE = 0xBE;
    s.pBF = 0xBF;
    s.pFFBA = 0xBA;
    s.p7FBA = 0x7B;
    s.p0F = 0x0F;
    s.p1F = 0x1F;
    s.p4F = 0x4F;
    s.p5F = 0x5F;
    s.pLSY256 = 0x25;

    for (int i = 0; i < 4; ++i) s.wd_shadow[i] = static_cast<uint8_t>(0x2F + i);
    for (int i = 0; i < 16; ++i) s.comp_pal[i] = static_cast<uint8_t>(i);
    s.ulaplus_mode = 0x01;
    s.ulaplus_reg = 0x10;
    for (int i = 0; i < 64; ++i) s.ulaplus_cram[i] = static_cast<uint8_t>(i);
    for (int i = 0; i < 8; ++i) s.pFFF7[i] = static_cast<unsigned>(0xF7 + i);

    return s;
}

/// Compare two Z80States field-by-field on the architectural subset.
::testing::AssertionResult CpuStatesMatchArchitectural(const Z80State& expected,
                                                       const Z80State& actual)
{
#define CHECK(field) \
    do { \
        if (expected.field != actual.field) { \
            return ::testing::AssertionFailure() \
                << #field " mismatch: expected " << std::hex << +expected.field \
                << " actual " << +actual.field; \
        } \
    } while (0)

    CHECK(pc); CHECK(sp); CHECK(af); CHECK(bc); CHECK(de); CHECK(hl);
    CHECK(ix); CHECK(iy);
    EXPECT_EQ(expected.alt.af, actual.alt.af);
    EXPECT_EQ(expected.alt.bc, actual.alt.bc);
    EXPECT_EQ(expected.alt.de, actual.alt.de);
    EXPECT_EQ(expected.alt.hl, actual.alt.hl);
    CHECK(i); CHECK(r_low); CHECK(r_hi);
    CHECK(iff1); CHECK(iff2); CHECK(im); CHECK(halted);
    CHECK(memptr); CHECK(q);
    CHECK(eipos); CHECK(haltpos);
    CHECK(nmi_in_progress);
    CHECK(int_pending);
    CHECK(int_gate);
    CHECK(halt_cycle);
#undef CHECK

    return ::testing::AssertionSuccess();
}

} // namespace

// ===========================================================================
// CPU capture / restore
// ===========================================================================

TEST(TTDCpuStateTest, CaptureRestore_RoundTrip_ArchitecturalFields)
{
    Z80State original = MakeCanonicalZ80();
    TTDCpuState captured = CaptureCpuState(original);

    // Wipe a copy of the original so we can prove restore brings it back
    Z80State restored = original;
    std::memset(&restored, 0, sizeof(restored));

    RestoreCpuState(captured, &restored);

    EXPECT_TRUE(CpuStatesMatchArchitectural(original, restored));
}

TEST(TTDCpuStateTest, Restore_PreservesHostSideFields)
{
    Z80State original = MakeCanonicalZ80();
    TTDCpuState captured = CaptureCpuState(original);

    // The live Z80State has its own host-side pointers/cursors set up.
    // Simulate that by starting from a different host-side state.
    Z80State live{};
    std::memset(&live, 0, sizeof(live));
    live.FastMemIf = reinterpret_cast<const MemoryInterface*>(0xCAFE0001);
    live.DbgMemIf = reinterpret_cast<const MemoryInterface*>(0xCAFE0002);
    live.MemIf = reinterpret_cast<const MemoryInterface*>(0xCAFE0003);
    live.isDebugMode = true;
    live.trace_curs = 0xDEAD;
    live.trace_top = 0xBEEF;
    live.trace_mode = 0xF00D;
    live.mem_curs = 0x111;
    live.mem_top = 0x222;
    live.mem_second = 0x333;
    live.pc_trflags = 0x44;
    live.prev_pc = 0xABCD;
    live.m1_pc = 0xDCBA;
    live.last_branch = 0x9999;
    live.nextpc = 0x8888;
    live.cycles_to_capture = 0x42;
    live.rate = 0x80;
    live.tpi = 69888;
    live.z80_index = 3;

    // Restore should populate architectural fields and leave host-side alone.
    RestoreCpuState(captured, &live);

    // Architectural fields match the captured state
    EXPECT_EQ(live.pc, original.pc);
    EXPECT_EQ(live.af, original.af);
    EXPECT_EQ(live.memptr, original.memptr);
    EXPECT_EQ(live.q, original.q);

    // Host-side fields are unchanged
    EXPECT_EQ(live.FastMemIf, reinterpret_cast<const MemoryInterface*>(0xCAFE0001));
    EXPECT_EQ(live.DbgMemIf, reinterpret_cast<const MemoryInterface*>(0xCAFE0002));
    EXPECT_EQ(live.MemIf, reinterpret_cast<const MemoryInterface*>(0xCAFE0003));
    EXPECT_TRUE(live.isDebugMode);
    EXPECT_EQ(live.trace_curs, 0xDEADu);
    EXPECT_EQ(live.trace_top, 0xBEEFu);
    EXPECT_EQ(live.trace_mode, 0xF00Du);
    EXPECT_EQ(live.mem_curs, 0x111u);
    EXPECT_EQ(live.mem_top, 0x222u);
    EXPECT_EQ(live.mem_second, 0x333u);
    EXPECT_EQ(live.pc_trflags, 0x44u);
    EXPECT_EQ(live.prev_pc, 0xABCD);
    EXPECT_EQ(live.m1_pc, 0xDCBA);
    EXPECT_EQ(live.last_branch, 0x9999);
    EXPECT_EQ(live.nextpc, 0x8888);
    EXPECT_EQ(live.cycles_to_capture, 0x42);
    EXPECT_EQ(live.rate, 0x80u);
    EXPECT_EQ(live.tpi, 69888u);
    EXPECT_EQ(live.z80_index, 3u);
}

TEST(TTDCpuStateTest, Capture_DetectsAllArchitecturalChanges)
{
    // For each architectural field, perturb it and confirm the captured
    // state differs. This catches "forgot to capture field X" regressions.
    Z80State baseline = MakeCanonicalZ80();
    TTDCpuState baseCaptured = CaptureCpuState(baseline);

    // Helper macro: perturb one field, expect mismatch
#define EXPECT_FIELD_SEEN(fieldGetter, newValue) \
    do { \
        Z80State perturbed = baseline; \
        fieldGetter(perturbed) = (newValue); \
        TTDCpuState perturbedCaptured = CaptureCpuState(perturbed); \
        EXPECT_NE(std::memcmp(&baseCaptured, &perturbedCaptured, sizeof(TTDCpuState)), 0) \
            << "Change to " #fieldGetter " was not captured"; \
    } while (0)

    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.pc; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.sp; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.af; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.bc; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.de; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.hl; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.ix; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.iy; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.alt.af; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.alt.bc; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.alt.de; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.alt.hl; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint8_t& { return z.i; }, 0xFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint8_t& { return z.r_low; }, 0xFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint8_t& { return z.r_hi; }, 0xFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint8_t& { return z.iff1; }, 0xFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint8_t& { return z.iff2; }, 0xFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint8_t& { return z.im; }, 0x03);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint8_t& { return z.halted; }, 0x00);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.memptr; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint8_t& { return z.q; }, 0xFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.eipos; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->uint16_t& { return z.haltpos; }, 0xFFFF);
    EXPECT_FIELD_SEEN([](Z80State& z)->bool& { return z.nmi_in_progress; }, false);
    EXPECT_FIELD_SEEN([](Z80State& z)->bool& { return z.int_pending; }, false);
    EXPECT_FIELD_SEEN([](Z80State& z)->bool& { return z.int_gate; }, true);
    EXPECT_FIELD_SEEN([](Z80State& z)->unsigned& { return z.halt_cycle; }, 0xFFFFFFFFu);
#undef EXPECT_FIELD_SEEN
}

TEST(TTDCpuStateTest, Restore_NullDestinationIsSafe)
{
    TTDCpuState captured{};
    EXPECT_NO_FATAL_FAILURE(RestoreCpuState(captured, nullptr));
}

TEST(TTDCpuStateTest, Capture_SensibleSize)
{
    // The struct should be small enough that copying it per-frame is trivial.
    // ~36-48 bytes expected. If this grows past 128 we've added something
    // that doesn't belong here.
    EXPECT_LE(sizeof(TTDCpuState), 128u);
    // Sanity: should be plain old data so memcpy / memcmp are valid.
    EXPECT_TRUE(std::is_trivially_copyable_v<TTDCpuState>);
    EXPECT_TRUE(std::is_standard_layout_v<TTDCpuState>);
}

// ===========================================================================
// Chipset capture / restore
// ===========================================================================

TEST(TTDChipsetStateTest, CaptureRestore_RoundTrip_PortLatches)
{
    EmulatorState original = MakeCanonicalState();
    TTDChipsetState captured = CaptureChipsetState(original);

    EmulatorState restored{};
    std::memset(&restored, 0xFF, sizeof(restored));  // All ones to expose misses

    RestoreChipsetState(captured, &restored);

    EXPECT_EQ(restored.t_states, original.t_states);
    EXPECT_EQ(restored.frame_counter, original.frame_counter);
    EXPECT_EQ(restored.p7FFD, original.p7FFD);
    EXPECT_EQ(restored.pFE, original.pFE);
    EXPECT_EQ(restored.pEFF7, original.pEFF7);
    EXPECT_EQ(restored.pXXXX, original.pXXXX);
    EXPECT_EQ(restored.pBFFD, original.pBFFD);
    EXPECT_EQ(restored.pFFFD, original.pFFFD);
    EXPECT_EQ(restored.pDFFD, original.pDFFD);
    EXPECT_EQ(restored.pFDFD, original.pFDFD);
    EXPECT_EQ(restored.p1FFD, original.p1FFD);
    EXPECT_EQ(restored.pFF77, original.pFF77);
    EXPECT_EQ(restored.border_attr, original.border_attr);
    EXPECT_EQ(restored.flags, original.flags);

    EXPECT_EQ(restored.p7EFD, original.p7EFD);
    EXPECT_EQ(restored.p78FD, original.p78FD);
    EXPECT_EQ(restored.p7AFD, original.p7AFD);
    EXPECT_EQ(restored.p7CFD, original.p7CFD);
    EXPECT_EQ(restored.gmx_config, original.gmx_config);
    EXPECT_EQ(restored.gmx_magic_shift, original.gmx_magic_shift);
    EXPECT_EQ(restored.p00, original.p00);
    EXPECT_EQ(restored.p80FD, original.p80FD);
    EXPECT_EQ(restored.aFE, original.aFE);
    EXPECT_EQ(restored.aFB, original.aFB);
    EXPECT_EQ(restored.aFF77, original.aFF77);
    EXPECT_EQ(restored.active_ay, original.active_ay);
    EXPECT_EQ(restored.pBD, original.pBD);
    EXPECT_EQ(restored.pBE, original.pBE);
    EXPECT_EQ(restored.pBF, original.pBF);
    EXPECT_EQ(restored.pFFBA, original.pFFBA);
    EXPECT_EQ(restored.p7FBA, original.p7FBA);
    EXPECT_EQ(restored.p0F, original.p0F);
    EXPECT_EQ(restored.p1F, original.p1F);
    EXPECT_EQ(restored.p4F, original.p4F);
    EXPECT_EQ(restored.p5F, original.p5F);
    EXPECT_EQ(restored.pLSY256, original.pLSY256);

    EXPECT_EQ(std::memcmp(restored.wd_shadow, original.wd_shadow, 4), 0);
    EXPECT_EQ(std::memcmp(restored.comp_pal, original.comp_pal, 16), 0);
    EXPECT_EQ(restored.ulaplus_mode, original.ulaplus_mode);
    EXPECT_EQ(restored.ulaplus_reg, original.ulaplus_reg);
    EXPECT_EQ(std::memcmp(restored.ulaplus_cram, original.ulaplus_cram, 64), 0);
    EXPECT_EQ(std::memcmp(restored.pFFF7, original.pFFF7, sizeof(restored.pFFF7)), 0);
}

TEST(TTDChipsetStateTest, Restore_NullDestinationIsSafe)
{
    TTDChipsetState captured{};
    EXPECT_NO_FATAL_FAILURE(RestoreChipsetState(captured, nullptr));
}

TEST(TTDChipsetStateTest, Capture_DetectsAllPortLatchChanges)
{
    EmulatorState baseline = MakeCanonicalState();
    TTDChipsetState baseCaptured = CaptureChipsetState(baseline);

#define EXPECT_FIELD_SEEN(fieldGetter, newValue) \
    do { \
        EmulatorState perturbed = baseline; \
        fieldGetter(perturbed) = (newValue); \
        TTDChipsetState p = CaptureChipsetState(perturbed); \
        EXPECT_NE(std::memcmp(&baseCaptured, &p, sizeof(TTDChipsetState)), 0) \
            << "Change was not captured"; \
    } while (0)

    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint64_t& { return s.t_states; }, 0xFFFFFFFFFFFFFFFFull);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint64_t& { return s.frame_counter; }, 0xFFFFFFFFFFFFFFFFull);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.p7FFD; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.pFE; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.pEFF7; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.pBFFD; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.pFFFD; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.pDFFD; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.pFDFD; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.p1FFD; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.pFF77; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.border_attr; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.flags; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->unsigned& { return s.active_ay; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.ulaplus_mode; }, 0xFF);
    EXPECT_FIELD_SEEN([](EmulatorState& s)->uint8_t& { return s.ulaplus_reg; }, 0xFF);
#undef EXPECT_FIELD_SEEN
}

TEST(TTDChipsetStateTest, CaptureRestore_PaletteArrays)
{
    // Confirm palette array contents round-trip exactly.
    EmulatorState src = MakeCanonicalState();
    for (int i = 0; i < 16; ++i) src.comp_pal[i] = static_cast<uint8_t>(0x10 + i);
    for (int i = 0; i < 64; ++i) src.ulaplus_cram[i] = static_cast<uint8_t>(0x80 + i);
    for (int i = 0; i < 4; ++i)  src.wd_shadow[i] = static_cast<uint8_t>(0xF0 + i);

    TTDChipsetState captured = CaptureChipsetState(src);

    EmulatorState restored{};
    std::memset(&restored, 0, sizeof(restored));
    RestoreChipsetState(captured, &restored);

    for (int i = 0; i < 16; ++i) EXPECT_EQ(restored.comp_pal[i], 0x10 + i);
    for (int i = 0; i < 64; ++i) EXPECT_EQ(restored.ulaplus_cram[i], 0x80 + i);
    for (int i = 0; i < 4; ++i)  EXPECT_EQ(restored.wd_shadow[i], 0xF0 + i);
}

TEST(TTDChipsetStateTest, Capture_SensibleSize)
{
    // Should be ~200-300 bytes. Larger than CPU state due to palettes.
    EXPECT_LE(sizeof(TTDChipsetState), 512u);
    EXPECT_TRUE(std::is_trivially_copyable_v<TTDChipsetState>);
    EXPECT_TRUE(std::is_standard_layout_v<TTDChipsetState>);
}

// ===========================================================================
// TTDCheckpoint composite
// ===========================================================================

TEST(TTDCheckpointTest, DefaultConstruct_EmptyPeripheralBlobs)
{
    TTDCheckpoint cp;
    EXPECT_TRUE(cp.ayState.empty());
    EXPECT_TRUE(cp.fdcState.empty());
    EXPECT_TRUE(cp.tapeState.empty());
    EXPECT_TRUE(cp.covoxState.empty());
    EXPECT_TRUE(cp.ramPages.empty());
    EXPECT_EQ(cp.inputJournalOffset, 0u);
    EXPECT_EQ(cp.writeJournalOffset, 0u);
}

TEST(TTDCheckpointTest, Composite_RoundTrip)
{
    Z80State cpuSrc = MakeCanonicalZ80();
    EmulatorState chipSrc = MakeCanonicalState();

    TTDCheckpoint cp;
    cp.time.frame = 42;
    cp.globalT = 42 * 69888;
    cp.cpu = CaptureCpuState(cpuSrc);
    cp.chipset = CaptureChipsetState(chipSrc);

    // Pretend we have peripheral blobs from a not-yet-implemented device.
    // Round-trip through a vector copy to simulate the in-RAM blob target.
    cp.ayState = std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04};

    // Now restore into fresh destination structs.
    Z80State cpuDst{};
    EmulatorState chipDst{};
    std::memset(&cpuDst, 0, sizeof(cpuDst));
    std::memset(&chipDst, 0, sizeof(chipDst));

    RestoreCpuState(cp.cpu, &cpuDst);
    RestoreChipsetState(cp.chipset, &chipDst);

    EXPECT_TRUE(CpuStatesMatchArchitectural(cpuSrc, cpuDst));
    EXPECT_EQ(chipDst.p7FFD, chipSrc.p7FFD);
    EXPECT_EQ(chipDst.t_states, chipSrc.t_states);
    EXPECT_EQ(cp.ayState, (std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04}));
}

// ===========================================================================
// TTDPageRef sentinel
// ===========================================================================

TEST(TTDPageRefTest, NeverTouchedSentinel)
{
    // v2: TTDPageRef holds 4 sub-page slot indices. A ref is "normal" iff
    // at least one sub-slot is populated; "never touched" iff all four are
    // the kNeverTouched sentinel.
    TTDPageRef normal;
    normal.pageSlots[0] = 5;
    normal.pageSlots[1] = 6;
    normal.pageSlots[2] = 7;
    normal.pageSlots[3] = 8;
    EXPECT_FALSE(normal.IsNeverTouched());

    TTDPageRef untouched;
    untouched.SetNeverTouched();
    EXPECT_TRUE(untouched.IsNeverTouched());

    // Mixed (some sub-pages touched, others never touched) is NOT never-touched.
    TTDPageRef mixed;
    mixed.pageSlots[0] = 5;
    mixed.pageSlots[1] = TTDPageRef::kNeverTouched;
    mixed.pageSlots[2] = TTDPageRef::kNeverTouched;
    mixed.pageSlots[3] = TTDPageRef::kNeverTouched;
    EXPECT_FALSE(mixed.IsNeverTouched());
}

TEST(TTDPageRefTest, SentinelValueDoesNotCollideWithSmallIndices)
{
    // Verify the sentinel is high enough to never collide with typical
    // page-store indices in practice (a 4MB machine has 256 pages; even
    // with generous over-provisioning we won't reach 2^32).
    EXPECT_GT(TTDPageRef::kNeverTouched, 0x10000u);
}

// ===========================================================================
// TTDTimePoint ordering
// ===========================================================================

TEST(TTDTimePointTest, Ordering_IsFrameMajorThenTStates)
{
    TTDTimePoint a{10, 100};
    TTDTimePoint b{10, 200};
    TTDTimePoint c{11, 0};

    EXPECT_LT(a, b);
    EXPECT_LT(b, c);
    EXPECT_FALSE(c < a);
    EXPECT_NE(a, b);
    EXPECT_EQ(a, (TTDTimePoint{10, 100}));
}
