#ifdef UNREALNG_HAVE_SAM2695
// MultiSoundCard (core/src/emulator/slots/cards/multisound/multisoundcard.h): the ZX-MultiSound assembled from its
// modules and driven through direct port calls, as the slot adapter will drive it (docs/inprogress/2026-10-03-zx-multisound/
// tdd-integration.md MS-3). Time axis: a Pentagon's 3.5 MHz audio T-states, 71680 per frame, absolute; the rig closes
// and opens frames as the host time crosses their ends.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/multisoundscenario.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/cards/multisound/multisoundcard.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/tsfm/ym2203pair.h"
#include "sam2695/sam2695.h"
#include "sam2695/soundbank.h"

namespace
{
constexpr uint64_t kFrameTicks = 71680;  // Pentagon frame, 3.5 MHz
constexpr uint64_t kMidiBitT = 112;      // 3 500 000 / 31 250

constexpr uint16_t kYmRegister = 0xFFFD;
constexpr uint16_t kYmData = 0xBFFD;
constexpr uint16_t kSaaAddress = 0x01FF;
constexpr uint16_t kSaaData = 0x00FF;
constexpr uint16_t kGsData = 0x00B3;
constexpr uint16_t kGsCommand = 0x00BB;

/// One preset (bank 0, program 0): a looped 64-frame square, so a Note On holds one voice
class TinyBank : public sam2695::ISoundBank
{
public:
    TinyBank()
    {
        using namespace sam2695;
        _model.name = "multisoundcard-test";

        SampleInfo s;
        s.name = "square";
        s.start = 0;
        s.end = 64;
        s.loopStart = 0;
        s.loopEnd = 64;
        s.sampleRate = kInternalRate;
        s.originalPitch = 60;
        _model.samples.push_back(s);
        for (int i = 0; i < 64; i++)
            _model.data16.push_back(static_cast<int16_t>(i < 32 ? 8000 : -8000));
        _model.data16.resize(64 + 46, 0);  // SF2 zero tail

        Zone presetZone;
        presetZone.link = 0;
        Zone instZone;
        instZone.link = 0;
        instZone.gens[static_cast<int>(Gen::SampleModes)] = 1;  // loop continuously
        instZone.setMask |= 1ull << static_cast<int>(Gen::SampleModes);
        _model.zones.push_back(presetZone);
        _model.zones.push_back(instZone);

        Instrument inst;
        inst.name = "square";
        inst.zoneFirst = 1;
        inst.zoneCount = 1;
        _model.instruments.push_back(inst);

        Preset p;
        p.name = "square";
        p.program = 0;
        p.bank = 0;
        p.zoneFirst = 0;
        p.zoneCount = 1;
        _model.presets.push_back(p);

        _digest.fill(0);
        _digest[0] = 0x4D;
    }

    const sam2695::BankModel& Model() const override { return _model; }
    const sam2695::BankDigest& Digest() const override { return _digest; }

private:
    sam2695::BankModel _model;
    sam2695::BankDigest _digest{};
};

/// A 32 KB GS ROM in the per-process scratch dir: the program at #0000, the rest NOP
std::string WriteGsRom(const char* leafName, const std::vector<uint8_t>& program)
{
    std::vector<uint8_t> image(SoundChip_GeneralSound::ROM_SIZE, 0x00);
    std::memcpy(image.data(), program.data(), program.size());
    const std::string path = TestPathHelper::GetUniqueTestScratchPath(leafName);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    return path;
}

std::string ReadText(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

/// The card on a host timeline: accesses happen at `t`, frames close and open as `t` crosses their ends, and the
/// rows of every closed frame are collected
class Rig
{
public:
    Rig(EmulatorContext* context, const MultiSoundCardConfig& config) : card(context, config)
    {
        card.FrameStart(0, kFrameTicks);
    }

    void Out(uint16_t port, uint8_t value, uint64_t step = 40)
    {
        Settle();
        card.Out(port, value, t);
        t += step;
    }

    void OutAt(uint64_t at, uint16_t port, uint8_t value)
    {
        t = std::max(t, at);
        Settle();
        card.Out(port, value, t);
    }

    uint8_t In(uint16_t port, bool* drives = nullptr, uint64_t step = 40)
    {
        Settle();
        bool d = false;
        const uint8_t value = card.In(port, t, d);
        if (drives)
            *drives = d;
        t += step;
        return value;
    }

    void Reg(uint8_t reg, uint8_t value)
    {
        Out(kYmRegister, reg);
        Out(kYmData, value);
    }

    void Saa(uint8_t reg, uint8_t value)
    {
        Out(kSaaAddress, reg);
        Out(kSaaData, value);
    }

    void Advance(uint64_t ticks)
    {
        t += ticks;
        Settle();
    }

    void RunFrames(int frames)
    {
        for (int i = 0; i < frames; i++)
        {
            t = std::max(t, frameEnd);
            Settle();
        }
    }

    /// Forgets the collected rows (a clean measurement window from the next closed frame on)
    void Clear()
    {
        for (auto& row : collected)
            row.clear();
    }

    /// Peak-to-peak swing of one side of a row over the collected frames
    double Swing(MultiSoundRow row, int side) const
    {
        const std::vector<int16_t>& v = collected[static_cast<size_t>(row)];
        if (v.size() < 2)
            return 0.0;
        int lo = INT16_MAX;
        int hi = INT16_MIN;
        for (size_t i = static_cast<size_t>(side); i < v.size(); i += 2)
        {
            lo = std::min<int>(lo, v[i]);
            hi = std::max<int>(hi, v[i]);
        }
        return static_cast<double>(hi - lo);
    }

    MultiSoundCardReport Report() const
    {
        MultiSoundCardReport report;
        card.Describe(report);
        return report;
    }

    MultiSoundCard card;
    uint64_t t = 100;
    uint64_t frameEnd = kFrameTicks;
    std::vector<int16_t> collected[static_cast<size_t>(MultiSoundRow::Count)];

private:
    void Settle()
    {
        while (t >= frameEnd)
        {
            const size_t frames = card.FrameEnd(frameEnd);
            for (size_t r = 0; r < static_cast<size_t>(MultiSoundRow::Count); r++)
            {
                const int16_t* row = card.Row(static_cast<MultiSoundRow>(r));
                collected[r].insert(collected[r].end(), row, row + frames * 2);
            }
            card.FrameStart(frameEnd, kFrameTicks);
            frameEnd += kFrameTicks;
        }
    }
};

/// One FM carrier at TL 0 on channel 2 of the selected chip (the TSFM output tests' reference note)
void ProgramFmNote(Rig& rig)
{
    const uint8_t setup[][2] = {
        {0x42, 0x7F}, {0x46, 0x7F}, {0x4A, 0x7F}, {0x4E, 0x00},
        {0x52, 0x1F}, {0x56, 0x1F}, {0x5A, 0x1F}, {0x5E, 0x1F},
        {0x3E, 0x01},
        {0xA6, 0x39}, {0xA2, 0x00},
        {0x28, 0xF2},
    };
    for (const auto& [reg, data] : setup)
        rig.Reg(reg, data);
}

/// SAA voice `voice` at full amplitude on one side (or both), middle A
void ProgramSaaTone(Rig& rig, int voice, uint8_t amplitude)
{
    rig.Saa(static_cast<uint8_t>(0x00 + voice), amplitude);
    rig.Saa(static_cast<uint8_t>(0x08 + voice), 227);
    rig.Saa(static_cast<uint8_t>(0x10 + voice / 2), static_cast<uint8_t>(voice & 1 ? 0x30 : 0x03));
    rig.Saa(0x14, static_cast<uint8_t>(1u << voice));
    rig.Saa(0x1C, 0x01);
}

/// The MIDI bytes as the 128K-style send routine bit-banges them on R14 bit 2 (other bits high), from `t0`
uint64_t BitBangMidi(Rig& rig, const std::vector<uint8_t>& bytes, uint64_t t0)
{
    auto level = [](bool high) { return static_cast<uint8_t>(high ? 0xFF : 0xFB); };
    uint64_t t = t0;
    for (uint8_t byte : bytes)
    {
        rig.OutAt(t, kYmData, level(false));  // start bit
        t += kMidiBitT;
        for (int b = 0; b < 8; b++)
        {
            rig.OutAt(t, kYmData, level((byte >> b) & 1));
            t += kMidiBitT;
        }
        rig.OutAt(t, kYmData, level(true));  // stop bit
        t += kMidiBitT;
    }
    return t;
}
}  // namespace

class MultiSoundCard_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = std::make_unique<EmulatorContext>(LoggerLevel::LogError);
        _context->config.sound.gs_vol = 8000;
        _context->config.frame = static_cast<uint32_t>(kFrameTicks);
        _context->config.frame_duration_us = 20480;
        _context->emulatorState.current_z80_frequency_multiplier = 1;
        _context->emulatorState.hw_turbo_ratio_applied = 1;
    }

    /// The card with a GS ROM that halts at once (DI : HALT): the tests that do not exercise the GS keep its CPU cheap
    MultiSoundCardConfig QuietConfig()
    {
        MultiSoundCardConfig config;
        config.gsRomPath = WriteGsRom("multisound-gs-halt.rom", {0xF3, 0x76});
        return config;
    }

    std::unique_ptr<EmulatorContext> _context;
};

/// region <Control byte and decode>

TEST_F(MultiSoundCard_Test, WorkedExampleControlByteYmRegistersAndSaaEnable)
{
    // requirements.md §1: #F7 is a control byte (top four bits 1111): bit 0 = 1 selects U10 (chip select 1), bit 1 = 1
    // register read mode, bit 2 = 1 FM muted, bit 3 = 0 SAA clock on; the byte also reaches U10 as an address
    Rig rig(_context.get(), QuietConfig());
    rig.Out(kYmRegister, 0xF7);

    MultiSoundCardReport r = rig.Report();
    EXPECT_EQ(r.latches.ymChip, 1);
    EXPECT_FALSE(r.latches.ymReadStatus);
    EXPECT_TRUE(r.latches.fmMuted);
    EXPECT_TRUE(r.latches.saaClock);
    EXPECT_EQ(r.ym[1].address, 0xF7) << "the control byte is also U10's address write";
    EXPECT_EQ(r.ym[0].address, 0x00) << "U4 latched nothing";

    // AY register 7 of the selected YM (U10), then SAA #1C = 1
    rig.Reg(0x07, 0x38);
    rig.Saa(0x1C, 0x01);
    r = rig.Report();
    EXPECT_EQ(r.ym[1].ssgRegisters[7], 0x38);
    EXPECT_EQ(r.ym[0].ssgRegisters[7], 0x00);
    EXPECT_EQ(r.saa.registers[0x1C], 0x01);
    EXPECT_TRUE(r.saa.soundEnabled);
    EXPECT_TRUE(r.saa.clockEnabled);

    // IN #FFFD in register mode returns U10's register 7; #BFFD asserts IORQGE but floats
    bool drives = false;
    rig.Out(kYmRegister, 0x07);
    EXPECT_EQ(rig.In(kYmRegister, &drives), 0x38);
    EXPECT_TRUE(drives);
    EXPECT_EQ(rig.card.Peek(kYmRegister), 0x38);
    rig.In(kYmData, &drives);
    EXPECT_FALSE(drives);
    EXPECT_TRUE(rig.card.Iorqge(kYmData));

    // Sound: U10 tone A, SAA voice 0 left. FM is muted and has no note: its row stays silent
    rig.Reg(0x00, 0x40);
    rig.Reg(0x08, 0x0F);
    ProgramSaaTone(rig, 0, 0x0F);
    rig.RunFrames(4);
    EXPECT_GT(rig.Swing(MultiSoundRow::Ssg, 0), 1000.0) << "U10 SSG A, left";
    EXPECT_LT(rig.Swing(MultiSoundRow::Ssg, 1), 20.0) << "SSG A has no right weight";
    EXPECT_GT(rig.Swing(MultiSoundRow::Saa, 0), 1000.0) << "SAA voice 0, left amplitude";
    EXPECT_LT(rig.Swing(MultiSoundRow::Saa, 1), 20.0);
    EXPECT_EQ(rig.Swing(MultiSoundRow::Fm, 0), 0.0);
    EXPECT_EQ(rig.Swing(MultiSoundRow::Dac, 0), 0.0);
    EXPECT_EQ(rig.Swing(MultiSoundRow::Midi, 0), 0.0);
}

TEST_F(MultiSoundCard_Test, FmIsMutedAfterResetUntilAControlByteClearsBit2)
{
    // Reset: FM*_ENA driven low. A note programmed on U4 stays silent until #F0 (bit 2 = 0); #F4 mutes it again; a bus
    // reset restores the muted state and resets the chips
    Rig rig(_context.get(), QuietConfig());
    MultiSoundCardReport r = rig.Report();
    EXPECT_TRUE(r.latches.fmMuted);
    EXPECT_EQ(r.latches.ymChip, 0);
    EXPECT_FALSE(r.latches.ymReadStatus) << "reset: IN #FFFD reads the register, not the status";
    EXPECT_FALSE(r.latches.saaClock);

    ProgramFmNote(rig);
    rig.RunFrames(2);
    EXPECT_EQ(rig.Swing(MultiSoundRow::Fm, 0), 0.0) << "muted after reset";

    rig.Out(kYmRegister, 0xF0);
    rig.RunFrames(1);
    rig.Clear();
    rig.RunFrames(2);
    const double on = rig.Swing(MultiSoundRow::Fm, 0);
    EXPECT_GT(on, 5000.0) << "FM on";
    EXPECT_NEAR(rig.Swing(MultiSoundRow::Fm, 0), rig.Swing(MultiSoundRow::Fm, 1), 1.0) << "FM is centered";

    // Muted again: what is left is the coupling capacitor's discharge (3.2 Hz corner, 50 ms time constant)
    rig.Out(kYmRegister, 0xF4);
    rig.RunFrames(10);
    rig.Clear();
    rig.RunFrames(2);
    EXPECT_LT(rig.Swing(MultiSoundRow::Fm, 0), 0.01 * on) << "muted again";

    rig.Out(kYmRegister, 0xF0);
    rig.RunFrames(1);
    rig.card.BusReset(rig.t);
    r = rig.Report();
    EXPECT_TRUE(r.latches.fmMuted);
    EXPECT_EQ(r.ym[0].fmKeyOn[2], 0) << "the chips are reset too";
    rig.RunFrames(10);
    rig.Clear();
    rig.RunFrames(2);
    EXPECT_LT(rig.Swing(MultiSoundRow::Fm, 0), 0.01 * on);
}

TEST_F(MultiSoundCard_Test, TsfmChipSwitchFfStopsTheSaaAndMutesFm)
{
    // A plain TurboSound chip switch #FF has bits 2 and 3 set: FM muted, SAA clock stopped (hardware-reference §3.3
    // item 3); the SAA's counters freeze. #FE does the same with U4
    Rig rig(_context.get(), QuietConfig());
    rig.Out(kYmRegister, 0xF0);
    ProgramSaaTone(rig, 0, 0xFF);
    rig.RunFrames(1);
    const uint64_t running = rig.card.Saa().ChipClocks();
    rig.RunFrames(1);
    EXPECT_GT(rig.card.Saa().ChipClocks(), running) << "SAA clock on after #F0";

    rig.Out(kYmRegister, 0xFF);
    MultiSoundCardReport r = rig.Report();
    EXPECT_FALSE(r.latches.saaClock);
    EXPECT_TRUE(r.latches.fmMuted);
    EXPECT_EQ(r.latches.ymChip, 1);
    rig.RunFrames(1);
    const uint64_t stopped = rig.card.Saa().ChipClocks();
    rig.RunFrames(2);
    EXPECT_EQ(rig.card.Saa().ChipClocks(), stopped) << "the counters freeze";
    EXPECT_FALSE(rig.Report().saa.clockEnabled);

    rig.Out(kYmRegister, 0xFE);
    EXPECT_EQ(rig.Report().latches.ymChip, 0);
    EXPECT_FALSE(rig.Report().latches.saaClock);
}

TEST_F(MultiSoundCard_Test, DffdReachesTheYmWithoutIorqge)
{
    // #DFFD matches the YM register decode (A13 = 0): the card takes the write without IORQGE, so the machine's own
    // #DFFD paging latches it as well (hardware-reference §3.2)
    Rig rig(_context.get(), QuietConfig());
    EXPECT_FALSE(rig.card.Iorqge(0xDFFD));
    EXPECT_TRUE(rig.card.Iorqge(0xFFFD));

    rig.Out(0xDFFD, 0x07);
    rig.Out(kYmData, 0x3E);
    MultiSoundCardReport r = rig.Report();
    EXPECT_EQ(r.ym[0].address, 0x07);
    EXPECT_EQ(r.ym[0].ssgRegisters[7], 0x3E);

    bool drives = false;
    EXPECT_EQ(rig.In(0xDFFD, &drives), 0x3E) << "a #DFFD read drives the selected register (bus fight on the machine)";
    EXPECT_TRUE(drives);

    rig.Out(0xDFFD, 0xF1);  // a control byte through #DFFD: U10, status read mode (bit 1 = 0)
    r = rig.Report();
    EXPECT_EQ(r.latches.ymChip, 1);
    EXPECT_TRUE(r.latches.ymReadStatus);
}

TEST_F(MultiSoundCard_Test, RomLockBlocksSaaAndSoundriveFromRomCode)
{
    // The last M1 fetch from #0000-#3FFF locks the SAA and SounDrive ports (TR-DOS uses #1F / #FF); the YM ports stay
    // open; a fetch from #4000 up unlocks
    Rig rig(_context.get(), QuietConfig());
    rig.card.M1(0x3D2F);
    rig.Saa(0x1C, 0x01);
    rig.Out(0x001F, 0xC0);
    rig.Reg(0x07, 0x2A);
    MultiSoundCardReport r = rig.Report();
    EXPECT_EQ(r.saa.registers[0x1C], 0x00);
    EXPECT_EQ(r.ym[0].ssgRegisters[7], 0x2A);
    rig.RunFrames(1);
    EXPECT_EQ(rig.card.Dacs().Channel(1).volume, 0) << "SounDrive write ignored";

    rig.card.M1(0x8000);
    rig.Saa(0x1C, 0x01);
    rig.Out(0x001F, 0xC0);
    rig.RunFrames(1);
    EXPECT_EQ(rig.Report().saa.registers[0x1C], 0x01);
    EXPECT_EQ(rig.card.Dacs().Channel(1).volume, 0x3F);
    EXPECT_EQ(rig.card.Dacs().Channel(1).sample, 0xC0);
}

TEST_F(MultiSoundCard_Test, OptionsSwitchFunctionsAndTheirPortsOff)
{
    Rig rig(_context.get(), QuietConfig());
    MultiSoundOptions options;
    options.ym = false;
    options.saa = false;
    options.gs = false;
    options.sd = false;
    rig.card.SetOptions(options);

    bool drives = true;
    EXPECT_FALSE(rig.card.Iorqge(kYmRegister));
    EXPECT_FALSE(rig.card.Iorqge(kGsCommand));
    rig.In(kYmRegister, &drives);
    EXPECT_FALSE(drives);
    rig.In(kGsCommand, &drives);
    EXPECT_FALSE(drives);
    rig.Reg(0x07, 0x3F);
    rig.Out(kYmRegister, 0xF0);  // neither YM latches nor the SAA clock
    rig.Saa(0x1C, 0x01);
    rig.Out(kGsCommand, 0x2A);
    rig.Out(0x000F, 0xC0);
    rig.RunFrames(1);
    MultiSoundCardReport r = rig.Report();
    EXPECT_EQ(r.ym[0].ssgRegisters[7], 0x00);
    EXPECT_TRUE(r.latches.fmMuted);
    EXPECT_FALSE(r.latches.saaClock);
    EXPECT_EQ(r.saa.registers[0x1C], 0x00);
    EXPECT_EQ(r.gs.commandFromHost, 0x00) << "the GS never saw #BB";
    EXPECT_EQ(rig.card.Dacs().Channel(0).volume, 0);

    // SAA only: a control byte still starts the SAA clock (its own decode, no IORQGE), the YM stays untouched
    options.saa = true;
    rig.card.SetOptions(options);
    rig.Out(kYmRegister, 0xF0);
    r = rig.Report();
    EXPECT_TRUE(r.latches.saaClock);
    EXPECT_TRUE(r.latches.fmMuted);
    EXPECT_EQ(r.ym[0].address, 0x00);

    // Classic control mask (unofficial issue #11 patch) with the SAA off: #F7 is a register address, not control
    options = MultiSoundOptions{};
    options.saa = false;
    options.ctrlMask = MultiSoundCtrlMask::Classic;
    rig.card.SetOptions(options);
    rig.Out(kYmRegister, 0xF7);
    r = rig.Report();
    EXPECT_EQ(r.latches.ymChip, 0);
    EXPECT_EQ(r.ym[0].address, 0xF7);
    options.ctrlMask = MultiSoundCtrlMask::Pro;
    rig.card.SetOptions(options);
    rig.Out(kYmRegister, 0xF7);
    EXPECT_EQ(rig.Report().latches.ymChip, 1);
}

/// endregion </Control byte and decode>

/// region <Mixer rows>

TEST_F(MultiSoundCard_Test, RowsCarryTheBoardWeights)
{
    // SSG: A left 0.417, B centre 0.213 both sides, C right 0.417 (ACB with B in the centre); SAA and DAC on their own
    // side; FM centred (FmIsMuted... checks it)
    // One card per channel: the SSG output is unipolar, so a channel switched off leaves its coupling capacitor
    // discharging (0.66 Hz corner) - a tail that is real but would blur the next measurement
    auto measure = [&](uint8_t volumeRegister, double out[2])
    {
        Rig rig(_context.get(), QuietConfig());
        rig.Out(kYmRegister, 0xF0);
        rig.Reg(0x07, 0x38);   // tones on, noise off
        const uint8_t tonePeriods[] = {0, 2, 4};
        for (uint8_t reg : tonePeriods)
            rig.Reg(reg, 0x40);
        rig.Reg(volumeRegister, 0x0F);
        rig.RunFrames(2);
        rig.Clear();
        rig.RunFrames(3);
        out[0] = rig.Swing(MultiSoundRow::Ssg, 0);
        out[1] = rig.Swing(MultiSoundRow::Ssg, 1);
    };
    double a[2], b[2], c[2];
    measure(8, a);
    measure(9, b);
    measure(10, c);
    EXPECT_GT(a[0], 1000.0);
    EXPECT_EQ(a[1], 0.0) << "A has no right weight";
    EXPECT_EQ(c[0], 0.0) << "C has no left weight";
    EXPECT_NEAR(c[1] / a[0], 1.0, 0.03);
    EXPECT_NEAR(b[0] / b[1], 1.0, 0.001) << "B is centred";
    EXPECT_NEAR(b[0] / a[0], MultiSoundBoard::kWeightSsgCenter / MultiSoundBoard::kWeightSsgSide, 0.03);

    Rig rig(_context.get(), QuietConfig());
    rig.Out(kYmRegister, 0xF0);
    // SAA: voice 1 with a right amplitude only
    ProgramSaaTone(rig, 1, 0xF0);
    rig.RunFrames(1);
    rig.Clear();
    rig.RunFrames(2);
    EXPECT_GT(rig.Swing(MultiSoundRow::Saa, 1), 1000.0);
    EXPECT_LT(rig.Swing(MultiSoundRow::Saa, 0), 20.0);
}

TEST_F(MultiSoundCard_Test, SoundriveChannelsAreHardLeftAndRight)
{
    // DAC row: SounDrive #0F / #1F (channels 0, 1) left only, #4F / #5F (channels 2, 3) right only, no cross-feed
    struct Case
    {
        uint16_t port;
        int side;
    };
    for (const Case& c : {Case{0x000F, 0}, Case{0x001F, 0}, Case{0x004F, 1}, Case{0x005F, 1}})
    {
        Rig rig(_context.get(), QuietConfig());
        for (int i = 0; i < 200; i++)
            rig.Out(c.port, static_cast<uint8_t>((i & 8) ? 0xC0 : 0x40), 200);
        rig.RunFrames(1);
        EXPECT_GT(rig.Swing(MultiSoundRow::Dac, c.side), 1000.0) << std::hex << c.port;
        EXPECT_EQ(rig.Swing(MultiSoundRow::Dac, 1 - c.side), 0.0) << std::hex << c.port;
    }
}

/// endregion </Mixer rows>

/// region <General Sound and the shared DACs>

TEST_F(MultiSoundCard_Test, SoundriveAndGsShareTheDacsTheLaterStrobeWins)
{
    // GS program: DAC 0 volume 63, ~208 us of DJNZ (~730 host ticks), a sample fetch from #6000 (DAC 0), then IN #0B
    // (command flag <- volume 3 bit 5), HALT. A SounDrive write before the fetch loses the sample, one after it wins;
    // a SounDrive write to channel 3 sets the GS's volume 3 to 63, which #0B reads
    const std::vector<uint8_t> program = {
        0xF3,                    // DI
        0x3E, 0xC0,              // LD A,#C0
        0x32, 0x00, 0x60,        // LD (#6000),A  (RAM 1; a write strobes no DAC)
        0x3E, 0x3F,              // LD A,#3F
        0xD3, 0x06,              // OUT (#06),A   volume 0 = 63
        0x06, 0x00,              // LD B,0
        0x10, 0xFE,              // DJNZ $        256 x 13 T at 16 MHz
        0x3A, 0x00, 0x60,        // LD A,(#6000)  DAC 0 sample #C0
        0xDB, 0x0B,              // IN A,(#0B)
        0x76,                    // HALT
    };
    MultiSoundCardConfig config;
    config.gsRomPath = WriteGsRom("multisound-gs-dac.rom", program);

    {
        Rig early(_context.get(), config);
        early.OutAt(50, 0x005F, 0x10);     // SounDrive channel 3: volume 3 = 63 before the GS reads #0B
        early.OutAt(100, 0x000F, 0x40);    // SounDrive channel 0 before the GS sample
        early.RunFrames(1);
        EXPECT_EQ(early.card.Dacs().Channel(0).sample, MultiSoundLogic::ConvertSample(0xC0)) << "the GS strobe ends later";
        EXPECT_EQ(early.card.Dacs().Channel(0).volume, 0x3F);
        EXPECT_EQ(early.card.Dacs().LateEvents(), 0u);
        EXPECT_EQ(early.In(kGsCommand) & 0x01, 0x01) << "#0B read volume 3 bit 5 = 1 (SounDrive set it)";
    }
    {
        Rig late(_context.get(), config);
        late.OutAt(5000, 0x000F, 0x40);    // after the GS sample
        late.RunFrames(1);
        EXPECT_EQ(late.card.Dacs().Channel(0).sample, MultiSoundLogic::ConvertSample(0x40)) << "the SounDrive strobe ends later";
        EXPECT_EQ(late.In(kGsCommand) & 0x01, 0x00) << "volume 3 is 0";
    }
}

namespace
{
bool WaitGsStatus(Rig& rig, uint8_t mask, bool set, int polls)
{
    for (int i = 0; i < polls; i++)
    {
        if (((rig.In(kGsCommand) & mask) != 0) == set)
            return true;
        rig.Advance(300);
    }
    return false;
}
}  // namespace

TEST_F(MultiSoundCard_Test, GsBootsGs105bAndPlaysASampleThroughTheSharedDacsHardLeftAndRight)
{
    // Runtime ~300 ms, over the 50 ms budget on purpose: the real GS 1.05b firmware (data/rom/gs105b.rom) boots
    // through its POST and RAM probe (~200 emulated frames of a 16 MHz Z80), then a host uploads a sample (COM #38,
    // bytes, #D2) and plays it in GS channel 1 (#80) and 3 (#82). The board has no cross-feed: channels 1-2 left,
    // 3-4 right
    MultiSoundCardConfig config;
    Rig rig(_context.get(), config);
    ASSERT_TRUE(rig.Report().gs.romLoaded) << config.gsRomPath;
    EXPECT_EQ(rig.Report().gs.ramKB, 1024u);

    int frames = 0;
    while (!rig.Report().gs.firmwareReady && frames < 500)
    {
        rig.RunFrames(1);
        frames++;
    }
    ASSERT_LT(frames, 500) << "POST never completed";
    if (rig.In(kGsCommand) & 0x80)
        rig.In(kGsData);  // POST reply

    // COM #38: load a sample (unsigned 8-bit, a square of 32-byte periods)
    rig.Out(kGsCommand, 0x38);
    ASSERT_TRUE(WaitGsStatus(rig, 0x01, false, 200)) << "no ack for #38";
    const uint8_t fx = rig.In(kGsData);
    EXPECT_EQ(fx, 1) << "the first sample";
    for (int i = 0; i < 2048; i++)
    {
        rig.Out(kGsData, (i & 16) ? 0xE0 : 0x20);
        ASSERT_TRUE(WaitGsStatus(rig, 0x80, false, 50)) << "byte " << i << " not taken";
    }
    rig.Out(kGsCommand, 0xD2);
    ASSERT_TRUE(WaitGsStatus(rig, 0x01, false, 200)) << "no ack for #D2";

    // Play in GS channel 1 (DAC 0, left)
    rig.Clear();
    rig.Out(kGsData, fx);
    rig.Out(kGsCommand, 0x80);
    ASSERT_TRUE(WaitGsStatus(rig, 0x01, false, 200));
    rig.RunFrames(3);
    EXPECT_GT(rig.Swing(MultiSoundRow::Dac, 0), 500.0) << "GS channel 1 plays on the left";
    EXPECT_LT(rig.Swing(MultiSoundRow::Dac, 1), 1.0) << "no cross-feed";
    EXPECT_GT(rig.Report().gs.dacFetches, 0u);

    // Let it end, then channel 3 (DAC 2, right)
    rig.RunFrames(40);
    rig.Clear();
    rig.Out(kGsData, fx);
    rig.Out(kGsCommand, 0x82);
    ASSERT_TRUE(WaitGsStatus(rig, 0x01, false, 200));
    rig.RunFrames(3);
    EXPECT_GT(rig.Swing(MultiSoundRow::Dac, 1), 500.0) << "GS channel 3 plays on the right";
    EXPECT_LT(rig.Swing(MultiSoundRow::Dac, 0), 0.1 * rig.Swing(MultiSoundRow::Dac, 1))
        << "the left side only drifts (the first sample has ended; its last level decays through the coupling)";
    EXPECT_EQ(rig.card.Dacs().LateEvents(), 0u);
}

/// endregion </General Sound and the shared DACs>

/// region <MIDI>

TEST_F(MultiSoundCard_Test, MidiNoteBitBangedOnU4ReachesTheSynthAndU10DoesNotDrive)
{
    // U4 (chip select 0) IOA2 is the SAM2695's MIDI IN. A program makes port A an output and bit-bangs Note On
    // (#90 #3C #64) on R14 bit 2 at 31 250 baud after the synthesizer's 50 ms boot window: one voice on channel 1.
    // The same bytes on U10's R14 reach nothing
    MultiSoundCardConfig config = QuietConfig();
    config.midiBank = std::make_shared<TinyBank>();
    Rig rig(_context.get(), config);
    EXPECT_TRUE(rig.card.MidiBankLoaded());
    EXPECT_EQ(rig.Report().midi.bankStatus, "loaded");

    rig.Reg(0x0E, 0xFF);   // idle-high latch while still an input
    rig.Reg(0x07, 0x40);   // port A output: the pins show #FF
    rig.Out(kYmRegister, 0x0E);
    const uint64_t end = BitBangMidi(rig, {0x90, 0x3C, 0x64}, 200000);
    rig.t = end + 2 * kMidiBitT;
    rig.RunFrames(2);

    sam2695::SynthReport synth;
    rig.card.DescribeSynth(synth);
    EXPECT_EQ(synth.bytesReceived, 3u);
    EXPECT_EQ(synth.framingErrors, 0u);
    EXPECT_EQ(synth.activeVoices, 1u);
    EXPECT_EQ(synth.channels[0].activeVoices, 1);
    EXPECT_GT(rig.Swing(MultiSoundRow::Midi, 0), 100.0);
    EXPECT_GT(rig.Swing(MultiSoundRow::Midi, 1), 100.0);
    const uint64_t edges = rig.Report().midiLine.edges;

    // U10 (control byte bit 0 = 1, FM / SAA on): its pins are not wired
    rig.Out(kYmRegister, 0xF1);
    rig.Reg(0x0E, 0xFF);
    rig.Reg(0x07, 0x40);
    rig.Out(kYmRegister, 0x0E);
    const uint64_t end2 = BitBangMidi(rig, {0x91, 0x40, 0x64}, rig.t + 1000);
    rig.t = end2 + 2 * kMidiBitT;
    rig.RunFrames(1);
    rig.card.DescribeSynth(synth);
    EXPECT_EQ(synth.bytesReceived, 3u);
    EXPECT_EQ(synth.channels[1].activeVoices, 0);
    EXPECT_EQ(rig.Report().midiLine.edges, edges);
}

TEST_F(MultiSoundCard_Test, MissingBankLeavesTheSynthSilentAndTheReportSaysNoBank)
{
    MultiSoundCardConfig config = QuietConfig();
    config.midiBankPath = "midi/no-such-bank.sf2";
    Rig rig(_context.get(), config);
    const MultiSoundCardReport r = rig.Report();
    EXPECT_FALSE(r.midi.bankLoaded);
    EXPECT_EQ(r.midi.bankStatus, "no bank");
    EXPECT_NE(r.midi.bankError.find("not found"), std::string::npos) << r.midi.bankError;
    rig.RunFrames(1);
    EXPECT_EQ(rig.Swing(MultiSoundRow::Midi, 0), 0.0);
}

/// endregion </MIDI>

/// region <Real program>

TEST_F(MultiSoundCard_Test, TfmPlayerTracePlaysThroughTheCard)
{
    // CL-2 trace: the TFM Music Compiler 1.12 player's #FFFD / #BFFD writes for 100 frames (tfm-player-trace.msc).
    // Each frame starts with #F8 (U4, FM on, SAA off); the writes are spread over the frame. Both chips get their
    // key-ons and the FM row sounds. Runtime ~100 ms, over the 50 ms budget on purpose: the trace is 100 frames
    // and every frame renders all five rows (about 1 ms of card per emulated frame)
    MultiSoundScenario scenario;
    std::string error;
    const std::string path = TestPathHelper::GetTestDataPath("sound/multisound/scenarios/tfm-player-trace.msc");
    ASSERT_TRUE(ParseMultiSoundScenario(ReadText(path), scenario, error)) << error;

    // Group the writes into frames at each #F8 control byte
    std::vector<std::vector<MultiSoundCycle>> frames;
    for (const MultiSoundCycle& cycle : scenario.cycles)
    {
        if (cycle.op == MultiSoundCycle::Op::Out && cycle.address == kYmRegister && cycle.value == 0xF8)
            frames.emplace_back();
        if (!frames.empty())
            frames.back().push_back(cycle);
    }
    ASSERT_EQ(frames.size(), 100u);

    Rig rig(_context.get(), QuietConfig());
    rig.card.M1(0x61A8);
    uint8_t keyOnSeen[2] = {};
    size_t frameIndex = 0;
    for (const auto& frame : frames)
    {
        if (frameIndex++ == 1)
        {
            // The capture began after the player had loaded its instruments: the trace's first frame clears every
            // register (attack rate 0 = no sound) and the later frames only set frequencies and key-ons. A stand-in
            // instrument (algorithm 7, fastest attack, every operator at TL #20) on all three channels of both chips,
            // written once after the trace's clear, lets the key-ons be heard
            for (uint8_t select : {uint8_t{0xF8}, uint8_t{0xF9}})
            {
                rig.t = rig.frameEnd - kFrameTicks + 500;
                rig.Out(kYmRegister, select);
                for (uint8_t ch = 0; ch < 3; ch++)
                {
                    rig.Reg(static_cast<uint8_t>(0xB0 + ch), 0x07);
                    for (uint8_t op = 0; op < 16; op += 4)
                    {
                        rig.Reg(static_cast<uint8_t>(0x30 + op + ch), 0x01);   // MUL 1
                        rig.Reg(static_cast<uint8_t>(0x50 + op + ch), 0x1F);   // AR fastest
                        rig.Reg(static_cast<uint8_t>(0x40 + op + ch), 0x20);   // TL 0x20 (-24 dB: six voices stay unclipped)
                    }
                }
            }
        }
        rig.t = rig.frameEnd - kFrameTicks + 2000;
        for (const MultiSoundCycle& cycle : frame)
        {
            ASSERT_EQ(cycle.op, MultiSoundCycle::Op::Out);
            rig.Out(cycle.address, cycle.value, 60);
            if (cycle.address == kYmData)
            {
                // Key-on mirror of both chips after every data write (a key-on and its key-off can share a frame)
                for (int c = 0; c < 2; c++)
                    for (uint8_t k : rig.card.Ym().chip(c)->fmKeyOn)
                        keyOnSeen[c] |= k;
            }
        }
        rig.RunFrames(1);
    }
    EXPECT_NE(keyOnSeen[0], 0) << "U4 played FM notes";
    EXPECT_NE(keyOnSeen[1], 0) << "U10 played FM notes";
    EXPECT_FALSE(rig.Report().latches.fmMuted);
    EXPECT_FALSE(rig.Report().latches.saaClock) << "#F8 / #F9 stop the SAA";
    EXPECT_GT(rig.Swing(MultiSoundRow::Fm, 0), 5000.0);
    EXPECT_NEAR(rig.Swing(MultiSoundRow::Fm, 0), rig.Swing(MultiSoundRow::Fm, 1), 1.0);
    EXPECT_EQ(rig.Swing(MultiSoundRow::Saa, 0), 0.0);
}

/// endregion </Real program>

#endif  // UNREALNG_HAVE_SAM2695
