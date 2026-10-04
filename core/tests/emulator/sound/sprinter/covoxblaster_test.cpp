#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/sprinter/ttdsprinter.h"
#include "emulator/emulator.h"
#include "emulator/machines/sprinter/sprinterfixture.h"
#include "emulator/memory/sprinter/sprinteraccelerator.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/sprinter/covoxblaster.h"

/// T-CBL (Sprinter test-plan §2.9; tdd-accel-sound-input §2): the Covox /
/// Covox-Blaster on its own, driven with explicit base T-states (no machine).
/// Times: one play tick every 16 x (CBL_TAB + 1) T; rate 10 (CBL_TAB 13) = 224 T.
class CovoxBlaster_Test : public ::testing::Test
{
protected:
    CovoxBlaster _cbl{nullptr};

    CovoxBlasterState& St() { return _cbl.State(); }

    /// OTIR-style fill with the INT off: B counts down, the entry is ~B
    void Otir(uint32_t t, const std::vector<uint8_t>& bytes, uint8_t b = 0)
    {
        for (uint8_t v : bytes)
        {
            b = static_cast<uint8_t>(b - 1);
            _cbl.WriteData(t, v, b);
        }
    }
};

TEST_F(CovoxBlaster_Test, RateTable_SixteenRows)
{
    // SP2_1K30.TDF CBL_TAB: 42 MHz / 192 / (TAB + 1); 16 T at 3.5 MHz per 218.75 kHz step
    const uint8_t tab[16] = {13, 9, 0, 0, 0, 0, 0, 0, 27, 19, 13, 9, 6, 4, 3, 1};
    const double khz[16] = {15.625, 21.875, 218.75, 218.75, 218.75, 218.75, 218.75, 218.75,
                            7.8125, 10.9375, 15.625, 21.875, 31.25, 43.75, 54.6875, 109.375};
    for (uint8_t rate = 0; rate < 16; rate++)
    {
        EXPECT_EQ(CovoxBlaster::kDivider[rate], tab[rate]) << int(rate);
        EXPECT_EQ(CovoxBlaster::TickTstates(static_cast<uint8_t>(0x80 | rate)), 16u * (tab[rate] + 1u)) << int(rate);
        EXPECT_NEAR(CovoxBlaster::RateHz(static_cast<uint8_t>(0x80 | rate)) / 1000.0, khz[rate], 1e-9) << int(rate);
    }
    EXPECT_EQ(CovoxBlaster::RateHz(0x0A), 0.0) << "CBL off";
}

TEST_F(CovoxBlaster_Test, CovoxMode_ByteToBothChannels)
{
    EXPECT_EQ(St().levelL, 0x8000) << "reset: the DAC rests at #8000";
    _cbl.WriteData(100, 0xC0, 0x00);
    EXPECT_EQ(St().levelL, 0xC000);
    EXPECT_EQ(St().levelR, 0xC000);
    EXPECT_EQ(St().covoxWrites, 1u);
    EXPECT_EQ(CovoxBlaster::Amplitude(St().levelL), 0x4000 / 2);
    EXPECT_EQ(St().cnt, 0) << "CBL_CNT stays 0 with CBL off";
    // The PLD writes the ring in Covox mode too (CBL.wren is not gated by CBL_MODE): entry ~A15..A8
    EXPECT_EQ(St().ring[0xFF], 0xC000);
}

TEST_F(CovoxBlaster_Test, Mono8Bit_OtirFillsInOrderAndPlays)
{
    _cbl.WriteControl(0, 0x8A);  // CBL on, mono, 8-bit, INT off, 15.625 kHz
    std::vector<uint8_t> bytes(256);
    for (int i = 0; i < 256; i++)
        bytes[i] = static_cast<uint8_t>(i);
    Otir(0, bytes);  // B = 0: entries 0, 1, ..., 255
    for (int i = 0; i < 256; i++)
        ASSERT_EQ(St().ring[i], i << 8) << i;
    EXPECT_EQ(St().wa, 0) << "CBL_WA stays 0 with the INT off";

    // The DAC follows ring[CNT]; CNT steps once per 224 T
    const uint32_t start = St().nextTick;
    EXPECT_EQ(_cbl.ApplyFeBits(start + 224 * 10, 0x1F) & 0x80, 0x00);
    EXPECT_EQ(St().cnt, 11);
    EXPECT_EQ(St().levelL, 11 << 8);
    EXPECT_EQ(St().levelR, 11 << 8);
    // #FE bit 7 = CNT7 with the INT off: the second half plays from entry 128
    EXPECT_EQ(_cbl.ApplyFeBits(start + 224 * 127, 0x1F) & 0x80, 0x80);
}

TEST_F(CovoxBlaster_Test, Stereo_TwoEntriesPerTick)
{
    _cbl.WriteControl(0, 0xCA);  // CBL on, stereo
    Otir(0, {0x10, 0x20, 0x30, 0x40, 0x50, 0x60});
    EXPECT_EQ(St().levelL, 0x1000) << "entry 0 left";
    EXPECT_EQ(St().levelR, 0x2000) << "entry 1 right";
    const uint32_t start = St().nextTick;
    _cbl.ApplyFeBits(start, 0);
    EXPECT_EQ(St().cnt, 2);
    EXPECT_EQ(St().levelL, 0x3000);
    EXPECT_EQ(St().levelR, 0x4000);
    _cbl.ApplyFeBits(start + 224, 0);
    EXPECT_EQ(St().cnt, 4);
    EXPECT_EQ(St().levelL, 0x5000);
    EXPECT_EQ(St().levelR, 0x6000);
}

TEST_F(CovoxBlaster_Test, SixteenBit_PairsLowThenHighSignFlipped)
{
    _cbl.WriteControl(0, 0xBA);  // CBL on, 16-bit, INT on
    // DW 0, 1000, -1000: lo, hi pairs
    const int16_t words[3] = {0, 1000, -1000};
    for (int16_t w : words)
    {
        _cbl.WriteData(0, static_cast<uint8_t>(w & 0xFF), 0);
        _cbl.WriteData(0, static_cast<uint8_t>((w >> 8) & 0xFF), 0);
    }
    EXPECT_EQ(St().ring[0], 0x8000) << "silence is #0000 in 16-bit";
    EXPECT_EQ(St().ring[1], static_cast<uint16_t>(1000 + 0x8000));
    EXPECT_EQ(St().ring[2], static_cast<uint16_t>(-1000 + 0x8000));
    EXPECT_EQ(St().wa, 3) << "one entry per two bytes";
    EXPECT_EQ(St().ringWrites, 3u);
}

TEST_F(CovoxBlaster_Test, IntEvery128Samples_AckMovesWriteIndex)
{
    _cbl.WriteControl(0, 0x9A);  // CBL on, mono, 8-bit, INT on, 224 T
    const uint32_t start = St().nextTick;
    // 128 ticks: CNT #7F -> #80 on the 128th
    EXPECT_FALSE(_cbl.IntRequested(start + 224 * 126));
    EXPECT_EQ(St().cnt, 127);
    EXPECT_TRUE(_cbl.IntRequested(start + 224 * 127));
    EXPECT_EQ(St().cnt, 128);
    EXPECT_EQ(St().intRequests, 1u);
    // Pending: #FE bit 7 = 1, CBL_WA held at the half to fill (0), writes do not advance it
    EXPECT_EQ(_cbl.ApplyFeBits(start + 224 * 127, 0) & 0x80, 0x80);
    EXPECT_EQ(_cbl.EffectiveWriteIndex(), 0x00);
    _cbl.WriteData(start + 224 * 127, 0x11, 0);
    _cbl.WriteData(start + 224 * 127, 0x22, 0);
    EXPECT_EQ(St().ring[0], 0x2200) << "both writes hit entry 0 while the request is pending";

    _cbl.Acknowledge(start + 224 * 127 + 10);
    EXPECT_FALSE(_cbl.IntRequested(start + 224 * 127 + 10));
    EXPECT_EQ(St().wa, 0x00);
    for (int i = 0; i < 128; i++)
        _cbl.WriteData(start + 224 * 128, static_cast<uint8_t>(i), 0);
    EXPECT_EQ(St().ring[127], 127 << 8);
    EXPECT_EQ(St().wa, 0x80);
    // Filled: CNT7 (1) XOR WA7 (1) = 0
    EXPECT_EQ(_cbl.ApplyFeBits(start + 224 * 128, 0) & 0x80, 0x00);

    // The second request at #FF -> #00, WA then points at the upper half
    EXPECT_TRUE(_cbl.IntRequested(start + 224 * 255));
    EXPECT_EQ(St().cnt, 0);
    _cbl.Acknowledge(start + 224 * 255);
    EXPECT_EQ(St().wa, 0x80);
    EXPECT_EQ(St().intRequests, 2u);
}

TEST_F(CovoxBlaster_Test, IntOff_NoRequestAndDisableClearsPending)
{
    _cbl.WriteControl(0, 0x8A);
    EXPECT_FALSE(_cbl.IntRequested(St().nextTick + 224 * 200));
    _cbl.WriteControl(0, 0x9A);
    const uint32_t t = St().nextTick + 224 * 200;
    EXPECT_TRUE(_cbl.IntRequested(t));
    _cbl.WriteControl(t, 0x8A);  // INT off: the request goes (CBL_INT presets)
    EXPECT_FALSE(_cbl.IntRequested(t));
}

TEST_F(CovoxBlaster_Test, FeBits_BeamBelowLine272_OnlyInCblMode)
{
    EXPECT_EQ(_cbl.ApplyFeBits(300 * 224, 0xBF), 0xBF) << "CBL off: the keyboard byte unchanged";
    _cbl.WriteControl(0, 0x8A);
    EXPECT_EQ(_cbl.ApplyFeBits(271 * 224 + 223, 0xFF) & 0x20, 0x00);
    EXPECT_EQ(_cbl.ApplyFeBits(272 * 224, 0xDF) & 0x20, 0x20);
    EXPECT_EQ(_cbl.ApplyFeBits(272 * 224, 0x5F) & 0x1F, 0x1F) << "keys and bit 6 pass";
}

TEST_F(CovoxBlaster_Test, CblOff_ClearsPlayIndexAndHoldsLevel)
{
    _cbl.WriteControl(0, 0x8A);
    Otir(0, std::vector<uint8_t>(256, 0x90));
    const uint32_t t = St().nextTick + 224 * 5;
    _cbl.ApplyFeBits(t, 0);
    EXPECT_EQ(St().cnt, 6);
    _cbl.WriteControl(t, 0x0A);
    EXPECT_EQ(St().cnt, 0);
    EXPECT_EQ(St().levelL, 0x9000) << "CBL_R holds its word until a Covox write";
    _cbl.WriteData(t + 1, 0x80, 0);
    EXPECT_EQ(St().levelL, 0x8000);
}

TEST_F(CovoxBlaster_Test, ReservedRates_PlayAt21875kHzStep)
{
    _cbl.WriteControl(0, 0x83);  // rate 3: CBL_TAB 0 - the PLD ticks every 16 T (MAME: never)
    const uint32_t start = St().nextTick;
    _cbl.ApplyFeBits(start + 16 * 9, 0);
    EXPECT_EQ(St().cnt, 10);
}

TEST_F(CovoxBlaster_Test, WriteIntoPlayingEntry_HeardAtOnce)
{
    _cbl.WriteControl(0, 0x8A);
    // INT off, entry ~B: B = #FF writes entry 0, which plays now (CNT = 0)
    _cbl.WriteData(0, 0x33, 0xFF);
    EXPECT_EQ(St().levelL, 0x3300);
}

TEST_F(CovoxBlaster_Test, EndFrame_RebasesTheTickAndRendersTheLevel)
{
    _cbl.WriteControl(0, 0x8A);
    Otir(0, std::vector<uint8_t>(256, 0xC0));
    _cbl.AudioFrameStart(false);
    _cbl.EndFrame(71680, 903);
    EXPECT_LT(St().nextTick, 224u) << "the next tick moved into the new frame";
    EXPECT_EQ(St().ticks, 71680u / 224u - 1) << "one tick per line from line 1 (CBL_CTX ran from frame start with CBL off)";
    const int16_t* buffer = _cbl.AudioBuffer();
    EXPECT_NEAR(buffer[900 * 2], CovoxBlaster::Amplitude(0xC000), 64) << "the level settles at #C000";
    EXPECT_NEAR(buffer[900 * 2 + 1], CovoxBlaster::Amplitude(0xC000), 64);
}

TEST_F(CovoxBlaster_Test, Tone_RendersItsFrequency)
{
    // A 1 kHz-ish square wave from the ring: 8 entries high, 8 low at 15.625 kHz = 976.6 Hz
    _cbl.WriteControl(0, 0x8A);
    std::vector<uint8_t> bytes(256);
    for (int i = 0; i < 256; i++)
        bytes[i] = ((i / 8) & 1) ? 0xC0 : 0x40;
    Otir(0, bytes);
    _cbl.AudioFrameStart(false);
    _cbl.EndFrame(71680, 903);
    const int16_t* buffer = _cbl.AudioBuffer();
    int crossings = 0;
    for (int i = 101; i < 903; i++)
        if ((buffer[(i - 1) * 2] < 0) != (buffer[i * 2] < 0))
            crossings++;
    // 802 samples at 44.1 kHz = 18.2 ms: 976.6 Hz gives ~35.5 crossings
    EXPECT_NEAR(crossings, 35, 2);
}

/// The Covox-Blaster in the machine: codes #88 / #89, the INT through the Sprinter INT source, the
/// accelerator's page-#FD path, the mixer slot, the single AY, the TTD blob (id 32)
class CovoxBlasterMachine_Test : public SprinterFixture
{
protected:
    void SetUp() override
    {
        SprinterFixture::SetUp();
        OpenDcp();
        // INC SP2000.inc: data #FB / #4F (code #88), control #4E (code #89), #FE (code #40)
        SetCode(0x00FB, false, SprinterCode::Covox);
        SetCode(0x004E, false, SprinterCode::CovoxBlaster);
        SetCode(0x004E, true, SprinterCode::CovoxBlaster);
        SetCode(0x00FE, true, SprinterCode::Keyboard);
    }

    CovoxBlaster& Cbl() { return _decoder->GetCovoxBlaster(); }
};

TEST_F(CovoxBlasterMachine_Test, Codes88And89_ReachTheDevice)
{
    Out(0x004E, 0x00);
    Out(0x00FB, 0xC0);
    EXPECT_EQ(Cbl().State().levelL, 0xC000) << "Covox: both channels";
    EXPECT_EQ(Cbl().State().levelR, 0xC000);

    Out(0x004E, 0xDA);
    EXPECT_EQ(In(0x004E), 0xDA) << "code #89 reads back";
    EXPECT_EQ(_decoder->CblControl(), 0xDA);
    // INT on: the write index advances; the data port's high byte is ignored
    Out(0x12FB, 0x11);
    Out(0x1CFB, 0x22);  // A15-A13 = 0: the same table entry
    EXPECT_EQ(Cbl().State().ring[0], 0x1100);
    EXPECT_EQ(Cbl().State().ring[1], 0x2200);
    EXPECT_EQ(Cbl().State().wa, 2);
}

TEST_F(CovoxBlasterMachine_Test, HalfRingInt_ThroughTheSprinterIntSource)
{
    Out(0x004E, 0x9A);  // mono, INT on, a tick per scan line
    SprinterIntSource& source = _decoder->GetIntSource();
    const uint32_t first = Cbl().State().nextTick;
    EXPECT_FALSE(source.IsIntAsserted(first + 224 * 126));
    // 128 lines later the PLD asks for data: vector #FF, the acknowledge ends the request
    const uint32_t t = first + 224 * 127;
    ASSERT_TRUE(source.IsIntAsserted(t));
    EXPECT_EQ(In(0x00FE) & 0x80, 0x80) << "#FE bit 7: a half needs data";
    EXPECT_EQ(source.AcknowledgeInterrupt(t), 0xFF);
    EXPECT_FALSE(Cbl().State().intPending);
    EXPECT_EQ(Cbl().State().wa, 0x00) << "the write index starts the half just played";
}

TEST_F(CovoxBlasterMachine_Test, AcceleratorCopyIntoPageFD_FeedsTheRing)
{
    // Window 1 = page #FD, the data in window 2
    Pld().Cell(SprinterCode::Page1) = SprinterMemory::kCblPage;
    _decoder->UpdateBanks();
    ASSERT_EQ(_sprinterMemory->GetBankAction(1), SprinterMemory::BankAction::CblPage);
    const uint8_t page2 = Pld().Cell(SprinterCode::Page2);
    for (uint16_t i = 0; i < 4; i++)
        Ram(page2, static_cast<uint16_t>(0x0100 + i)) = static_cast<uint8_t>(0x11 * (i + 1));
    Pld().allMode = 0x01;  // the accelerator on
    Out(0x004E, 0x9A);     // CBL with its INT on: the PLD's page term needs it

    // LD HL,#8100 : LD BC,#4000 : LD D,D : LD A,4 : LD L,L : LD A,(HL) : LD (BC),A : LD B,B
    RunCode({0x21, 0x00, 0x81, 0x01, 0x00, 0x40, 0x52, 0x3E, 0x04, 0x6D, 0x7E, 0x02, 0x40});
    for (uint16_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(Ram(SprinterMemory::kCblPage, i), 0x11 * (i + 1)) << "the plain store lands too";
        EXPECT_EQ(Cbl().State().ring[i], (0x11 * (i + 1)) << 8) << i;
    }
    EXPECT_EQ(Cbl().State().wa, 4);

    // A plain CPU store (no copy mode) does not feed the ring; nor does a copy with the INT off
    Poke(0x4010, 0x77);
    EXPECT_EQ(Cbl().State().ringWrites, 4u);
    Out(0x004E, 0x8A);
    RunCode({0x21, 0x00, 0x81, 0x01, 0x20, 0x40, 0x52, 0x3E, 0x04, 0x6D, 0x7E, 0x02, 0x40});
    EXPECT_EQ(Cbl().State().ringWrites, 4u);
}

TEST_F(CovoxBlasterMachine_Test, MixerSlot_CovoxRowCarriesTheDevice)
{
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);
    EXPECT_EQ(sound->getModelAudioSource(), &Cbl());
    const AudioDeviceInfo* row = sound->device(AudioSourceType::COVOX);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->name, "Covox-Blaster");
    EXPECT_EQ(sound->deviceBuffer(AudioSourceType::COVOX), Cbl().AudioBuffer());
}

TEST_F(CovoxBlasterMachine_Test, SingleAy_NoSecondChip)
{
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);
    EXPECT_EQ(sound->getAYChipCount(), 1);
    EXPECT_EQ(sound->device(AudioSourceType::AY2_All), nullptr);
    // #FE to the register select (code #90) selects nothing; the next writes stay on the one chip
    SetCode(0xFFFD, false, SprinterCode::AyAddress);
    SetCode(0xBFFD, false, 0x91);
    Out(0xFFFD, 0xFE);
    Out(0xFFFD, 0x08);
    Out(0xBFFD, 0x0F);
    EXPECT_EQ(sound->getAYChip(0)->readRegister(0x08), 0x0F);
}

TEST_F(CovoxBlasterMachine_Test, TtdBlob_RoundTrip)
{
    Out(0x004E, 0xFA);
    for (int i = 0; i < 10; i++)
        Out(0x00FB, static_cast<uint8_t>(i * 7));
    ttd::TTDSprinterCovoxBlaster blob(*_decoder);
    ASSERT_EQ(blob.TTDStateSize(), 545u);
    std::vector<uint8_t> saved(blob.TTDStateSize());
    blob.TTDSaveState(saved.data());
    const CovoxBlasterState before = Cbl().State();
    const uint64_t hash = blob.TTDHashState();

    Out(0x004E, 0x00);
    Out(0x00FB, 0x01);
    EXPECT_NE(blob.TTDHashState(), hash);

    blob.TTDLoadState(saved.data());
    EXPECT_EQ(std::memcmp(&before, &Cbl().State(), sizeof(CovoxBlasterState)), 0);
    EXPECT_EQ(blob.TTDHashState(), hash);
    const std::vector<ttd::PeripheralId> ids = _decoder->GetTTDModelStateIds();
    EXPECT_NE(std::find(ids.begin(), ids.end(), ttd::PeripheralId::SprinterCovoxBlaster), ids.end());
}

/// The sound at 21 MHz, through the whole frame loop (Emulator::RunFrame): the AY's pitch and the
/// Covox-Blaster's sample rate are clocked in emulated time, not in CPU T-states - the turbo (CPU
/// clock x 6) must change neither, and the stream must have no gaps. The machine: the shipped
/// Sprinter config, the PLD started configured (FastStart), a `DI : JR $` loop in RAM, the turbo on.
/// ~1 s of emulated audio per test (50 frames at 21 MHz, ~150 ms): the measurement needs it
class SprinterSoundTurbo_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;
    AudioCaptureAnalyzer* _capture = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("SPRINTER", TurboSoundKind::Single);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->FastStart();
        // The loader's power-on wait window ends here as the BIOS ends it (WCR = 0): only the PLD's waits
        _decoder->GetZ84().Write(0xEE, 0x00);
        _decoder->GetZ84().Write(0xEF, 0x00);

        // The port table entries this test uses (page #40), then open the decoder with a read
        Code(0x00FE, true, SprinterCode::Keyboard);
        _decoder->DecodePortIn(0x00FE, 0);
        Code(0x007C, false, SprinterCode::SysCnf);
        Code(0xFFFD, false, SprinterCode::AyAddress);
        Code(0xBFFD, false, 0x91);
        for (uint16_t high = 0; high < 8; high++)  // A15-A13 are in the index: OTIR's B walks all of them
            Code(static_cast<uint16_t>(high << 13 | 0x00FB), false, SprinterCode::Covox);
        Code(0x004E, false, SprinterCode::CovoxBlaster);

        // DI : JR $ in window 2
        Memory* memory = _context->pMemory;
        memory->DirectWriteToZ80Memory(0x8000, 0xF3);
        memory->DirectWriteToZ80Memory(0x8001, 0x18);
        memory->DirectWriteToZ80Memory(0x8002, 0xFE);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->iff1 = z80->iff2 = 0;

        Out(0x007C, 0x03);  // CNF/SYS bit 1 = 1: turbo = bit 0 -> 21 MHz
        ASSERT_EQ(_context->emulatorState.hw_turbo_ratio, 6);
        for (int i = 0; i < 2; i++)
            _emulator->RunFrame(true);  // the turbo applies from a frame start
        ASSERT_EQ(_context->emulatorState.hw_turbo_ratio_applied, 6);

        AnalyzerManager* analyzers = _context->pDebugManager ? _context->pDebugManager->GetAnalyzerManager() : nullptr;
        ASSERT_NE(analyzers, nullptr);
        analyzers->activate("audiocapture");
        _capture = analyzers->getAnalyzer<AudioCaptureAnalyzer>("audiocapture");
        ASSERT_NE(_capture, nullptr);
    }

    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }

    void Code(uint16_t port, bool isRead, uint8_t code)
    {
        _context->pMemory->RAMPageAddress(SprinterMemory::kPortTablePage)[_decoder->LookupIndex(port, isRead)] = code;
    }
    void Out(uint16_t port, uint8_t value) { _decoder->DecodePortOut(port, value, 0x8000); }
    void Ay(uint8_t reg, uint8_t value)
    {
        Out(0xFFFD, reg);
        Out(0xBFFD, value);
    }

    /// Run until `seconds` of the master mix are captured; the left channel, its mean removed
    std::vector<double> Capture(double seconds)
    {
        const size_t rate = _context->pSoundManager->getCoreRate();
        _capture->startCapture(static_cast<size_t>(seconds * static_cast<double>(rate)) * 2);
        for (int guard = 0; guard < 200 && !_capture->isCaptureComplete(); guard++)
            _emulator->RunFrame(true);
        EXPECT_TRUE(_capture->isCaptureComplete());
        const std::vector<int16_t>& buffer = _capture->getBuffer();
        std::vector<double> left(_capture->getCapturedSamples() / 2);
        double mean = 0;
        for (size_t i = 0; i < left.size(); i++)
            mean += left[i] = buffer[i * 2];
        mean /= static_cast<double>(left.size() ? left.size() : 1);
        for (double& v : left)
            v -= mean;
        return left;
    }

    /// Rising zero crossings (sample positions, linearly interpolated)
    static std::vector<double> Crossings(const std::vector<double>& x)
    {
        std::vector<double> at;
        for (size_t i = 1; i < x.size(); i++)
            if (x[i - 1] < 0 && x[i] >= 0)
                at.push_back(static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]));
        return at;
    }

    /// The frequency from the crossings, and the largest deviation of one period from the mean (a gap or a
    /// repeat shows as a long or short period)
    void Measure(const std::vector<double>& x, double& hz, double& worstPeriodError)
    {
        // The first 0.1 s left out: the mixer's filters settle on the new tone there
        const std::vector<double> all = Crossings(x);
        const double skip = 0.1 * static_cast<double>(_context->pSoundManager->getCoreRate());
        std::vector<double> at;
        for (double a : all)
            if (a >= skip)
                at.push_back(a);
        ASSERT_GT(at.size(), 10u);
        const double period = (at.back() - at.front()) / static_cast<double>(at.size() - 1);
        hz = static_cast<double>(_context->pSoundManager->getCoreRate()) / period;
        worstPeriodError = 0;
        for (size_t i = 1; i < at.size(); i++)
            worstPeriodError = std::max(worstPeriodError, std::abs((at[i] - at[i - 1]) - period));
    }
};

TEST_F(SprinterSoundTurbo_Test, AyTone_1750kHzClockAt21MHz)
{
    // Tone A, period 250: 1 750 000 / (16 x 250) = 437.5 Hz
    Ay(0, 250);
    Ay(1, 0);
    Ay(7, 0x3E);
    Ay(8, 0x0F);
    double hz = 0, worst = 0;
    Measure(Capture(1.0), hz, worst);
    EXPECT_NEAR(hz, 437.5, 437.5 * 0.002) << "AY pitch at 21 MHz";
    EXPECT_LT(worst, 2.0) << "an uneven period: a gap or a repeat in the AY stream (samples)";
}

TEST_F(SprinterSoundTurbo_Test, CovoxBlaster_SampleRateAt21MHz)
{
    // CBL on, mono, 8-bit, INT off, 15.625 kHz; a square of 16 entries: 976.5625 Hz
    Out(0x004E, 0x8A);
    for (int i = 0; i < 256; i++)
    {
        const uint8_t b = static_cast<uint8_t>(255 - i);  // OTIR with B = 0: the entry is ~B = i
        _decoder->DecodePortOut(static_cast<uint16_t>(b << 8 | 0xFB), ((i / 8) & 1) ? 0xC0 : 0x40, 0x8000);
    }
    _emulator->RunFrame(true);  // the first frame starts at the play phase the control write found
    const uint32_t ticksBefore = _decoder->GetCovoxBlaster().State().ticks;
    for (int i = 0; i < 10; i++)
        _emulator->RunFrame(true);
    EXPECT_EQ(_decoder->GetCovoxBlaster().State().ticks - ticksBefore, 10u * 320u)
        << "one play tick per 224 base T-states: 320 per 71 680 T frame, at any CPU clock";

    double hz = 0, worst = 0;
    Measure(Capture(1.0), hz, worst);
    EXPECT_NEAR(hz, 976.5625, 976.5625 * 0.002) << "Covox-Blaster rate at 21 MHz";
    EXPECT_LT(worst, 2.0) << "an uneven period: a gap or a repeat in the Covox-Blaster stream (samples)";
}


// The CPU clock the sound runs beside: `INC BC : JR -3` from RAM at 21 MHz makes 11 947 passes a frame -
// 36 clocks a pass (18 T by the book + the PLD's memory waits, MAME's phase rule), the same count MAME's
// sprinter driver gives on BIOS 3.06 / DSS 1.71 (tools/machines/sprinter/mame-capture loop mode, S6)
TEST_F(SprinterSoundTurbo_Test, CpuThroughput_MatchesMameAt21MHz)
{
    Memory* memory = _context->pMemory;
    const uint8_t loop[4] = {0xF3, 0x03, 0x18, 0xFD};  // DI : loop: INC BC : JR loop
    for (uint16_t i = 0; i < 4; i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), loop[i]);
    Z80* z80 = _context->pCore->GetZ80();
    z80->pc = 0x8000;
    _emulator->RunFrame(true);
    for (int frame = 0; frame < 3; frame++)
    {
        const uint16_t before = z80->bc;
        _emulator->RunFrame(true);
        EXPECT_NEAR(static_cast<uint16_t>(z80->bc - before), 11947, 1) << "frame " << frame;
    }
}

/// The statistics in the reports (int_requests, ticks, ring_writes, covox_writes) count since the last reset: every
/// reset kind goes through the PLD's /RESET (PortDecoder_Sprinter::ResetPld -> CovoxBlaster::Reset). The PLD itself
/// has no such counters; the ring (lpm_ram_dp) and CBL_CTX have no reset term and keep their values
TEST_F(CovoxBlasterMachine_Test, Reset_RestartsTheStatisticsKeepsTheRing)
{
    // The device driven directly (the port path is Codes88And89_ReachTheDevice): the resets are the machine's
    auto play = [&] {
        CovoxBlaster& cbl = Cbl();
        cbl.WriteControl(0, 0x00);
        cbl.WriteData(0, 0xC0, 0x00);  // a Covox write (lands in the ring too, entry #FF)
        cbl.WriteControl(0, 0x9A);     // mono, INT on, a tick per scan line
        cbl.WriteData(0, 0x5A, 0x00);  // into the ring at entry 0
        ASSERT_TRUE(cbl.IntRequested(cbl.State().nextTick + 224 * 127));
        ASSERT_EQ(cbl.State().intRequests, 1u);
        ASSERT_EQ(cbl.State().ticks, 128u);
        ASSERT_EQ(cbl.State().ringWrites, 2u);
        ASSERT_EQ(cbl.State().covoxWrites, 1u);
    };
    auto expectRestarted = [&](const char* kind) {
        const CovoxBlasterState& s = Cbl().State();
        EXPECT_EQ(s.intRequests, 0u) << kind;
        EXPECT_EQ(s.ticks, 0u) << kind;
        EXPECT_EQ(s.ringWrites, 0u) << kind;
        EXPECT_EQ(s.covoxWrites, 0u) << kind;
        EXPECT_EQ(s.control, 0x00) << kind;
        EXPECT_FALSE(s.intPending) << kind;
        EXPECT_EQ(s.ring[0], 0x5A00) << kind << ": the ring has no reset term";
    };

    // Ctrl+Alt+Del / a write to page #A0: a CPU reset of the running configuration
    ASSERT_NO_FATAL_FAILURE(play());
    _decoder->RequestCpuReset(SprinterResetKind::SoftReset);
    _decoder->OnMachineStep(0);
    expectRestarted("soft reset");

    // The RESET button: the PLD loads its configuration again
    ASSERT_NO_FATAL_FAILURE(play());
    _core->Reset();
    expectRestarted("RESET button");
}
