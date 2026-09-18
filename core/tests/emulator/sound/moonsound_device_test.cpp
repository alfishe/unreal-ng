#ifdef UNREALNG_HAVE_OPL4

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "common/sound/filters/masterlimiter.h"
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

    // Register 3 is the address counter's A21..A16 byte: only the low 6
    // bits latch, bits 7-6 always read back 0 (hardware-verified, PoC
    // MemoryAccessSweep).
    cpu->out(0x7E, 3);
    cpu->out(0x7F, 0xAB);
    cpu->out(0x7E, 3);
    EXPECT_EQ(cpu->in(0x7F), 0xAB & 0x3F);

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

/// FM channel `ch` of bank 0 keyed at unity: TL 0, AR 15 (zero-time attack),
/// steady near-full-scale tone - the parameterised form of the reference
/// voice. fnum gets a per-channel offset so a multi-channel key-on does not
/// sum N phase-locked copies. Both FM backends use the classic YMF262
/// operator layout (modulator 0x20 + ch%3 + 8*(ch/3), carrier mod+3;
/// 0xC0 routing CHA|CHB = both sides) - the same formula the bisect tests
/// and the engine's own decode use.
void KeyOnFmChannelThroughPorts(Z80* cpu, int ch)
{
    const auto fm1 = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0xC4, reg);
        cpu->out(0xC5, value);
    };
    const uint8_t mod = static_cast<uint8_t>(0x20 + ch % 3 + 8 * (ch / 3));
    const uint8_t car = static_cast<uint8_t>(mod + 3);
    fm1(mod, 0x01);      // modulator: mult 1
    fm1(car, 0x01);      // carrier: mult 1
    fm1(mod + 0x20, 0x00);  // mod TL 0
    fm1(car + 0x20, 0x00);  // car TL 0
    fm1(mod + 0x40, 0xF0);  // mod AR 15, DR 0
    fm1(car + 0x40, 0xF0);  // car AR 15, DR 0
    fm1(mod + 0x60, 0x00);  // mod SL 0, RR 0
    fm1(car + 0x60, 0x00);  // car SL 0, RR 0
    fm1(0xA0 + ch, static_cast<uint8_t>(0x03 + ch));  // fnum low
    fm1(0xC0 + ch, 0x30);  // CHA+CHB: both sides (routing bits include)
    fm1(0xB0 + ch, 0x33);  // fnum high 3, block 4, key on
}

/// FM channel 0 key-on through the real ports - the library test suite's
/// reference voice (TL 0, AR 15, block 4): steady near-full-scale tone.
void KeyOnFmCh0ThroughPorts(Z80* cpu)
{
    KeyOnFmChannelThroughPorts(cpu, 0);
}

/// Arm the wave part (NEW2|NEW at FM bank-1 reg 0x105) and upload a
/// zero-DC full-scale square tone into SRAM through the real memory window:
/// 12-byte tone header at 0x200000 (16-bit samples, start 0x200100, loop 0,
/// end 8 stored as its complement 0xFFF8) and the eight-word loop of 4x +FS
/// then 4x -FS. Zero DC matters for master-mix tests: the master DC blocker
/// (~5 Hz) would eat a DC loop within a frame. Wave-table header base 4 so
/// wave number 384 fetches the uploaded header.
void UploadSquareToneThroughPorts(Z80* cpu)
{
    const auto wave = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0x7E, reg);
        cpu->out(0x7F, value);
    };

    cpu->out(0xC6, 0x05);
    cpu->out(0xC7, 0x03);

    wave(0x02, 0x01);  // SRAM upload window on (MA = reg 2 bit 0)
    wave(0x03, 0x20);
    wave(0x04, 0x00);
    wave(0x05, 0x00);
    for (const uint8_t b : {0xA0, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00})
        wave(0x06, b);
    wave(0x03, 0x20);
    wave(0x04, 0x01);
    wave(0x05, 0x00);
    for (int i = 0; i < 4; i++)
    {
        wave(0x06, 0x7F);
        wave(0x06, 0xFF);
    }
    for (int i = 0; i < 4; i++)
    {
        wave(0x06, 0x80);
        wave(0x06, 0x01);
    }
    wave(0x02, 0x10);  // window off, wave-table header base 4
}

/// Key PCM slot `slot` (0-23) onto wave 384 at unity: full level (TL 0 with
/// LD), zero-time attack (AR 15), centred pan. The register file is
/// interleaved - reg = group base + slot number (the chip decodes group as
/// (reg-8)/24 and slot as (reg-8)%24), NOT slot*8 + offset. Register values
/// mirror the reference voice of the PCM-only render test; order matters -
/// the wave-low write triggers the header fetch.
void KeyOnPcmSlotThroughPorts(Z80* cpu, int slot)
{
    const auto wave = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0x7E, reg);
        cpu->out(0x7F, value);
    };
    wave(0x20 + slot, 0xFF);  // wave bit 9 + fnum low 7 bits
    wave(0x08 + slot, 0x80);  // wave low: 384 -> tone fetch
    wave(0x38 + slot, 0x07);  // octave 0, fnum 1023
    wave(0x98 + slot, 0xF0);  // AR 15, D1R 0
    wave(0xB0 + slot, 0x00);  // DL 0, D2R 0
    wave(0xC8 + slot, 0x00);  // RC 0, RR 0 (offset 8 = 0xC8 + slot, not 0xD8)
    wave(0x50 + slot, 0x01);  // TL 0 with LD: immediate
    wave(0x68 + slot, 0x80);  // key on, pan centre
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

/// Gain staging, both engines simultaneously (integration 5.2/5.3): a
/// full-scale FM voice and a full-scale PCM slot keyed together, played
/// through the real frame lifecycle into the wide mix bus. With the chip's
/// reset block mix (FM -9 dB, PCM unity) the summed master stays inside the
/// limiter's linear region - nominal material never engages the compressor -
/// and each source stays under the -6 dB headroom trim on its own.
TEST_F(MoonSoundDevice_Test, FrameEnd_FmAndPcmFullScale_DefaultChipMix_StaysInLimiterLinearRegion)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);

    KeyOnFmChannelThroughPorts(cpu, 0);
    UploadSquareToneThroughPorts(cpu);
    KeyOnPcmSlotThroughPorts(cpu, 0);

    for (int frame = 0; frame < 4; frame++)
        mainLoop->RunFrame();

    const int fmPeak = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    const int pcmPeak = MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME);
    const int masterPeak = MaxAbsSample(soundManager->deviceBuffer(AudioSourceType::MasterMix), SAMPLES_PER_FRAME);

    EXPECT_GT(fmPeak, 1000);
    EXPECT_LE(fmPeak, 16500) << "FM source must stay under the headroom trim (5.3)";
    EXPECT_GT(pcmPeak, 1000);
    EXPECT_LE(pcmPeak, 16500) << "PCM source must stay under the headroom trim (5.3)";
    // The recalibrated FM block (PoC modulator-depth fix) renders a full FM
    // voice at roughly a quarter of the rail through the default -9 dB mix,
    // so the master sum is PCM-dominated: PCM alone cannot reach this bar.
    EXPECT_GT(masterPeak, 9000) << "both full-scale sources must be audible in the master sum";
    EXPECT_LE(masterPeak, static_cast<int>(MasterLimiter::KNEE_LINEAR))
        << "chip-default block mix keeps a full-scale FM+PCM sum in the limiter's linear region";
}

/// Gain staging, worst legal chip mix: block-mix register 0xF8 pushed to
/// unity (0 dB - the maximum the chip itself allows) with both engines at
/// full scale. The recalibrated single-voice FM level leaves the sum below
/// the limiter knee (that region is exercised by the all-voices-maxed test
/// below); unity must still beat the default -9 dB mix, and the soft curve
/// must hold the master under its asymptotic ceiling: no hard clipping
/// anywhere, neither in the sources nor in the quantised master.
TEST_F(MoonSoundDevice_Test, FrameEnd_FmAndPcmFullScale_UnityChipMix_MasterStaysUnderSoftCeiling)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);

    UploadSquareToneThroughPorts(cpu);
    // FM block mix L=R=unity (0 dB) - the loudest setting the chip permits.
    // F8/F9 live in the wave register file, so they go through #7E/#7F with
    // the card armed (the upload helper did the arming).
    cpu->out(0x7E, 0xF8);
    cpu->out(0x7F, 0x00);
    KeyOnFmChannelThroughPorts(cpu, 0);
    KeyOnPcmSlotThroughPorts(cpu, 0);

    for (int frame = 0; frame < 4; frame++)
        mainLoop->RunFrame();

    const int fmPeak = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    const int pcmPeak = MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME);
    const int masterPeak = MaxAbsSample(soundManager->deviceBuffer(AudioSourceType::MasterMix), SAMPLES_PER_FRAME);

    EXPECT_GT(fmPeak, 1000);
    EXPECT_LE(fmPeak, 16500) << "FM source must stay under the headroom trim (5.3)";
    EXPECT_GT(pcmPeak, 1000);
    EXPECT_LE(pcmPeak, 16500) << "PCM source must stay under the headroom trim (5.3)";
    EXPECT_GT(masterPeak, 13000)
        << "unity chip mix must beat the default-mix master by the +9 dB block-mix lift";
    EXPECT_LE(masterPeak, static_cast<int>(MasterLimiter::CEILING))
        << "the soft limiter ceiling must hold - the master must never hard-clip";
}

/// Gain staging, maximum abuse: all nine bank-0 FM channels and all 24 PCM
/// slots keyed at TL 0 with the FM block mix at unity. The group sums rail
/// inside the chip (the authentic 16-bit DAC boundary, Clamp16 in the
/// library), so both sources still arrive at the mixer under the trim and
/// the master still cannot clip - over-gain is structurally impossible.
TEST_F(MoonSoundDevice_Test, FrameEnd_AllFmChannelsAndPcmSlotsMaxed_MasterNeverClips)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);

    UploadSquareToneThroughPorts(cpu);
    cpu->out(0x7E, 0xF8);
    cpu->out(0x7F, 0x00);
    for (int ch = 0; ch <= 8; ch++)
        KeyOnFmChannelThroughPorts(cpu, ch);
    for (int slot = 0; slot < 24; slot++)
        KeyOnPcmSlotThroughPorts(cpu, slot);

    for (int frame = 0; frame < 4; frame++)
        mainLoop->RunFrame();

    const int fmPeak = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    const int pcmPeak = MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME);
    const int masterPeak = MaxAbsSample(soundManager->deviceBuffer(AudioSourceType::MasterMix), SAMPLES_PER_FRAME);

    EXPECT_GT(fmPeak, 1000);
    EXPECT_LE(fmPeak, 32768)
        << "the FM group tops out at the chip's 16-bit DAC boundary; the -6 dB device"
           " trim lives in the mixer gain, not the source buffer (5.3)";
    EXPECT_GT(pcmPeak, 1000);
    EXPECT_GT(pcmPeak, 30000)
        << "24 maximised slots must rail the PCM group at the 16-bit DAC boundary";
    EXPECT_GT(masterPeak, static_cast<int>(MasterLimiter::KNEE_LINEAR));
    EXPECT_LE(masterPeak, static_cast<int>(MasterLimiter::CEILING))
        << "even with every voice maximised the master must stay under the soft ceiling";
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

// ===========================================================================
// Conformance canaries (core-tdd 12.2): thin per-field rows promoted from
// the PoC sweep families (tools/poc/015-opl4-synthesis/tests/opl4sweep.cpp)
// to the device level - one register behaviour each, asserted on audio
// rendered through the real port/bus/frame path. The exact per-step numbers
// and full grids stay in the PoC suite; these pin the same semantics where
// the emulator actually uses them.
// ===========================================================================
namespace
{

/// Register bases of FM channel 0's two operators in the 0x20 family; the
/// other families hang off the same offsets the key-on helper uses
/// (+0x20 total level, +0x40 attack/decay, +0x60 sustain/release, +0xC0
/// waveform select). Both FM backends use the classic YMF262 layout, so
/// the carrier is 0x23 exactly like KeyOnFmChannelThroughPorts.
constexpr uint8_t kFmCh0Mod = 0x20;
constexpr uint8_t kFmCh0Car = 0x23;
constexpr uint8_t kFmModTl = static_cast<uint8_t>(kFmCh0Mod + 0x20);
constexpr uint8_t kFmCarTl = static_cast<uint8_t>(kFmCh0Car + 0x20);
constexpr uint8_t kFmCarSlRr = static_cast<uint8_t>(kFmCh0Car + 0x60);
constexpr uint8_t kFmModSlRr = static_cast<uint8_t>(kFmCh0Mod + 0x60);
constexpr uint8_t kFmCarWs = static_cast<uint8_t>(kFmCh0Car + 0xC0);

/// Peak, minimum and DC of one stereo side across the first `frames`
/// frames - the pan and waveform canaries need per-side and DC numbers the
/// both-sides MaxAbsSample cannot give.
struct SideStats
{
    int peak = 0;
    int lo = 0;
    double mean = 0.0;
};

SideStats ScanSide(const int16_t* buffer, size_t frames, int channel)
{
    SideStats s;
    long long sum = 0;
    for (size_t i = 0; i < frames; i++)
    {
        const int v = static_cast<int>(buffer[i * AUDIO_CHANNELS + channel]);
        s.peak = std::max(s.peak, std::abs(v));
        s.lo = std::min(s.lo, v);
        sum += v;
    }
    s.mean = frames > 0 ? static_cast<double>(sum) / static_cast<double>(frames) : 0.0;
    return s;
}

/// The device publishes one frame of already-synthesised audio per RunFrame:
/// register writes issued between frames reach the rendered buffers only
/// once the in-flight audio has drained (observed: FM ~2 frames, PCM ~1).
/// The canaries flush that pipe after every write burst before measuring.
void FlushAudioPipe(MainLoop_CUT* mainLoop, int frames)
{
    for (int i = 0; i < frames; i++)
        mainLoop->RunFrame();
}

} // namespace

/// Canary (FmTlLadderSweep row): carrier total level is live-writable
/// during key-on and steps down at the datasheet's 0.75 dB per unit (16
/// units = -12 dB). The modulator sits at the field maximum so the ladder
/// measures a near-pure carrier; the reset-default block mix is a constant
/// gain the ratio bands absorb.
TEST_F(MoonSoundDevice_Test, Canary_FmTlLadder_AttenuatesCarrierInSpecSteps)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);
    const auto fm1 = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0xC4, reg);
        cpu->out(0xC5, value);
    };

    KeyOnFmCh0ThroughPorts(cpu);
    fm1(kFmModTl, 0x3F);  // modulator TL max: near-pure carrier
    FlushAudioPipe(mainLoop, 3); // attack settle + write drain

    const uint8_t tlRow[4] = {0x00, 0x10, 0x20, 0x30};
    int peaks[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++)
    {
        fm1(kFmCarTl, tlRow[i]);
        FlushAudioPipe(mainLoop, 2); // FM writes drain after ~2 rendered frames
        mainLoop->RunFrame();
        peaks[i] = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    }

    ASSERT_GT(peaks[0], 1000);
    for (int i = 0; i < 3; i++)
    {
        EXPECT_LT(peaks[i + 1], peaks[i]) << "TL " << static_cast<int>(tlRow[i + 1])
                                          << " must attenuate below TL " << static_cast<int>(tlRow[i]);
        // -12 dB nominal per 16-unit step; generous +-6 dB band around it
        EXPECT_LT(peaks[i + 1] * 100, peaks[i] * 45) << "step " << i << " is steeper than -18 dB";
        EXPECT_GT(peaks[i + 1] * 100, peaks[i] * 12) << "step " << i << " is shallower than -6 dB";
    }
    EXPECT_GT(peaks[3], 15) << "-36 dB of a full-scale tone must stay off the int16 floor";
}

/// Canary (FmEnvStageSweep row): AR 15 attack is effectively instant, the
/// sustain stage (DR 0 at SL 0) holds a flat plateau, and a key-off with
/// RR 15 releases to silence within a frame - all through the real frame
/// lifecycle on a keyed voice.
TEST_F(MoonSoundDevice_Test, Canary_FmEnvelopeStages_AttackSustainAndRelease)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);
    const auto fm1 = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0xC4, reg);
        cpu->out(0xC5, value);
    };

    KeyOnFmCh0ThroughPorts(cpu);
    fm1(kFmModTl, 0x3F); // near-pure carrier
    FlushAudioPipe(mainLoop, 3);

    mainLoop->RunFrame();
    const int attack = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    mainLoop->RunFrame();
    const int sustain0 = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    mainLoop->RunFrame();
    const int sustain1 = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);

    EXPECT_GT(attack, sustain1 / 2) << "AR 15 attack must be effectively instant";
    ASSERT_GT(sustain0, 1000);
    EXPECT_GT(sustain0 * 100, sustain1 * 85) << "sustain plateau droops more than 15%";
    EXPECT_LT(sustain0 * 100, sustain1 * 118) << "sustain plateau rises more than 18%";

    // Fast release on both operators, then key off (fnum/block preserved).
    fm1(kFmModSlRr, 0x0F);
    fm1(kFmCarSlRr, 0x0F);
    fm1(0xB0, 0x13);
    FlushAudioPipe(mainLoop, 2); // drain the still-keyed audio first
    mainLoop->RunFrame();
    const int release0 = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    mainLoop->RunFrame();
    const int release1 = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    EXPECT_LT(release0 * 20, sustain1) << "RR 15 release must be effectively instant";
    EXPECT_LT(release1 * 100, sustain1) << "the voice must be gone one frame later";
}

/// Canary (FmWaveformSweep row): waveform select on the carrier - the sine
/// is bipolar with near-zero DC, while the half sine and the full-wave
/// rectified sine are unipolar with DC at 1/pi resp. 2/pi of the peak.
/// WSE (bank 0 reg 0x01 bit 5) is the documented enable.
TEST_F(MoonSoundDevice_Test, Canary_FmWaveformSelect_SineBipolarRectifiedUnipolar)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);
    const auto fm1 = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0xC4, reg);
        cpu->out(0xC5, value);
    };

    cpu->out(0xC4, 0x01);
    cpu->out(0xC5, 0x20); // WSE: waveform select enable
    KeyOnFmCh0ThroughPorts(cpu);
    fm1(kFmModTl, 0x3F); // near-pure carrier
    FlushAudioPipe(mainLoop, 3); // settle

    SideStats st[3];
    const uint8_t wsRow[3] = {0, 1, 2}; // sine, half sine, full-wave rectified
    for (int i = 0; i < 3; i++)
    {
        fm1(kFmCarWs, wsRow[i]);
        FlushAudioPipe(mainLoop, 2);
        mainLoop->RunFrame();
        st[i] = ScanSide(moonsound->getFmBuffer(), SAMPLES_PER_FRAME, 0);
    }

    // ws 0: bipolar symmetric, no DC
    ASSERT_GT(st[0].peak, 1000);
    EXPECT_LT(std::fabs(st[0].mean) * 10, st[0].peak) << "sine must be DC-free";
    EXPECT_LT(st[0].lo * 5, -st[0].peak * 3) << "sine must swing symmetrically negative";
    // ws 1: positive half sine - unipolar, DC above 1/5 of the peak
    ASSERT_GT(st[1].peak, 200);
    EXPECT_GT(st[1].lo * 10, -st[1].peak) << "half sine must not go negative";
    EXPECT_GT(st[1].mean * 5, st[1].peak) << "half sine DC is 1/pi of the peak";
    // ws 2: full-wave rectified - unipolar with well above the half sine's DC
    ASSERT_GT(st[2].peak, 200);
    EXPECT_GT(st[2].lo * 10, -st[2].peak) << "rectified sine must not go negative";
    EXPECT_GT(st[2].mean * 20, st[2].peak * 9) << "rectified sine DC is 2/pi of the peak";
    EXPECT_GT(st[2].mean * 2, st[1].mean * 3) << "rectified DC must double the half-sine DC";
}

/// Canary (PcmLoopEdgeMatrix row, the E = 0 corner): with the loop end
/// stored as its complement 0 the engine's documented openMSX-model
/// behaviour is a 64 KiB one-shot - the wrap never fires and the voice
/// plays past the uploaded data into silence, while a normal complement
/// end keeps wrapping (control arm). The core PCM block is the in-tree
/// engine either way; ymfm's wrap-every-step decode of this corner is the
/// known divergence recorded in the cosim tier.
TEST_F(MoonSoundDevice_Test, Canary_PcmLoopEndZero_PlaysPastDataWithoutWrap)
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

    // Control arm: the uploaded tone (end complement 8) wraps forever.
    UploadSquareToneThroughPorts(cpu);
    KeyOnPcmSlotThroughPorts(cpu, 0);
    FlushAudioPipe(mainLoop, 3);
    EXPECT_GT(MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME), 1000)
        << "control: a normal complement loop end must keep wrapping";

    // Quiet slot 0 (fast release), then rewrite the SAME header with the
    // end stored as its complement 0; slot 1 re-fetches it at key-on.
    wave(0xC8, 0x0F); // slot 0's RC/RR (offset 8): RR 15
    wave(0x68, 0x00); // key off
    FlushAudioPipe(mainLoop, 3);
    EXPECT_LT(MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME), 100)
        << "release sanity before the one-shot arm";

    wave(0x02, 0x01); // SRAM window on
    wave(0x03, 0x20);
    wave(0x04, 0x00);
    wave(0x05, 0x00); // header at 0x200000
    for (const uint8_t b : {0xA0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})
        wave(0x06, b); // end complement 0x0000: E = 0
    wave(0x02, 0x10); // window off, header base 4

    // The 8-sample table at step ~1: a burst inside the drained frames,
    // then past-data reads - which must be silence, not a wrap to the start.
    KeyOnPcmSlotThroughPorts(cpu, 1);
    int burst = 0;
    for (int frame = 0; frame < 4; frame++)
    {
        mainLoop->RunFrame();
        burst = std::max(burst, MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME));
    }
    EXPECT_GT(burst, 1000) << "the E=0 one-shot burst must be audible";
    mainLoop->RunFrame();
    mainLoop->RunFrame();
    EXPECT_EQ(MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME), 0)
        << "E=0 must not wrap: past-data reads are silence";
}

/// Canary (PcmPanSweep row): pan 0 feeds both sides, pan 7 is hard right,
/// pan 9 hard left, pan 8 switches both sides off - live-writable on a
/// keyed voice.
TEST_F(MoonSoundDevice_Test, Canary_PcmPanRow_CentreHardRightHardLeftBothOff)
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

    UploadSquareToneThroughPorts(cpu);
    KeyOnPcmSlotThroughPorts(cpu, 0);
    FlushAudioPipe(mainLoop, 3); // settle at full level

    wave(0x68, 0x80); // pan 0: both sides
    FlushAudioPipe(mainLoop, 2);
    mainLoop->RunFrame();
    const int centreL = ScanSide(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME, 0).peak;
    const int centreR = ScanSide(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME, 1).peak;
    ASSERT_GT(centreL, 1000);
    ASSERT_GT(centreR, 1000);

    wave(0x68, 0x87); // pan 7: hard right
    FlushAudioPipe(mainLoop, 2);
    mainLoop->RunFrame();
    const SideStats right = ScanSide(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME, 1);
    const int rightL = ScanSide(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME, 0).peak;
    EXPECT_GT(right.peak, 1000);
    EXPECT_LT(rightL * 32, right.peak) << "hard right must leave the left side silent";

    wave(0x68, 0x89); // pan 9: hard left
    FlushAudioPipe(mainLoop, 2);
    mainLoop->RunFrame();
    const SideStats left = ScanSide(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME, 0);
    const int leftR = ScanSide(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME, 1).peak;
    EXPECT_GT(left.peak, 1000);
    EXPECT_LT(leftR * 32, left.peak) << "hard left must leave the right side silent";

    wave(0x68, 0x88); // pan 8: both sides off
    FlushAudioPipe(mainLoop, 2);
    mainLoop->RunFrame();
    const SideStats offL = ScanSide(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME, 0);
    const SideStats offR = ScanSide(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME, 1);
    EXPECT_LT(offL.peak, 100) << "pan 8 switches the left side off";
    EXPECT_LT(offR.peak, 100) << "pan 8 switches the right side off";
}

/// Canary (PcmTlLadderSweep row, the 0x7F corner): total level 0x7F with
/// LD set maps to the internal full-attenuation level (the HW-verified D6
/// special) - immediate silence - while a mid-field value stays audible at
/// the PCM block's 0.375 dB-per-unit level.
TEST_F(MoonSoundDevice_Test, Canary_PcmTlSpecialLevel_MutesImmediatelyWithLd)
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

    UploadSquareToneThroughPorts(cpu);
    KeyOnPcmSlotThroughPorts(cpu, 0);
    FlushAudioPipe(mainLoop, 3);
    const int full = MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME);
    ASSERT_GT(full, 1000);

    wave(0x50, 0x41); // TL 0x20 (-12 dB at PCM's 0.375 dB/unit) + LD: immediate
    FlushAudioPipe(mainLoop, 2);
    mainLoop->RunFrame();
    const int mid = MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME);
    EXPECT_LT(mid * 10, full * 4) << "TL 0x20 is -12 dB, not louder";
    EXPECT_GT(mid * 20, full * 3) << "TL 0x20 is -12 dB, not near-silence";

    wave(0x50, 0xFF); // TL 0x7F + LD: the D6 special - full attenuation
    FlushAudioPipe(mainLoop, 2);
    mainLoop->RunFrame();
    EXPECT_EQ(MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME), 0)
        << "TL 0x7F with LD must mute immediately";
}

/// Canary (MixFieldMatrix row): block mix 0xF8 scales only the FM group
/// (0x1B, the reset default, is -9 dB both sides) and 0xF9 only the PCM
/// group (0xFF mutes both sides); neither field touches the other block.
TEST_F(MoonSoundDevice_Test, Canary_BlockMixFields_ScaleTheirOwnBlockOnly)
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

    UploadSquareToneThroughPorts(cpu); // arms the card for 0xF8/0xF9 access
    wave(0xF8, 0x00);                  // FM unity
    wave(0xF9, 0x00);                  // PCM unity
    KeyOnFmCh0ThroughPorts(cpu);
    KeyOnPcmSlotThroughPorts(cpu, 0);
    FlushAudioPipe(mainLoop, 3);
    const int fmUnity = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    const int pcmUnity = MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME);
    ASSERT_GT(fmUnity, 1000);
    ASSERT_GT(pcmUnity, 1000);

    wave(0xF8, 0x1B); // FM -9 dB both sides (the reset default)
    FlushAudioPipe(mainLoop, 3); // FM audio drains slowest
    const int fmAttenuated = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    const int pcmUntouched = MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME);
    EXPECT_GT(fmAttenuated * 4, fmUnity) << "0x1B is -9 dB, not -12 dB or worse";
    EXPECT_LT(fmAttenuated * 2, fmUnity) << "0x1B is -9 dB, not near unity";
    EXPECT_GT(pcmUntouched * 10, pcmUnity * 9) << "0xF8 must not touch the PCM block";

    wave(0xF9, 0xFF); // PCM both sides muted
    FlushAudioPipe(mainLoop, 3);
    EXPECT_EQ(MaxAbsSample(moonsound->getPcmBuffer(), SAMPLES_PER_FRAME), 0)
        << "PCM mix 0xFF is full attenuation";
    const int fmUntouched = MaxAbsSample(moonsound->getFmBuffer(), SAMPLES_PER_FRAME);
    EXPECT_GT(fmUntouched * 4, fmUnity) << "0xF9 must not touch the FM block";
    EXPECT_LT(fmUntouched * 2, fmUnity) << "0xF9 must not touch the FM block";
}

/// Canary (MemoryAccessSweep row): the SRAM window round-trips distinct
/// bytes through the real bus - write through reg 6 with auto-increment,
/// re-point the 22-bit address counter, read the same bytes back, and
/// cross-check the device's wave memory at the same addresses.
TEST_F(MoonSoundDevice_Test, Canary_SramWindow_RoundTripsDistinctBytes)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    const auto wave = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0x7E, reg);
        cpu->out(0x7F, value);
    };

    // Arm the wave part: the YMF278B ignores wave register access while
    // NEW2 is clear (openMSX-verified).
    cpu->out(0xC6, 0x05);
    cpu->out(0xC7, 0x03);

    static constexpr uint8_t kPattern[8] = {0x11, 0x27, 0x33, 0x55, 0x77, 0x99, 0xB4, 0xFE};
    constexpr uint32_t kSram = 0x201000; // SRAM well clear of the tone data

    wave(0x02, 0x01); // memory window on (MA)
    wave(0x03, 0x20); // 22-bit address 0x201000: A21..A16 / A15..A8 / A7..A0
    wave(0x04, 0x10);
    wave(0x05, 0x00);
    for (uint8_t b : kPattern)
        wave(0x06, b); // data writes auto-increment the counter

    wave(0x03, 0x20); // re-point and read back
    wave(0x04, 0x10);
    wave(0x05, 0x00);
    cpu->out(0x7E, 0x06); // select the memory data register: reads auto-increment
    for (int i = 0; i < 8; i++)
    {
        EXPECT_EQ(cpu->in(0x7F), kPattern[i]) << "SRAM byte " << i << " must read back through the bus";
        EXPECT_EQ(moonsound->waveMemory().Read(kSram + static_cast<uint32_t>(i)), kPattern[i])
            << "SRAM byte " << i << " must land in the wave memory";
    }
}

/// Hiss bisect round 3 (mfm_sample_2 follow-up, 2026-09-17). The .MFM files
/// carry the MoonBlaster FM instrument table at offset 8 (11 bytes each:
/// modFlags carFlags modTL carTL modArDr carArDr modSlRr carSlRr wsM wsC
/// fbconn), which pins the hiss instruments to ground truth: DJINGLE2 /
/// DJINGLE4 / PATSTORY ins0 = the lead patch with fbconn 0x0D (FB6, additive),
/// FOUNTAIN ins2 = the pad with fbconn 0x0B (FB5, additive), DJINGLE4 ins1
/// and FOUNTAIN ins0 = fbconn 0x0E (FB7, FM). The earlier guest snapshot
/// "FB=7 on all 18 channels" was shadow garbage (zero C0 writes captured).
/// Round 2 pinned the hiss to the feedback register at FB7; this round sweeps
/// FB across the exact guest instruments. Die-accurate Nuked-OPL3
/// (scratch/nuked-ref/fbtest) reads: lead FB5-add hf=0.07 tonal, FB6-add
/// hf=0.99 noise, FB7-add 1.15; pad FB5-add hf=0.15 tonal, FB6-add 1.12
/// noise; FB6-fm 0.93. An engine that disagrees with Nuked at the same C0
/// is the deviation.
///
/// RESOLVED: the init-inclusive guest capture (capture starts before the
/// melody-advance keypress) shows the player writes C0 twice per channel at
/// melody init - first the correct fbconn|0x30 from the instrument, then a
/// stereo-panning write of 0x0F|pan that clobbers every channel to FB7
/// additive. At FB7 both local backends and Nuked go chaotic for the hiss
/// melodies' low-TL instruments (sustain hf ~1.2-1.3 here vs Nuked 1.15)
/// and stay tonal for everything else: the hiss is the demo binary's own
/// panning clobber, reproduced faithfully - the emulator matches the
/// die-accurate reference at the exact captured register state (the TRD's
/// player is byte-identical to the analyzed build). Known inter-reference
/// gap, unrelated to this verdict: at FB6-additive the local engines read
/// tonal (hf 0.13) where Nuked still reads noisy (0.99).
TEST_F(MoonSoundDevice_Test, Diagnostic_MfmHissPatches_SustainedVsRetrig)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
    ASSERT_NE(moonsound, nullptr);
    Z80* cpu = context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    ASSERT_NE(mainLoop, nullptr);

    const auto fm1 = [cpu](uint8_t reg, uint8_t value)
    {
        cpu->out(0xC4, reg);
        cpu->out(0xC5, value);
    };

    // One variant = a full channel programme. Register numbers follow the
    // slot map above (same formula the engine uses), so every write lands on
    // the channel that A0/B0/C0 key and route.
    struct Patch
    {
        const char* name;
        int ch;             // bank-0 channel number
        uint8_t modFlags;   // 0x20 + ch%3 + 8*(ch/3)
        uint8_t modTl;
        uint8_t modArDr;
        uint8_t modSlRr;
        uint8_t carFlags;
        uint8_t carTl;
        uint8_t carArDr;
        uint8_t carSlRr;
        uint8_t a0;         // fnum low
        uint8_t b0Kon;      // KON | block | fnum high
        uint8_t c0;         // feedback | conn | routing
    };
    constexpr int kLeadCh = 7;
    constexpr int kPadCh = 2;
    const Patch patches[] = {
        // Lead = MFM ins0 of DJINGLE2/DJINGLE4/PATSTORY (fbconn 0x0D = FB6 add).
        {"lead-fb4-add", kLeadCh, 0x71, 10, 0xAF, 0x14, 0x31, 18, 0xC7, 0x24, 0xD0, 0x31, 0x39},
        {"lead-fb5-add", kLeadCh, 0x71, 10, 0xAF, 0x14, 0x31, 18, 0xC7, 0x24, 0xD0, 0x31, 0x3B},
        {"lead-fb6-add", kLeadCh, 0x71, 10, 0xAF, 0x14, 0x31, 18, 0xC7, 0x24, 0xD0, 0x31, 0x3D},
        {"lead-fb7-add", kLeadCh, 0x71, 10, 0xAF, 0x14, 0x31, 18, 0xC7, 0x24, 0xD0, 0x31, 0x3F},
        // Melody 3 ch6 guest row: same instrument, TL5/TL62.
        {"m3-tl5-62-fb6", kLeadCh, 0x71, 5, 0xAF, 0x14, 0x31, 62, 0xC7, 0x24, 0xD0, 0x31, 0x3D},
        // Pad = FOUNTAIN ins2 (fbconn 0x0B = FB5 add).
        {"pad-fb4-add", kPadCh, 0x51, 7, 0x90, 0xB4, 0x04, 10, 0x06, 0xC6, 0x59, 0x31, 0x39},
        {"pad-fb5-add", kPadCh, 0x51, 7, 0x90, 0xB4, 0x04, 10, 0x06, 0xC6, 0x59, 0x31, 0x3B},
        {"pad-fb6-add", kPadCh, 0x51, 7, 0x90, 0xB4, 0x04, 10, 0x06, 0xC6, 0x59, 0x31, 0x3D},
        {"pad-fb7-add", kPadCh, 0x51, 7, 0x90, 0xB4, 0x04, 10, 0x06, 0xC6, 0x59, 0x31, 0x3F},
        // DJINGLE4 ins1 (fbconn 0x0E = FB7 FM) and FOUNTAIN ins0 (same).
        {"m5ins1-fb7-fm", kLeadCh, 0xF3, 160, 0xB3, 0xA6, 0xF1, 5, 0xD2, 0xE6, 0xD0, 0x31, 0x3E},
        {"m10ins0-fb7-fm", kLeadCh, 0x31, 27, 0x41, 0x0B, 0x61, 128, 0x92, 0x3B, 0xD0, 0x31, 0x3E},
    };

    struct Window
    {
        double rms;
        double hf;
        double zc;
        double dc;
    };
    const auto measure = [&](int frames, const auto& drive)
    {
        double energy = 0.0, diff = 0.0, mean = 0.0;
        long long samples = 0, crossings = 0;
        for (int f = 0; f < frames; f++)
        {
            drive(f);
            mainLoop->RunFrame();
            const int16_t* fm = moonsound->getFmBuffer();
            double lastV = 0.0;
            for (int s = 0; s < SAMPLES_PER_FRAME; s++)
            {
                const double v = (static_cast<double>(fm[2 * s]) + fm[2 * s + 1]) * 0.5;
                energy += v * v;
                mean += v;
                const double d = (s == 0) ? 0.0 : v - lastV;
                diff += d * d;
                if (s > 0 && ((lastV < 0) != (v < 0)))
                    crossings++;
                lastV = v;
            }
            samples += SAMPLES_PER_FRAME;
        }
        Window w;
        w.rms = std::sqrt(energy / samples);
        w.hf = std::sqrt(diff / (energy + 1e-30));
        w.zc = static_cast<double>(crossings) / samples;
        w.dc = mean / samples;
        return w;
    };

    std::cout << "[bisect2] backend=" <<
#ifdef OPL4_FM_YMFM
        "ymfm"
#else
        "opl4"
#endif
              << " (lead ch7 @0x31/0x34, pad ch2 @0x22/0x25)\n";
    for (const Patch& p : patches)
    {
        const uint8_t mod = static_cast<uint8_t>(0x20 + p.ch % 3 + 8 * (p.ch / 3));
        const uint8_t car = static_cast<uint8_t>(mod + 3);
        cpu->out(0xC6, 0x05);
        cpu->out(0xC7, 0x01); // 0x105 NEW: bank 1 live
        fm1(0x08, 0x00);      // no CSM, note-select 0
        fm1(0xBD, 0x00);      // no deep LFO, no rhythm
        fm1(mod, p.modFlags);
        fm1(car, p.carFlags);
        fm1(static_cast<uint8_t>(mod + 0x20), p.modTl);
        fm1(static_cast<uint8_t>(car + 0x20), p.carTl);
        fm1(static_cast<uint8_t>(mod + 0x40), p.modArDr);
        fm1(static_cast<uint8_t>(car + 0x40), p.carArDr);
        fm1(static_cast<uint8_t>(mod + 0x60), p.modSlRr);
        fm1(static_cast<uint8_t>(car + 0x60), p.carSlRr);
        fm1(static_cast<uint8_t>(0xA0 + p.ch), p.a0);
        fm1(static_cast<uint8_t>(0xC0 + p.ch), p.c0);
        fm1(static_cast<uint8_t>(0xB0 + p.ch), static_cast<uint8_t>(p.b0Kon & ~0x20)); // key off

        const uint8_t b0Ch = static_cast<uint8_t>(0xB0 + p.ch);
        const auto keyOn = [&]() { fm1(b0Ch, p.b0Kon); };
        const auto keyOff = [&]() { fm1(b0Ch, static_cast<uint8_t>(p.b0Kon & ~0x20)); };

        keyOn();
        const Window sustain = measure(120, [](int) {}); // sustained note, no traffic
        keyOff();
        const Window release = measure(10, [](int) {});

        // Retrigger cycle: key on 3 frames in, off again at frame 12 - the
        // player's row cadence at tempo 8 (~6.25 Hz), then repeat.
        int cycle = 0;
        const Window retrig = measure(80, [&](int f)
        {
            const int phase = f % 16;
            if (phase == 3)
            {
                keyOn();
                cycle++;
            }
            else if (phase == 12)
                keyOff();
        });

        std::cout << "[bisect2] " << p.name << ": sustain rms=" << sustain.rms
                  << " hf=" << sustain.hf << " zc=" << sustain.zc << " dc=" << sustain.dc
                  << " | release rms=" << release.rms
                  << " | retrig(x" << cycle << ") rms=" << retrig.rms
                  << " hf=" << retrig.hf << " zc=" << retrig.zc << " dc=" << retrig.dc << "\n";
    }

    // Diagnostic pass: mechanics only. The verdict comes from the printed
    // sustain-vs-retrigger metrics across patches.
    SUCCEED();
}

#endif  // UNREALNG_HAVE_OPL4
