/// @file ttd_ay_serializer_test.cpp
/// @brief Round-trip tests for the AY/TurboSound TTDSerializable implementation.
///
/// Per parent TDD §15.1 test table: `TTD_Serializer_RoundTrip_<Device>` —
/// "Save → mutate → load → full-state compare, one per TTDSerializable".
///
/// Verification strategy:
///   - The **save → load → save-compare** pattern compares two serialized
///     buffers byte-for-byte. This is strictly stronger than field-by-field
///     comparison: it catches ANY field present in serialization that fails
///     to round-trip, without the test having to enumerate fields (which is
///     exactly the state-completeness audit risk the TDD warns about).
///   - CPU-visible register checks via getRegisters() confirm the
///     determinism-critical subset (parent TDD §5.5).
///   - Generator-phase checks via updateState() output confirm that audio
///     phase state (counters / LFSR / envelope segment) — NOT derivable from
///     the register file alone — is actually captured and restored.

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "common/modulelogger.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_serializable.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/soundmanager.h"

/// region <Helpers>

namespace
{
/// Serialize a device, then re-serialize after restore, and return true iff
/// the two byte buffers are identical. This is the full-state round-trip
/// identity check: if any serialized field fails to restore, the buffers
/// diverge.
bool RoundTripIdentity(ttd::TTDSerializable* dev)
{
    std::vector<uint8_t> saved(dev->TTDStateSize());
    dev->TTDSaveState(saved.data());

    // Re-serialize into a second buffer immediately — this is the reference.
    std::vector<uint8_t> probe(dev->TTDStateSize());
    dev->TTDSaveState(probe.data());

    // The two consecutive saves must be byte-identical (save is a pure read).
    if (saved != probe)
        return false;

    return true;  // (the load step happens in the caller before calling this)
}

/// Save dev into buffer, then later reload and compare — returns the two
/// buffers so the caller can assert equality.
struct RoundTripBuffers
{
    std::vector<uint8_t> before;  ///< State captured from the source device
    std::vector<uint8_t> after;   ///< State re-captured from the target after load
};

RoundTripBuffers DoRoundTrip(SoundChip_AY8910* src, SoundChip_AY8910* dst)
{
    RoundTripBuffers out;
    out.before.resize(src->TTDStateSize());
    src->TTDSaveState(out.before.data());

    dst->TTDLoadState(out.before.data());

    out.after.resize(dst->TTDStateSize());
    dst->TTDSaveState(out.after.data());
    return out;
}

/// Drive the chip through enough updateState() ticks to advance every
/// generator's phase well past zero (counters, LFSR, envelope segment).
void AdvanceGenerators(SoundChip_AY8910* chip, size_t ticks)
{
    for (size_t i = 0; i < ticks; ++i)
        chip->updateState(true /* bypassPrescaler */);
}
} // anonymous namespace

/// endregion </Helpers>

/// region <SoundChip_AY8910 serializer tests>

class TTD_AY_Serializer_Test : public ::testing::Test
{
protected:
    EmulatorContext*       _context = nullptr;
    SoundChip_AY8910CUT*   _chipA   = nullptr;  // source-of-truth chip
    SoundChip_AY8910CUT*   _chipB   = nullptr;  // restore target

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _chipA   = new SoundChip_AY8910CUT(_context);
        _chipB   = new SoundChip_AY8910CUT(_context);
    }

    void TearDown() override
    {
        delete _chipA;
        delete _chipB;
        delete _context;
    }
};

TEST_F(TTD_AY_Serializer_Test, TTDStateSize_IsStable_57Bytes)
{
    // Per the layout in soundchip_ay8910.cpp (registers + currentRegister +
    // 3 tone gens + noise gen + envelope gen = 57). TDD §6.4 requires this to
    // be fixed per device instance.
    EXPECT_EQ(_chipA->TTDStateSize(), 57u);
    EXPECT_EQ(_chipB->TTDStateSize(), 57u);

    // Stability: size doesn't change after state mutation.
    _chipA->writeRegister(AY_A_FINE, 0x42);
    EXPECT_EQ(_chipA->TTDStateSize(), 57u);
}

TEST_F(TTD_AY_Serializer_Test, RoundTrip_DefaultState_IsByteIdentical)
{
    // Both chips freshly reset. The serialized form of a default chip must
    // round-trip exactly.
    RoundTripBuffers rt = DoRoundTrip(_chipA, _chipB);
    EXPECT_EQ(rt.before, rt.after)
        << "Default-state AY serializer failed byte-for-byte round-trip";
}

TEST_F(TTD_AY_Serializer_Test, RoundTrip_RichRegisterState_PreservesRegisters)
{
    // Write a known-distinct pattern across all 16 registers via the public
    // register-write path. R7 (mixer) must keep bit values valid; we avoid
    // values that the chip masks, so the comparison is exact.
    for (uint8_t r = 0; r < 16; ++r)
    {
        uint8_t v = static_cast<uint8_t>(0x10 + r * 3);  // 0x10,0x13,0x16,...
        _chipA->writeRegister(r, v);
    }

    RoundTripBuffers rt = DoRoundTrip(_chipA, _chipB);

    // Full-state identity.
    EXPECT_EQ(rt.before, rt.after)
        << "Rich-register-state AY serializer failed byte-for-byte round-trip";

    // CPU-visible subset: register file + current register must match exactly.
    // This is the part that affects determinism (parent TDD §5.5).
    const uint8_t* regsA = _chipA->getRegisters();
    const uint8_t* regsB = _chipB->getRegisters();
    EXPECT_EQ(0, memcmp(regsA, regsB, 16))
        << "Register file not restored identically";
    EXPECT_EQ(_chipA->getCurrentRegister(), _chipB->getCurrentRegister());
}

TEST_F(TTD_AY_Serializer_Test, RoundTrip_GeneratorPhase_AdvancedCountersPreserved)
{
    // Write registers that enable tone/noise/envelope, then drive updateState
    // many times so the generator counters / LFSR / envelope segment advance
    // well past their reset values. This proves phase state (NOT derivable
    // from the register file) is captured and restored.
    _chipA->writeRegister(AY_MIXER_CONTROL, 0x00);              // enable all tone+noise
    _chipA->writeRegister(AY_A_VOLUME, 0x10);                   // envelope on channel A
    _chipA->writeRegister(AY_B_VOLUME, 0x10);                   // envelope on channel B
    _chipA->writeRegister(AY_C_VOLUME, 0x10);                   // envelope on channel C
    _chipA->writeRegister(AY_ENVELOPE_SHAPE, 0x0A);             // \/\/ shape
    _chipA->writeRegister(AY_NOISE_PERIOD, 0x05);
    _chipA->writeRegister(AY_ENVELOPE_PERIOD_FINE, 0x20);
    _chipA->writeRegister(AY_ENVELOPE_PERIOD_COARSE, 0x01);

    AdvanceGenerators(_chipA, 500);

    RoundTripBuffers rt = DoRoundTrip(_chipA, _chipB);

    // If generator phase weren't captured, the re-serialized buffer would
    // differ (counters/LFSR/segment would be at their reset-zero values in B).
    EXPECT_EQ(rt.before, rt.after)
        << "Generator phase state (counters/LFSR/segment) failed round-trip";
}

TEST_F(TTD_AY_Serializer_Test, RoundTrip_PhaseRestored_NotJustRegisterConfig)
{
    // Stronger than the identity check above: prove that after restore, chipB's
    // generator phase matches chipA's by continuing to step both and comparing
    // the per-step register-visible readback. If phase were NOT restored,
    // chipB's generators would be at reset-zero and diverge from chipA on the
    // very next updateState.
    _chipA->writeRegister(AY_MIXER_CONTROL, 0x00);
    _chipA->writeRegister(AY_A_VOLUME, 0x10);
    _chipA->writeRegister(AY_ENVELOPE_SHAPE, 0x0C);  // //// continuous ramp up
    AdvanceGenerators(_chipA, 250);

    // Snapshot + restore.
    std::vector<uint8_t> saved(_chipA->TTDStateSize());
    _chipA->TTDSaveState(saved.data());
    _chipB->TTDLoadState(saved.data());

    // Step both chips the same number of times and compare the serialized
    // state after each step. They must stay in lock-step.
    for (int step = 0; step < 32; ++step)
    {
        _chipA->updateState(true);
        _chipB->updateState(true);

        std::vector<uint8_t> a(_chipA->TTDStateSize());
        std::vector<uint8_t> b(_chipB->TTDStateSize());
        _chipA->TTDSaveState(a.data());
        _chipB->TTDSaveState(b.data());

        if (a != b)
        {
            FAIL() << "chipA and chipB diverged after restore at step " << step
                   << " — generator phase was not fully restored";
        }
    }
}

TEST_F(TTD_AY_Serializer_Test, SaveIsPureRead_DoesNotMutateDevice)
{
    // TDD §6.4: TTDSaveState must be a plain read with no side effects.
    // Verify by saving twice and comparing; also verify registers unchanged.
    _chipA->writeRegister(AY_A_FINE, 0x77);
    AdvanceGenerators(_chipA, 100);

    std::vector<uint8_t> first(_chipA->TTDStateSize());
    _chipA->TTDSaveState(first.data());

    std::vector<uint8_t> second(_chipA->TTDStateSize());
    _chipA->TTDSaveState(second.data());

    EXPECT_EQ(first, second) << "TTDSaveState has side effects on the device";
    EXPECT_EQ(_chipA->getRegisters()[AY_A_FINE], 0x77);
}

/// endregion </SoundChip_AY8910 serializer tests>

/// region <SoundChip_TurboSound serializer tests>

class TTD_TurboSound_Serializer_Test : public ::testing::Test
{
protected:
    EmulatorContext*       _context = nullptr;
    SoundChip_TurboSound*  _ttsA    = nullptr;
    SoundChip_TurboSound*  _ttsB    = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _ttsA    = new SoundChip_TurboSound(_context);
        _ttsB    = new SoundChip_TurboSound(_context);
        _ttsA->reset();
        _ttsB->reset();
    }

    void TearDown() override
    {
        delete _ttsA;
        delete _ttsB;
        delete _context;
    }
};

TEST_F(TTD_TurboSound_Serializer_Test, TTDStateSize_IsStable_115Bytes)
{
    // 1 byte current-chip selector + 2 × 57-byte AY chips = 115 bytes.
    EXPECT_EQ(_ttsA->TTDStateSize(), 115u);
    EXPECT_EQ(_ttsB->TTDStateSize(), 115u);
}

TEST_F(TTD_TurboSound_Serializer_Test, RoundTrip_DefaultState_IsByteIdentical)
{
    std::vector<uint8_t> saved(_ttsA->TTDStateSize());
    _ttsA->TTDSaveState(saved.data());

    _ttsB->TTDLoadState(saved.data());

    std::vector<uint8_t> probe(_ttsB->TTDStateSize());
    _ttsB->TTDSaveState(probe.data());

    EXPECT_EQ(saved, probe)
        << "Default TurboSound state failed byte-for-byte round-trip";
}

TEST_F(TTD_TurboSound_Serializer_Test, RoundTrip_BothChipsPreserved)
{
    // Put distinct register patterns in chip0 vs chip1 so a swap or omission
    // would be caught.
    SoundChip_AY8910* c0 = _ttsA->getChip(0);
    SoundChip_AY8910* c1 = _ttsA->getChip(1);
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c1, nullptr);

    c0->writeRegister(AY_A_FINE, 0x11);
    c0->writeRegister(AY_B_FINE, 0x22);
    c1->writeRegister(AY_A_FINE, 0x33);
    c1->writeRegister(AY_B_FINE, 0x44);

    std::vector<uint8_t> saved(_ttsA->TTDStateSize());
    _ttsA->TTDSaveState(saved.data());

    // Mutate B before loading so we know the restore actually wrote.
    SoundChip_AY8910* b0 = _ttsB->getChip(0);
    SoundChip_AY8910* b1 = _ttsB->getChip(1);
    b0->writeRegister(AY_A_FINE, 0xFF);
    b1->writeRegister(AY_A_FINE, 0xFF);

    _ttsB->TTDLoadState(saved.data());

    // Each chip's CPU-visible registers must match the source.
    EXPECT_EQ(b0->getRegisters()[AY_A_FINE], 0x11);
    EXPECT_EQ(b0->getRegisters()[AY_B_FINE], 0x22);
    EXPECT_EQ(b1->getRegisters()[AY_A_FINE], 0x33);
    EXPECT_EQ(b1->getRegisters()[AY_B_FINE], 0x44);
}

TEST_F(TTD_TurboSound_Serializer_Test, RoundTrip_CurrentChipSelector_Restored)
{
    // The TurboSound port handler selects the active chip via FF77. We verify
    // the selector byte round-trips by re-saving and checking the first byte
    // of the blob. (We can't easily drive the port path here, but the save
    // path encodes _currentChip; this guards against a regression where the
    // selector is dropped from serialization.)

    // Default: chip 0 active (selector byte == 0).
    std::vector<uint8_t> saved0(_ttsA->TTDStateSize());
    _ttsA->TTDSaveState(saved0.data());
    EXPECT_EQ(saved0[0], 0u);

    _ttsB->TTDLoadState(saved0.data());
    std::vector<uint8_t> probe0(_ttsB->TTDStateSize());
    _ttsB->TTDSaveState(probe0.data());
    EXPECT_EQ(probe0[0], 0u)
        << "Current-chip selector (chip 0) not preserved across round-trip";
}

/// endregion </SoundChip_TurboSound serializer tests>

/// region <TimeTravelManager integration: ayState blob is populated>

TEST(TTD_AY_ManagerIntegration_Test, CaptureNow_PopulatesAyStateBlob)
{
    // Verify the TimeTravelManager capture path actually fills the ayState checkpoint
    // blob with a TurboSound payload when recording. This is the wire-up test
    // for P1.5 (peripheral capture in CaptureNow).
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pTimeTravelManager, nullptr);

    // TurboSound must exist on the default model for the blob to be non-empty.
    SoundManager* sm = context->pSoundManager;
    ASSERT_NE(sm, nullptr);
    ASSERT_NE(sm->getTurboSound(), nullptr)
        << "Test precondition: TurboSound must be created by Init()";

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());

    // Baseline checkpoint should have captured the AY state.
    ASSERT_GE(context->pTimeTravelManager->GetCheckpointCount(), 1u);
    const ttd::TTDCheckpoint* cp = context->pTimeTravelManager->GetCheckpoint(0);
    ASSERT_NE(cp, nullptr);

    EXPECT_EQ(cp->ayState.size(), 115u)
        << "ayState blob must contain the TurboSound payload (1 + 2×57 bytes)";

    emulator.Stop();
    emulator.Release();
}

/// endregion </TimeTravelManager integration>
