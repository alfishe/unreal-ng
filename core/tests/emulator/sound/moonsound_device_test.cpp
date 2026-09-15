#ifdef UNREALNG_HAVE_OPL4

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/mainloop.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/soundmanager.h"

/// MoonSound (ZXM-MoonSound / YMF278B / OPL4) device integration tests.
///
/// Construction and wave-ROM loading run against a standard emulator instance:
/// the staged configs/<model>/unreal.ini next to the test binary carries the
/// heritage [ROM] MOONSOUND key, and the staged rom/ directory carries the
/// 2 MiB YRW801 image - exactly the resolution chain a real install uses
/// (working dir -> executable dir -> resources dir). The R6 guarantee
/// (MoonSound enabled must not alter legacy devices) is covered by the full
/// disk/boot suites that run against the same staged config with MoonSound=1
/// (e.g. UdiZvezdnoeBoot_Test.CatThenLoadFiles).
class MoonSoundDevice_Test : public ::testing::Test
{
protected:
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr);
        context = emulator->GetContext();
    }

    void TearDown() override
    {
        if (emulator)
        {
            EmulatorTestHelper::CleanupEmulator(emulator);
            emulator = nullptr;
        }
        MessageCenter::DisposeDefaultMessageCenter();
    }
};

/// Staged config enables the card ([SOUND] MoonSound=1): the device exists
/// and the two mixer sources (D5) are registered with the legacy volume scale
/// ([SOUND] MoonSoundVol=8000 -> 8000/8192).
TEST_F(MoonSoundDevice_Test, StagedConfig_CreatesDeviceAndRegistryEntries)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    ASSERT_TRUE(soundManager->hasMoonSound());

    const AudioDeviceInfo* fm = soundManager->device(AudioSourceType::Moonsound_FM);
    const AudioDeviceInfo* pcm = soundManager->device(AudioSourceType::Moonsound_PCM);
    ASSERT_NE(fm, nullptr);
    ASSERT_NE(pcm, nullptr);

    EXPECT_EQ(fm->name, "MoonSound FM (OPL3)");
    EXPECT_EQ(pcm->name, "MoonSound PCM (wave)");
    EXPECT_FLOAT_EQ(fm->volume, 8000.0f / 8192.0f);
    EXPECT_FLOAT_EQ(pcm->volume, 8000.0f / 8192.0f);
    EXPECT_FALSE(fm->mute);
    EXPECT_FALSE(pcm->mute);
}

/// Wave ROM load (D10 / integration 6.2): the heritage [ROM] MOONSOUND key
/// resolves against the executable directory and the full 2 MiB YRW801 image
/// lands in the wave memory. Loading a real image through the real resolver
/// is the point - a unit-level buffer copy proves nothing about resolution.
TEST_F(MoonSoundDevice_Test, HeritageKey_LoadsFullYrw801Image)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);

    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);

    constexpr size_t kYrw801Size = 2u * 1024u * 1024u;
    EXPECT_EQ(moonsound->waveRomLoadedBytes(), kYrw801Size);
    EXPECT_EQ(moonsound->waveMemory().RomEnd(), kYrw801Size);
}

/// Port protocol (integration 2.1 / 2.3): wave register writes go through the
/// real Z80::out funnel (latch #7E + data #7F) and read back through Z80::in.
/// On a freshly reset machine TR-DOS is not paged, so no legacy device decodes
/// #7F and the card's read value drives the bus (legacy-priority semantics).
/// Register 2 keeps the device-ID bits on reads ((v & 0x1F) | 0x20); register
/// 6 with MA=1 reads wave memory at the 22-bit address counter and the read
/// auto-increments it (verified-on-silicon behaviour pinned in the core TDD).
TEST_F(MoonSoundDevice_Test, WavePorts_WriteAndReadbackThroughRealBus)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);

    // Arm the wave part first: the YMF278B ignores wave register access
    // while NEW2 (FM bank-1 reg 0x105 bit 1) is clear (openMSX-verified).
    cpu->out(0xC6, 0x05);
    cpu->out(0xC7, 0x03);

    // Device-ID register: written 0x15, read back with the ID bits forced
    cpu->out(0x7E, 2);
    cpu->out(0x7F, 0x15);
    cpu->out(0x7E, 2);
    EXPECT_EQ(cpu->in(0x7F), 0x15 | 0x20);

    // Plain register 3 reads back raw
    cpu->out(0x7E, 3);
    cpu->out(0x7F, 0xAB);
    cpu->out(0x7E, 3);
    EXPECT_EQ(cpu->in(0x7F), 0xAB);

    // Memory window: MA is already 1 (bit 0 of the 0x15 written to reg 2).
    // Set the 22-bit address counter to 0 through regs 3/4/5, then read the
    // first two ROM bytes through reg 6 - consecutive reads must auto-increment.
    cpu->out(0x7E, 3);
    cpu->out(0x7F, 0);
    cpu->out(0x7E, 4);
    cpu->out(0x7F, 0);
    cpu->out(0x7E, 5);
    cpu->out(0x7F, 0);

    cpu->out(0x7E, 6);
    EXPECT_EQ(cpu->in(0x7F), moonsound->waveMemory().Read(0));
    EXPECT_EQ(cpu->in(0x7F), moonsound->waveMemory().Read(1));
}

namespace
{

/// Minimal legacy device pinned at one decoded port, standing in for the
/// Beta-128 FDC data mirror at #7F once TR-DOS is armed (the MoonService
/// scenario: a model-decoded device answering the card's shared address).
class LegacyMirrorStub : public PortDevice
{
public:
    uint8_t portDeviceInMethod(uint16_t) override { return 0xA5; }
    void portDeviceOutMethod(uint16_t, uint8_t) override {}
};

} // namespace

/// Armed-card bus claim (the MoonService-verified rule, card author's
/// MoonService v0.3a): while OPL4 NEW is not armed the card leaves #7F to a
/// legacy mirror device registered at the same decoded address - legacy
/// priority keeps its byte (R6). The service's init write (FM2 reg 05 =
/// NEW2|NEW, honored from bank 1 even in OPL3 mode) flips the card to drive
/// #7F; FM1 reg 05 (bank 0) must not arm it, only the wave data port is
/// claimed, and clearing NEW hands the bus back to the mirror.
TEST_F(MoonSoundDevice_Test, SharedBus_ArmedCardOverridesLegacyMirrorAt7F)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    PortDecoder* portDecoder = context->pPortDecoder;
    ASSERT_NE(portDecoder, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);

    // Select wave register 2 so a claimed read returns the device-ID byte.
    cpu->out(0x7E, 2);
    cpu->out(0x7F, 0x15);

    // Open a TR-DOS session so the Pentagon decoder puts the FDC ports on
    // the bus (the Beta-128 gate drops #7F as undecoded while CF_TRDOS is
    // clear). The service runs from TR-DOS with the session open - that is
    // exactly when its #7F reads fight the legacy mirror.
    context->emulatorState.flags |= CF_TRDOS;

    // Borrow the #7F exclusive slot from the FDC data register (its real
    // occupant - WD1793 holds 0x001F/3F/5F/7F/FF) and put a deterministic
    // mirror in its place: the model-decoded legacy device of the scenario.
    portDecoder->UnregisterPortHandler(0x007F);
    LegacyMirrorStub mirror;
    ASSERT_TRUE(portDecoder->RegisterPortHandler(0x007F, &mirror));

    // Unarmed: the legacy mirror keeps the bus byte (R6).
    EXPECT_FALSE(moonsound->portDeviceClaimsRead(0x7F));
    cpu->out(0x7E, 2);
    EXPECT_EQ(cpu->in(0x7F), 0xA5);

    // Bank 0 reg 05 does not arm the card (NEW lives at bank-1 0x105).
    cpu->out(0xC4, 0x05);
    cpu->out(0xC5, 0x03);
    EXPECT_FALSE(moonsound->portDeviceClaimsRead(0x7F));

    // The service's init write: FM2 reg 05 = NEW2|NEW. The card claims #7F
    // and its register byte drives the bus over the mirror. (The reg-2
    // payload must be re-sent after arming: the pre-arm write below was
    // ignored by the NEW2 gate, as on real silicon.)
    cpu->out(0xC6, 0x05);
    cpu->out(0xC7, 0x03);
    EXPECT_TRUE(moonsound->portDeviceClaimsRead(0x7F));
    cpu->out(0x7E, 2);
    cpu->out(0x7F, 0x15);
    EXPECT_EQ(cpu->in(0x7F), 0x15 | 0x20);

    // Low-byte decode regression (MoonService root cause): the Z80 immediate
    // forms `out (n),a` / `in a,(n)` execute with A in the HIGH address byte
    // (op_noprefix), and the card CPLD wires A0..A7 only - the dirty aliases
    // MoonService actually executes (0x027E latch select, 0x107F data, as
    // produced by `ld a,2 / out (#7E),a` and `ld a,#10 / out (#7F),a`) are
    // the card ports exactly like the clean 16-bit forms.
    EXPECT_TRUE(moonsound->portDeviceClaimsRead(0x107F));
    cpu->out(0x027E, 2);
    cpu->out(0x107F, 0x10);
    EXPECT_EQ(cpu->in(0x107F), 0x10 | 0x20);

    // Only read-side bus claims: the wave latch #7E stays a pure observer,
    // the wave data port #7F claims only while NEW2 is armed, and the FM
    // status port #C4 claims unconditionally (the register exists in every
    // arming state and the card drives the bus byte on its own decode -
    // keeps the motherboard #FE keyboard arm from winning the dirty-high-
    // byte status polls, see integration §2.6). Clearing NEW restores the
    // legacy byte.
    EXPECT_FALSE(moonsound->portDeviceClaimsRead(0x7E));
    EXPECT_TRUE(moonsound->portDeviceClaimsRead(0xC4));
    cpu->out(0xC6, 0x05);
    cpu->out(0xC7, 0x00);
    cpu->out(0x7E, 2);
    EXPECT_EQ(cpu->in(0x7F), 0xA5);

    // Hand the borrowed #7F slot back to the FDC and close the session.
    portDecoder->UnregisterPortHandler(0x007F);
    ASSERT_TRUE(context->pBetaDisk != nullptr);
    ASSERT_TRUE(portDecoder->RegisterPortHandler(0x007F, context->pBetaDisk));
    context->emulatorState.flags &= ~CF_TRDOS;
}

/// FM write path (integration 2.1): address/data latch pairs #C4/#C5 and
/// #C6/#C7 reach the chip - proven by a chip-state snapshot diff. A border
/// write (#FE, decoded by the ULA alone) must NOT touch the chip state.
TEST_F(MoonSoundDevice_Test, FmPorts_WritesReachChipState)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);

    const size_t stateSize = moonsound->chip().StateSize();
    ASSERT_GT(stateSize, 0u);
    std::vector<uint8_t> before(stateSize);
    std::vector<uint8_t> after(stateSize);

    // Negative control: a foreign port write leaves the chip state untouched
    moonsound->chip().SaveState(before.data());
    cpu->out(0xFE, 0x02);
    moonsound->chip().SaveState(after.data());
    EXPECT_EQ(std::memcmp(before.data(), after.data(), stateSize), 0)
        << "A #FE border write must not change MoonSound chip state";

    // Bank 1: register 0x01 (WSE) = 0x20
    cpu->out(0xC4, 0x01);
    cpu->out(0xC5, 0x20);
    // Bank 2: register 0x105 = 0x01 (NEW, bit 0 — OPL3 mode; NEW2 is bit 1)
    cpu->out(0xC6, 0x05);
    cpu->out(0xC7, 0x01);

    moonsound->chip().SaveState(after.data());
    EXPECT_NE(std::memcmp(before.data(), after.data(), stateSize), 0)
        << "FM register writes through #C4/#C5/#C6/#C7 must reach the chip";
}

namespace
{

/// Peak |sample| across the first `frames` stereo frames of a registry
/// buffer. Tests scan SAMPLES_PER_FRAME (882): every machine's frame
/// carries at least that many samples, so the region is always within what
/// handleFrameEnd wrote (and never reads a stale tail).
int MaxAbsSample(const int16_t* buffer, size_t frames)
{
    int peak = 0;
    for (size_t i = 0; i < frames * AUDIO_CHANNELS; i++)
    {
        const int magnitude = std::abs(static_cast<int>(buffer[i]));
        peak = std::max(peak, magnitude);
    }
    return peak;
}

/// FM channel 0 key-on through the real ports - the library test suite's
/// reference voice (TL 0, AR 15, block 4): steady near-full-scale tone. The
/// carrier register addresses follow the selected FM backend: the in-tree
/// engine maps the operator families linearly (carrier 0x21/0x41/0x61/0x81,
/// 0xC0 routing 0 = both sides) while YMF262/ymfm use the classic layout
/// (ch0 carrier 0x23/0x43/0x63/0x83, CHA|CHB = both sides).
void KeyOnFmCh0ThroughPorts(Z80* cpu)
{
    const auto fm1 = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0xC4, reg);
        cpu->out(0xC5, value);
    };
    fm1(0x20, 0x01);  // modulator: mult 1 (both maps)
#if defined(OPL4_FM_YMFM)
    fm1(0x23, 0x01);  // classic carrier: mult 1
    fm1(0x40, 0x00);  // mod TL 0
    fm1(0x43, 0x00);  // car TL 0
    fm1(0x60, 0xF0);  // mod AR 15, DR 0
    fm1(0x63, 0xF0);  // car AR 15, DR 0
    fm1(0x80, 0x00);  // mod SL 0, RR 0
    fm1(0x83, 0x00);  // car SL 0, RR 0
#else
    fm1(0x21, 0x01);  // operator 1 mult 1
    fm1(0x40, 0x00);  // operator 0 TL 0
    fm1(0x41, 0x00);  // operator 1 TL 0
    fm1(0x60, 0xF0);  // AR 15, DR 0
    fm1(0x61, 0xF0);
    fm1(0x80, 0x00);  // SL 0, RR 0
    fm1(0x81, 0x00);
#endif
    fm1(0xA0, 0x03);  // fnum low
#if defined(OPL4_FM_YMFM)
    fm1(0xC0, 0x30);  // CHA+CHB: both sides (routing bits include)
#else
    fm1(0xC0, 0x00);  // feedback 0, both outputs (routing bits exclude)
#endif
    fm1(0xB0, 0x33);  // fnum 0x303, block 4, key on
}

} // namespace

/// Render path (integration 4.2 / D5): a keyed FM tone lands in the FM
/// source and only there. Frames run through the real mainloop, so the
/// sample accumulator, the SoundManager lifecycle and the registry buffers
/// behave exactly as in production. Gain staging (5.3): a near-full-scale
/// tone lands at about half scale in the buffer - audible, never above the
/// trim.
TEST_F(MoonSoundDevice_Test, FrameEnd_KeyedFmTone_RendersIntoFmSourceOnly)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);

    KeyOnFmCh0ThroughPorts(cpu);

    for (int frame = 0; frame < 4; frame++)
        mainLoop->RunFrame();

    EXPECT_GT(MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME), 1000);
    EXPECT_LE(MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME), 16500)
        << "headroom trim (5.3) must keep a full-scale tone under half scale";
    EXPECT_EQ(MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME), 0);
}

/// Render path, PCM side (integration 4.2 / D5 / 6.3): a 16-bit tone header
/// and maximum-amplitude samples are uploaded into SRAM through the real
/// memory window (reg 2 MA=1, address through regs 3/4/5, data through
/// reg 6 with auto-increment), then slot 0 is keyed onto that header. The
/// tone lands in the PCM source and only there - together with the FM test
/// this pins the split routing (D5) in both directions.
TEST_F(MoonSoundDevice_Test, FrameEnd_KeyedPcmTone_RendersIntoPcmSourceOnly)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);

    const auto wave = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0x7E, reg);
        cpu->out(0x7F, value);
    };

    // Arm the wave part: NEW2|NEW at FM bank-1 reg 0x105 — without it the
    // chip ignores every wave register write (openMSX-verified).
    cpu->out(0xC6, 0x05);
    cpu->out(0xC7, 0x03);

    // SRAM upload window on (MA = reg 2 bit 0); the header-base bits are
    // only read at the tone fetch, so they can stay 0 during the upload.
    wave(0x02, 0x01);
    // 12-byte tone header at 0x200000: 16-bit samples (bits 2), sample
    // start 0x200100, loop 0, end 4 (stored as its complement 0xFFFC),
    // rewrite bytes for banks 5..9 all zero.
    wave(0x03, 0x20);
    wave(0x04, 0x00);
    wave(0x05, 0x00);
    for (const uint8_t b : {0xA0, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00})
        wave(0x06, b);
    // Eight 16-bit 0x7FFF sample words at 0x200100.
    wave(0x03, 0x20);
    wave(0x04, 0x01);
    wave(0x05, 0x00);
    for (int i = 0; i < 8; i++)
    {
        wave(0x06, 0x7F);
        wave(0x06, 0xFF);
    }
    // Window off, wave-table header base 4 (headers live in SRAM bank 0).
    wave(0x02, 0x10);

    // Key slot 0 onto wave 384 (header base 4 fetches at 0x200000): full
    // level, instant attack, centred pan. Order matters - the wave-low
    // write (0x08) triggers the header fetch that rewrites banks 5..9.
    wave(0x20, 0xFF);  // wave bit 9 + fnum low 7 bits
    wave(0x08, 0x80);  // wave low: 384 -> tone fetch
    wave(0x38, 0x07);  // octave 0, fnum 1023
    wave(0x98, 0xF0);  // AR 15, D1R 0
    wave(0xB0, 0x00);  // DL 0, D2R 0
    wave(0xD8, 0x00);  // RC 0, RR 0
    wave(0x50, 0x01);  // TL 0 with LD: immediate
    wave(0x68, 0x80);  // key on, pan centre

    for (int frame = 0; frame < 4; frame++)
        mainLoop->RunFrame();

    EXPECT_GT(MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME), 1000);
    EXPECT_LE(MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME), 16500)
        << "headroom trim (5.3) must keep a full-scale tone under half scale";
    EXPECT_EQ(MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME), 0);
}

/// Turbo without audio (3.3 / R8 / D3): a suppressed frame renders silence
/// into both sources while the chip core keeps running, so the still-keyed
/// tone is sustaining and renders again the moment audio is wanted. Driven
/// at the device level - the mainloop sequences exactly these calls when
/// turbo has no audio (SoundManager::handleFrameStart forwards, the frame
/// end is skipped).
TEST_F(MoonSoundDevice_Test, SuppressedFrame_RendersSilenceCoreKeepsRunning)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);

    KeyOnFmCh0ThroughPorts(cpu);

    // Baseline: the tone is audible through the normal frame lifecycle.
    for (int frame = 0; frame < 2; frame++)
        mainLoop->RunFrame();
    EXPECT_GT(MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME), 1000);

    // Suppressed frame: silence in both sources (pending audio dropped).
    moonsound->setSynthesisSuppressed(true);
    moonsound->handleFrameStart();
    moonsound->handleFrameEnd(SAMPLES_PER_FRAME);
    EXPECT_EQ(MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME), 0);
    EXPECT_EQ(MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME), 0);

    // The core ran through the suppressed frame (D3): the sustaining tone
    // renders again immediately once audio is wanted.
    moonsound->setSynthesisSuppressed(false);
    moonsound->handleFrameStart();
    moonsound->handleFrameEnd(SAMPLES_PER_FRAME);
    EXPECT_GT(MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME), 1000);
}

/// Save neutrality + restore exactness (TTD 11.1 / 11.3, device level).
/// Phase A runs frames with a TTDSaveState at every boundary and keeps the
/// rendered FM frames; a mid-run checkpoint is kept. Phase B restores that
/// checkpoint and replays the same frames WITHOUT any saves. Identical
/// audio and an identical final hash prove both halves of the Tier A
/// contract: the save is side-effect free, and the restore reproduces the
/// trajectory exactly. Residual delivery audio is drained at every
/// boundary so both phases compare the same clean cadence - the streams
/// are Tier C, not captured, so the checkpoint cannot carry them.
TEST_F(MoonSoundDevice_Test, TTD_SaveNeutralAndRestoreExact_ReplaysIdenticalAudio)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);

    KeyOnFmCh0ThroughPorts(cpu);

    const size_t frameSamples = SAMPLES_PER_FRAME * AUDIO_CHANNELS;
    const auto runFrameCapture = [moonsound, frameSamples](std::vector<int16_t>& audio)
    {
        moonsound->handleFrameStart();
        moonsound->handleFrameEnd(SAMPLES_PER_FRAME);
        // Sink any residual delivery frames so the boundary is clean.
        float sink[256];
        while (moonsound->chip().RenderSplit(sink, sink, 128) > 0)
        {
        }
        audio.insert(audio.end(), moonsound->getFmBuffer(), moonsound->getFmBuffer() + frameSamples);
    };

    std::vector<uint8_t> blob(moonsound->TTDStateSize());
    std::vector<int16_t> audioA;
    for (int i = 0; i < 3; i++)
    {
        runFrameCapture(audioA);
        moonsound->TTDSaveState(blob.data());
    }
    const std::vector<uint8_t> checkpoint = blob;

    // Phase A tail: three more frames WITH saves interleaved.
    for (int i = 0; i < 3; i++)
    {
        runFrameCapture(audioA);
        moonsound->TTDSaveState(blob.data());
    }
    const uint64_t hashWithSaves = moonsound->TTDHashState();

    // Phase B: restore the checkpoint, replay WITHOUT any saves.
    moonsound->TTDLoadState(checkpoint.data());
    std::vector<int16_t> audioB;
    for (int i = 0; i < 3; i++)
        runFrameCapture(audioB);
    const uint64_t hashReplay = moonsound->TTDHashState();

    ASSERT_EQ(audioB.size(), 3 * frameSamples);
    for (int i = 0; i < 3; i++)
    {
        EXPECT_EQ(std::memcmp(audioA.data() + static_cast<size_t>(3 + i) * frameSamples,
                              audioB.data() + static_cast<size_t>(i) * frameSamples,
                              frameSamples * sizeof(int16_t)),
                  0)
            << "replayed frame " << i << " differs from the saved-interleaved run";
    }
    EXPECT_EQ(hashWithSaves, hashReplay);
}

/// Tier A round-trip (TTD 11.2): a scripted state with distinct latch
/// values and a keyed, advanced tone is captured; the live device is
/// clobbered with a full reset; the restore must bring everything back -
/// proven by a fresh capture comparing byte-identical and by the header
/// carrying the latches the next data write will need after the seek.
TEST_F(MoonSoundDevice_Test, TTD_RoundTrip_RestoresLatchesAndChipState)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);

    KeyOnFmCh0ThroughPorts(cpu);
    // Arm NEW2 first so the wave latch write below is accepted (the chip
    // ignores wave register access while NEW2 is clear, openMSX-verified).
    cpu->out(0xC6, 0x05);
    cpu->out(0xC7, 0x03);
    cpu->out(0xC4, 0x21);  // distinct latch states left behind
    cpu->out(0xC6, 0x44);
    cpu->out(0x7E, 0x33);
    // Let the chip state advance past the keying (envelope, timers).
    moonsound->handleFrameStart();
    moonsound->handleFrameEnd(SAMPLES_PER_FRAME);

    const size_t stateSize = moonsound->TTDStateSize();
    EXPECT_EQ(stateSize, sizeof(MoonSoundTTDHeader) + moonsound->chip().StateSize());
    std::vector<uint8_t> saved(stateSize);
    moonsound->TTDSaveState(saved.data());
    const uint64_t hashBefore = moonsound->TTDHashState();

    // Clobber: a full device reset plus zeroed latches.
    moonsound->reset();
    cpu->out(0xC4, 0x00);
    cpu->out(0xC6, 0x00);
    cpu->out(0x7E, 0x00);
    std::vector<uint8_t> clobbered(stateSize);
    moonsound->TTDSaveState(clobbered.data());
    ASSERT_NE(std::memcmp(saved.data(), clobbered.data(), stateSize), 0)
        << "sanity: the clobber must actually change the state";

    // Restore: everything back, byte-identical.
    moonsound->TTDLoadState(saved.data());
    std::vector<uint8_t> restored(stateSize);
    moonsound->TTDSaveState(restored.data());
    EXPECT_EQ(std::memcmp(saved.data(), restored.data(), stateSize), 0);
    EXPECT_EQ(moonsound->TTDHashState(), hashBefore);

    // The latches ride in the header: after the seek, a data write goes to
    // the register the RECORDED latch selected (11.2's latch case).
    const auto* header = reinterpret_cast<const MoonSoundTTDHeader*>(restored.data());
    EXPECT_EQ(header->fmLatch[0], 0x21);
    EXPECT_EQ(header->fmLatch[1], 0x44);
    EXPECT_EQ(header->waveLatch, 0x33);
    EXPECT_EQ(header->layoutVersion, SoundChip_Moonsound::kTtdLayoutVersion);
    EXPECT_EQ(header->headerSize, sizeof(MoonSoundTTDHeader));
}

/// Registry identity and size stability (TTD 11.8): the device reports
/// MoonSound under PeripheralId::MoonSound, and the Tier A size ignores
/// device configuration - a different SRAM size yields the identical blob
/// size, as the interface's stability promise requires.
TEST_F(MoonSoundDevice_Test, TTD_IdentityAndSizeStableAcrossConfigs)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);

    EXPECT_EQ(moonsound->TTDDeviceName(), "MoonSound");
    EXPECT_EQ(moonsound->TTDPeripheralId(), ttd::PeripheralId::MoonSound);

    const size_t sizeStandard = moonsound->TTDStateSize();
    ASSERT_GT(sizeStandard, sizeof(MoonSoundTTDHeader));

    // A second device with a different SRAM size, constructed standalone
    // (never attached to ports, never registered anywhere).
    unsigned& ramSizeKb = context->config.moonsound.ramSizeKb;
    const unsigned ramSizeKbOriginal = ramSizeKb;
    ramSizeKb = 640;
    SoundChip_Moonsound* other = new SoundChip_Moonsound(context, 44100);
    EXPECT_EQ(other->TTDStateSize(), sizeStandard);
    EXPECT_EQ(other->TTDPeripheralId(), ttd::PeripheralId::MoonSound);
    delete other;
    ramSizeKb = ramSizeKbOriginal;
}

#endif  // UNREALNG_HAVE_OPL4
