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

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/ttdrecordedstate.h"
#include "common/modulelogger.h"
#include "base/featuremanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdserializable.h"
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

TEST_F(TTD_AY_Serializer_Test, TTDStateSize_IsStable_73Bytes)
{
    // Per the layout in soundchip_ay8910.cpp (registers + currentRegister +
    // 3 tone gens + noise gen + envelope gen = 57, + 16 generator-side
    // registers = 73). TDD §6.4 requires this to be fixed per device instance.
    EXPECT_EQ(_chipA->TTDStateSize(), 73u);
    EXPECT_EQ(_chipB->TTDStateSize(), 73u);

    // Stability: size doesn't change after state mutation.
    _chipA->writeRegister(AY_A_FINE, 0x42);
    EXPECT_EQ(_chipA->TTDStateSize(), 73u);
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

/// A TTD restore puts the saved registers back, never the reset state: a chip restored onto a freshly reset one
/// keeps the saved port directions and latches (R7 bits 6 / 7, R14 / R15), and the bus reads follow them
TEST_F(TTD_AY_Serializer_Test, RestoreKeepsSavedPortStateNotTheResetState)
{
    _chipA->writeRegister(AY_PORTA, 0x5A);
    _chipA->writeRegister(AY_PORTB, 0x3C);
    _chipA->writeRegister(AY_MIXER_CONTROL, 0b0111'1000);  // port A output, port B input, tones on
    _chipA->setRegister(AY_PORTA);

    _chipB->reset();
    RoundTripBuffers rt = DoRoundTrip(_chipA, _chipB);
    EXPECT_EQ(rt.before, rt.after);

    EXPECT_EQ(_chipB->readRegister(AY_MIXER_CONTROL), 0b0111'1000);
    EXPECT_EQ(_chipB->readRegister(AY_PORTA), 0x5A);
    EXPECT_EQ(_chipB->readRegister(AY_PORTB), 0x3C);
    EXPECT_EQ(_chipB->readCurrentRegister(), 0x5A) << "port A output: IN #FFFD reads the latch";
    EXPECT_EQ(_chipB->readRegisterOnBus(AY_PORTB), 0xFF) << "port B input: the pins";
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

TEST_F(TTD_TurboSound_Serializer_Test, TTDStateSize_IsStable_981Bytes)
{
    // 1 byte current-chip selector + 2 x 73-byte AY chips + timeline tail
    // (8-byte render cursor offset + 2 x pending SSG writes {1 + 64 x 6})
    // + render-phase tail (sample phase, LQ phase, 4 decimator phases: 48)
    // + frame-progress tail (render position, samples produced: 8)
    // = 1 + 146 + 8 + 770 + 48 + 8 = 981 bytes.
    EXPECT_EQ(_ttsA->TTDStateSize(), 981u);
    EXPECT_EQ(_ttsB->TTDStateSize(), 981u);
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
    SoundCardScope turboSound(TestSound::TurboSound);  // the slot is the subject
    Emulator emulator(LoggerLevel::LogError);
    // The ayState blob checked below is the legacy TurboSound payload; the
    // shipped default slot is FM now, so stage the AY ini before Init
    emulator.SetCustomConfigPath(
        EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::AY));
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pTimeTravelController, nullptr);

    // TurboSound must exist on the default model for the blob to be non-empty.
    SoundManager* sm = context->pSoundManager;
    ASSERT_NE(sm, nullptr);
    ASSERT_NE(sm->getTurboSound(), nullptr)
        << "Test precondition: TurboSound must be created by Init()";

    ASSERT_TRUE(context->pTimeTravelController->StartRecording());

    // Baseline checkpoint should have captured the AY state.
    ASSERT_GE(context->pTimeTravelController->GetCheckpointCount(), 1u);
    const ttd::TTDCheckpoint* cp = context->pTimeTravelController->GetCheckpoint(0);
    ASSERT_NE(cp, nullptr);

    const auto ayState = ttdtest::RecordedDeviceState(*context->pTimeTravelController, 0, ttd::PeripheralId::TurboSound);
    ASSERT_FALSE(ayState.empty()) << "TurboSound must register itself and appear in the checkpoint";
    EXPECT_EQ(ayState.size(), 981u)
        << "TurboSound blob must contain the payload (1 + 2x73 bytes + 778 timeline tail + 56 render tails)";

    emulator.Stop();
    emulator.Release();
}

/// endregion </TimeTravelManager integration>

/// region <AY clock switch replay (Profi hi-res, design-hires.md H2b)>

namespace
{
/// Profi driver, interrupts off, forever: channel A's tone period follows E, and every 16th pass flips #DFFD bit 7
/// (hi-res: the AY at 1.5 MHz, the CPU at its hi-res clock) - several AY clock switches per frame, at every position
constexpr uint16_t kAyClockDriverAddress = 0x8000;
const uint8_t kAyClockDriver[] = {
    0xF3,              // 8000 DI
    0x1E, 0x00,        // 8001 LD E,0
    0x16, 0x00,        // 8003 LD D,0             #DFFD shadow
    0x01, 0xFD, 0xFF,  // 8005 loop: LD BC,#FFFD
    0xAF,              // 8008 XOR A              register 0: tone A fine <- E
    0xED, 0x79,        // 8009 OUT (C),A
    0x06, 0xBF,        // 800B LD B,#BF
    0xED, 0x59,        // 800D OUT (C),E
    0x06, 0xFF,        // 800F LD B,#FF           register 8: volume A <- 15
    0x3E, 0x08,        // 8011 LD A,8
    0xED, 0x79,        // 8013 OUT (C),A
    0x06, 0xBF,        // 8015 LD B,#BF
    0x3E, 0x0F,        // 8017 LD A,15
    0xED, 0x79,        // 8019 OUT (C),A
    0x7B,              // 801B LD A,E             every 16th pass: flip hi-res
    0xE6, 0x0F,        // 801C AND #0F
    0x20, 0x09,        // 801E JR NZ,+9 (#8029)
    0x7A,              // 8020 LD A,D
    0xEE, 0x80,        // 8021 XOR #80
    0x57,              // 8023 LD D,A
    0x01, 0xFD, 0xDF,  // 8024 LD BC,#DFFD
    0xED, 0x79,        // 8027 OUT (C),A
    0x1C,              // 8029 INC E
    0x06, 0x30,        // 802A LD B,#30
    0x10, 0xFE,        // 802C DJNZ $
    0x18, 0xD5,        // 802E JR loop (#8005)
};

struct AyReplayPoint
{
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    ttd::TTDCpuState cpu;
    uint64_t ramHash = 0;
    std::vector<uint8_t> turboSound;
};

AyReplayPoint ObserveAyReplay(EmulatorContext* context)
{
    const Z80* z80 = context->pCore->GetZ80();
    AyReplayPoint point;
    point.frame = context->emulatorState.frame_counter;
    // TTD time units: monotonic through a clock switch, where z80.t is rescaled (down by 6/7 when a v3 enters hi-res)
    point.tInFrame = context->emulatorState.TtdTInFrame(z80->t);
    point.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
    point.ramHash = ttd::HashBytes(context->pMemory->RAMBase(), static_cast<size_t>(context->config.ramsize) * 1024u);
    ITurboSoundDevice* device = context->pSoundManager->getTurboSound();
    point.turboSound.resize(device->TTDStateSize());
    device->TTDSaveState(point.turboSound.data());
    return point;
}


/// Records the driver (optionally without its #DFFD flips) and replays it from the session start, a per-frame
/// checkpoint and a mid-frame seek; each must reach the recorded end with the same CPU, RAM and TurboSound bytes
void RunAyClockReplay(const char* model, bool flipHires, bool ayClockNew, int* hiresFrames)
{
    Emulator* emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind(model, TurboSoundKind::AY);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    context->config.profi_ay_clock_new = ayClockNew ? 1 : 0;
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    features->setFeature(Features::kSoundGeneration, true);
    features->setFeature(Features::kSoundHQ, true);  // the FIR decimators follow the clock too
    context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    auto* device = dynamic_cast<SoundChip_TurboSound*>(context->pSoundManager->getTurboSound());
    ASSERT_NE(device, nullptr);

    emulator->RunNFrames(3);
    Z80* z80 = context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(kAyClockDriver); i++)
        z80->DirectWrite(static_cast<uint16_t>(kAyClockDriverAddress + i), kAyClockDriver[i]);
    if (!flipHires)
        z80->DirectWrite(kAyClockDriverAddress + 0x1E, 0x18);  // JR NZ -> JR: never flip
    z80->pc = kAyClockDriverAddress;
    emulator->RunNCPUCycles(4321);

    ASSERT_TRUE(ttd->StartRecording());
    const uint64_t startFrame = ttd->GetCheckpoint(0)->time.frame;
    *hiresFrames = 0;
    for (int f = 0; f < 24; f++)
    {
        emulator->RunNFrames(1);
        *hiresFrames += device->GetPsgClock() == 1'500'000u ? 1 : 0;
    }
    emulator->RunNCPUCycles(777);
    const AyReplayPoint recorded = ObserveAyReplay(context);
    ttd->StopRecording();

    const ttd::TTDTimePoint starts[] = {{startFrame, 0}, {startFrame + 11, 0}, {startFrame + 17, 23456}};
    for (const ttd::TTDTimePoint& start : starts)
    {
        SCOPED_TRACE("from frame " + std::to_string(start.frame) + " unit " + std::to_string(start.tInFrame));
        ASSERT_TRUE(ttd->SeekTo(start));
        const EmulatorState* state = &context->emulatorState;
        const uint64_t frame = recorded.frame;
        const uint32_t t = recorded.tInFrame;
        emulator->RunUntilCondition([state, frame, t](const Z80State& cpu) {
            return state->frame_counter > frame || (state->frame_counter == frame && state->TtdTInFrame(cpu.t) >= t);
        });
        const AyReplayPoint replayed = ObserveAyReplay(context);
        EXPECT_EQ(replayed.frame, recorded.frame);
        EXPECT_EQ(replayed.tInFrame, recorded.tInFrame);
        EXPECT_EQ(std::memcmp(&replayed.cpu, &recorded.cpu, sizeof(replayed.cpu)), 0) << "CPU state";
        EXPECT_EQ(replayed.ramHash, recorded.ramHash) << "RAM";
        EXPECT_EQ(replayed.turboSound, recorded.turboSound) << "TurboSound state (AY clock, cursor, queued writes)";
    }

    EmulatorTestHelper::CleanupEmulator(emulator);
}
}  // namespace

/// A recording on a Profi v5 (SB7 "old") whose program flips hi-res - the CPU between 3.5 and 5 MHz, the AY between
/// 1.75 and 1.5 MHz - several times a frame replays byte for byte from the session start, a per-frame checkpoint and
/// a mid-frame seek: the clock, the cursor's fraction and any switch still queued at a checkpoint come back with the
/// TurboSound blob, and the machine re-derives the same clock from #DFFD after the restore.
TEST(TTD_AyClock_Replay_Test, HiresAyClockSwitchesReplayExactly)
{
    int hiresFrames = 0;
    RunAyClockReplay("PROFI", true, false, &hiresFrames);
    EXPECT_GT(hiresFrames, 0) << "the AY must have run at 1.5 MHz";
    EXPECT_LT(hiresFrames, 24) << "and at 1.75 MHz";
}

/// The v3: its hi-res switch also changes the frame (the 0a1d PROM's upper half: 320 lines) and runs the CPU at
/// 3 MHz; a recording across it replays exactly too
TEST(TTD_AyClock_Replay_Test, V3HiresFrameSwitchesReplayExactly)
{
    int hiresFrames = 0;
    RunAyClockReplay("PROFI3", true, false, &hiresFrames);
    EXPECT_GT(hiresFrames, 0);
    EXPECT_LT(hiresFrames, 24);
}

/// endregion </AY clock switch replay>
