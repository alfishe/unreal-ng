// General Sound board profiles (gsprofile.h): the ZX-MultiSound's GS on the
// classic card's code (docs/inprogress/2026-10-03-zx-multisound/architecture.md
// §4.2, hardware-reference.md §4.2, RTL findings F5 / F6 / F8 / F10).
//
// The classic profile is pinned by the existing GS suites (soundchip_gs_test,
// the golden digests, intrate, porttrace, bootdiag); this file covers what the
// MultiSound profile changes: the 16 MHz CPU against the 12 MHz / 321 INT
// pulse, the CPLD memory map at 1 and 2 MB, no #33, the CPLD's GS-side port
// rules, the DAC sink into MultiSoundDacs and the TTD round trip.

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/cards/multisound/multisounddacs.h"
#include "emulator/slots/cards/multisound/multisoundlogic.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"

namespace
{
constexpr size_t kFixed = SoundChip_GeneralSound::TTD_FIXED_STATE_SIZE;

// Burn a program into a 32 KB ROM image in the per-process scratch dir; the
// rest is NOP. `isr` (optional) goes to #0038
std::string writeRom(const char* leafName, const std::vector<uint8_t>& program, const std::vector<uint8_t>& isr = {})
{
    std::vector<uint8_t> image(SoundChip_GeneralSound::ROM_SIZE, 0x00);
    memcpy(image.data(), program.data(), program.size());
    if (!isr.empty())
        memcpy(image.data() + 0x38, isr.data(), isr.size());
    std::string path = TestPathHelper::GetUniqueTestScratchPath(leafName);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    return path;
}

int64_t decodeGsCyclesAbs(const std::vector<uint8_t>& blob)
{
    uint64_t raw = 0;
    for (int i = 7; i >= 0; i--)
        raw = (raw << 8) | blob[13 + static_cast<size_t>(i)];
    return static_cast<int64_t>(raw);
}

// Program fragment: select GS page `page`, store `value` at #8000
void storeInPage(std::vector<uint8_t>& p, uint8_t page, uint8_t value)
{
    const uint8_t code[] = {0x3E, page, 0xD3, 0x00, 0x3E, value, 0x32, 0x00, 0x80};
    p.insert(p.end(), std::begin(code), std::end(code));
}

struct RecordingSink : IGSDacSink
{
    struct Event
    {
        uint64_t time;
        bool volume;
        int channel;
        uint8_t value;
    };
    std::vector<Event> events;
    IGSDacSink* forward = nullptr;

    void GsSample(uint64_t time, int channel, uint8_t value) override
    {
        events.push_back({time, false, channel, value});
        if (forward)
            forward->GsSample(time, channel, value);
    }
    void GsVolume(uint64_t time, int channel, uint8_t volume) override
    {
        events.push_back({time, true, channel, volume});
        if (forward)
            forward->GsVolume(time, channel, volume);
    }
};
} // namespace

class SoundChip_GeneralSound_Profile_Test : public ::testing::Test
{
protected:
    std::unique_ptr<EmulatorContext> ctx;

    void SetUp() override
    {
        ctx = std::make_unique<EmulatorContext>(LoggerLevel::LogError);
        ctx->config.sound.gs_vol = 8000;
        // Pentagon frame: 3.5 MHz base clock
        ctx->config.frame = 69888;
        ctx->config.frame_duration_us = 19968;
        ctx->emulatorState.current_z80_frequency_multiplier = 1;
        ctx->emulatorState.hw_turbo_ratio_applied = 1;
    }

    std::unique_ptr<SoundChip_GeneralSound> makeCard(const GSProfile& profile, size_t ramKB = 1024)
    {
        return std::make_unique<SoundChip_GeneralSound>(ctx.get(), ramKB, 44100, profile);
    }

    static void runFrames(SoundChip_GeneralSound& card, int frames)
    {
        for (int i = 0; i < frames; i++)
        {
            card.handleFrameStart();
            card.handleFrameEnd(SAMPLES_PER_FRAME);
        }
    }

    static std::vector<uint8_t> save(const SoundChip_GeneralSound& card)
    {
        std::vector<uint8_t> blob(card.TTDStateSize());
        card.TTDSaveState(blob.data());
        return blob;
    }
};

/// region <Timing>

TEST_F(SoundChip_GeneralSound_Profile_Test, DerivedTiming_ClassicKeepsTwelveMHzUnits)
{
    const GSProfile classic = GSProfile::Classic();
    EXPECT_EQ(classic.UnitsPerSecond(), 12000000u);
    EXPECT_EQ(classic.UnitsPerCpuCycle(), 1);
    EXPECT_EQ(classic.IntPeriodUnits(), 320);
    EXPECT_EQ(classic.IntLowUnits(), 0) << "classic INT is held until accepted";
    EXPECT_TRUE(classic.controlPort);

    // 16 MHz CPU, 12 MHz divider: 48 MHz units, a CPU cycle = 3, INT = 321 x 4, low 33 x 4
    const GSProfile ms = GSProfile::MultiSound(1024);
    EXPECT_EQ(ms.UnitsPerSecond(), 48000000u);
    EXPECT_EQ(ms.UnitsPerCpuCycle(), 3);
    EXPECT_EQ(ms.IntPeriodUnits(), 1284);
    EXPECT_EQ(ms.IntLowUnits(), 132);
    EXPECT_FALSE(ms.controlPort);
    EXPECT_EQ(ms.memoryMap, GSMemoryMap::MultiSound1Mb);
    EXPECT_EQ(GSProfile::MultiSound(2048).memoryMap, GSMemoryMap::MultiSound2Mb);
}

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_IntPeriodIs321ClocksOf12MHzAtA16MHzCpu)
{
    // Zeroed ROM: NOPs (4 T) with interrupts disabled for one Pentagon frame.
    // One frame = 69888 tacts at 3.5 MHz = 958464 units of 48 MHz
    auto card = makeCard(GSProfile::MultiSound(1024));
    runFrames(*card, 1);

    const GSActivityCounters& c = card->getActivityCounters();
    const int64_t frameUnits = 958464;
    // 16 MHz: 319488 T in the frame = 79872 NOPs (the last one may overshoot)
    EXPECT_GE(c.cpuSteps, 79872u);
    EXPECT_LE(c.cpuSteps, 79873u);
    // INT every 1284 units (37.383 kHz): 746 boundaries in 958464 units
    EXPECT_EQ(c.interruptPeriods, static_cast<uint64_t>(frameUnits / 1284));

    const int64_t gsCyclesAbs = decodeGsCyclesAbs(save(*card));
    EXPECT_EQ(gsCyclesAbs % 1284, 0);
    EXPECT_EQ(gsCyclesAbs, (frameUnits / 1284) * 1284);
}

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_IntPulseIsLostWhenNotAcceptedClassicIsHeld)
{
    // IM 1, then a loop that keeps interrupts off for 50 NOPs (200 T) and opens
    // a 24 T window; ISR at #38: EI : RET.
    // Classic (12 MHz): 228 T of the 320-cycle period is DI, the held request
    // is taken in the window - no loss. MultiSound: the DI stretch (684 units)
    // is far longer than the 132-unit pulse, so most pulses end unaccepted
    std::vector<uint8_t> program = {0xED, 0x56, 0x31, 0x00, 0x60};  // IM 1 : LD SP,#6000
    const size_t loop = program.size();
    program.push_back(0xF3);                                         // DI
    program.insert(program.end(), 50, 0x00);                         // 50 x NOP
    program.push_back(0xFB);                                         // EI
    program.push_back(0x00);                                         // NOP (the INT shadow ends)
    program.push_back(0xC3);                                         // JP loop
    program.push_back(static_cast<uint8_t>(loop));
    program.push_back(0x00);
    const std::string rom = writeRom("gs-profile-intpulse.rom", program, {0xFB, 0xC9});

    auto classic = makeCard(GSProfile::Classic(), 128);
    classic->loadROM(rom);
    runFrames(*classic, 1);
    const GSActivityCounters& cc = classic->getActivityCounters();
    EXPECT_EQ(cc.interruptsCoalesced, 0u) << "classic: the request waits for the window";
    EXPECT_GE(cc.interruptsAccepted + 1, cc.interruptPeriods);

    auto ms = makeCard(GSProfile::MultiSound(1024));
    ms->loadROM(rom);
    runFrames(*ms, 1);
    const GSActivityCounters& mc = ms->getActivityCounters();
    // Every boundary is either accepted or lost (one may still be in flight)
    EXPECT_LE(mc.interruptsAccepted + mc.interruptsCoalesced, mc.interruptPeriods);
    EXPECT_GE(mc.interruptsAccepted + mc.interruptsCoalesced + 1, mc.interruptPeriods);
    EXPECT_GT(mc.interruptsCoalesced, mc.interruptPeriods / 2) << "pulses that end in the DI stretch are lost";
    EXPECT_GT(mc.interruptsAccepted, 0u) << "pulses that reach the EI window are taken";
}

/// endregion </Timing>

/// region <Memory map>

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_1MbBankingFollowsTheCpldMap)
{
    // Page N < #10: RAM 1 at N x 32 KB; bit 4: RAM 2; bit 5 ignored on the
    // 1 MB build; bit 6 selects no RAM but makes the page non-zero (#40 = RAM 1
    // gma 0, not ROM); bit 7 is outside the page register (#80 = ROM)
    std::vector<uint8_t> p;
    storeInPage(p, 0x01, 0x11);
    storeInPage(p, 0x10, 0x22);
    storeInPage(p, 0x1F, 0x33);
    storeInPage(p, 0x21, 0x44);  // = page 1
    storeInPage(p, 0x40, 0x55);  // RAM 1 offset 0
    storeInPage(p, 0x80, 0x66);  // ROM: discarded
    const uint8_t tail[] = {0x3E, 0x77, 0x32, 0x00, 0x40, 0x76};  // LD A,#77 : LD (#4000),A : HALT
    p.insert(p.end(), std::begin(tail), std::end(tail));

    auto card = makeCard(GSProfile::MultiSound(1024));
    card->loadROM(writeRom("gs-profile-1mb.rom", p));
    runFrames(*card, 1);
    ASSERT_TRUE(card->isCPUHalted());
    EXPECT_EQ(card->getRamSizeKB(), 1024u);

    const std::vector<uint8_t> blob = save(*card);
    ASSERT_EQ(blob.size(), kFixed + 1024 * 1024);
    const uint8_t* ram = blob.data() + kFixed;
    EXPECT_EQ(ram[0x8000], 0x44) << "page 1 = RAM 1 #8000; page #21 aliases it (bit 5 ignored)";
    EXPECT_EQ(ram[512 * 1024], 0x22) << "page #10 = RAM 2, gma 0";
    EXPECT_EQ(ram[512 * 1024 + 15 * 0x8000], 0x33) << "page #1F = RAM 2, gma 15";
    EXPECT_EQ(ram[0x0000], 0x55) << "page #40 = RAM 1 gma 0 (not ROM); page #80 = ROM, write discarded";
    EXPECT_EQ(ram[0xC000], 0x77) << "#4000-#7FFF = RAM 1 chip address #C000-#FFFF";
}

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_2MbBankingUsesPageBits5And4)
{
    std::vector<uint8_t> p;
    storeInPage(p, 0x01, 0x11);  // RAM 1
    storeInPage(p, 0x12, 0x22);  // RAM 2, gma 2
    storeInPage(p, 0x21, 0x33);  // RAM 3, gma 1 (not an alias on 2 MB)
    storeInPage(p, 0x3F, 0x44);  // RAM 4, gma 15
    p.push_back(0x76);

    auto card = makeCard(GSProfile::MultiSound(2048), 2048);
    card->loadROM(writeRom("gs-profile-2mb.rom", p));
    runFrames(*card, 1);
    ASSERT_TRUE(card->isCPUHalted());
    EXPECT_EQ(card->getRamSizeKB(), 2048u);

    const std::vector<uint8_t> blob = save(*card);
    ASSERT_EQ(blob.size(), kFixed + 2048 * 1024);
    const uint8_t* ram = blob.data() + kFixed;
    const size_t chip = MultiSoundGsMapping::kRamChipBytes;
    EXPECT_EQ(ram[0x8000], 0x11);
    EXPECT_EQ(ram[chip + 2 * 0x8000], 0x22);
    EXPECT_EQ(ram[2 * chip + 0x8000], 0x33);
    EXPECT_EQ(ram[3 * chip + 15 * 0x8000], 0x44);
}

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_RamSizeIsTheBoards)
{
    EXPECT_EQ(makeCard(GSProfile::MultiSound(1024), 128)->getRamSizeKB(), 1024u);
    EXPECT_EQ(makeCard(GSProfile::MultiSound(2048), 512)->getRamSizeKB(), 2048u);
    EXPECT_EQ(makeCard(GSProfile::Classic(), 4096)->getRamSizeKB(), 512u) << "classic range unchanged";
}

/// endregion </Memory map>

/// region <Ports>

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_HasNoControlPort)
{
    // MPAG 1 then HALT: a classic #33 reset would put MPAG back to 0
    const std::vector<uint8_t> p = {0x3E, 0x01, 0xD3, 0x00, 0x76};
    auto card = makeCard(GSProfile::MultiSound(1024));
    card->loadROM(writeRom("gs-profile-no33.rom", p));
    runFrames(*card, 1);
    ASSERT_EQ(card->getMPAG(), 1);

    card->portDeviceOutMethod(SoundChip_GeneralSound::PORT_CONTROL, 0x80);  // reset
    card->portDeviceOutMethod(SoundChip_GeneralSound::PORT_CONTROL, 0x40);  // NMI
    card->triggerNMI();
    runFrames(*card, 1);
    EXPECT_EQ(card->getMPAG(), 1) << "#33 bit 7 must not reset the MultiSound GS";
    EXPECT_EQ(card->getActivityCounters().nmisAccepted, 0u) << "no NMI line";
    EXPECT_TRUE(card->isCPUHalted());

    // The same sequence resets a classic card
    auto classic = makeCard(GSProfile::Classic(), 128);
    classic->loadROM(writeRom("gs-profile-33.rom", p));
    runFrames(*classic, 1);
    classic->portDeviceOutMethod(SoundChip_GeneralSound::PORT_CONTROL, 0x80);
    EXPECT_EQ(classic->getMPAG(), 0);
}

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_GsPortsFollowTheCpld)
{
    // Results go to #4000.. (RAM 1 chip address #C000..)
    const std::vector<uint8_t> p = {
        0x3E, 0x5A, 0xD3, 0x03,  // OUT (3),#5A: reply register, data flag set
        0xDB, 0x02,              // IN A,(2): data flag cleared
        0xDB, 0x13,              // IN A,(#13) = port 3 (A3-A0): flag set, reply kept, reads #FF
        0x32, 0x00, 0x40,        // -> #4000
        0xDB, 0x10,              // IN A,(#10) = port 0, undecoded: #FF
        0x32, 0x01, 0x40,        // -> #4001
        0xDB, 0x0E,              // IN A,(#0E): undecoded, #FF
        0x32, 0x02, 0x40,        // -> #4002
        0xDB, 0x24,              // IN A,(#24) = port 4: status {data, 111111, command}
        0x32, 0x03, 0x40,        // -> #4003
        0x3E, 0x20, 0xD3, 0x19,  // OUT (#19),#20 = port 9: volume 3 = #20
        0x3E, 0x00, 0xD3, 0x06,  // OUT (6),0: volume 0 = 0
        0xDB, 0x0B,              // IN A,(#0B): command flag <- volume 3 bit 5 (= 1)
        0x76                     // HALT
    };
    auto card = makeCard(GSProfile::MultiSound(1024));
    card->loadROM(writeRom("gs-profile-ports.rom", p));
    runFrames(*card, 1);
    ASSERT_TRUE(card->isCPUHalted());

    const std::vector<uint8_t> blob = save(*card);
    const uint8_t* window = blob.data() + kFixed + 0xC000;
    EXPECT_EQ(window[0], 0xFF) << "port 3 read";
    EXPECT_EQ(window[1], 0xFF) << "port #10 mirrors port 0, which reads undecoded";
    EXPECT_EQ(window[2], 0xFF) << "undecoded port";
    EXPECT_EQ(window[3], 0xFE) << "port 4 = data flag set, bits 1-6 = 1, command flag clear";
    EXPECT_EQ(card->getDataToHost(), 0x5A) << "a port 3 read leaves the reply register";
    EXPECT_EQ(card->getChannelVolume(3), 0x20) << "port #19 mirrors port 9";
    EXPECT_EQ(card->getStatusRaw() & 0x01, 0x01) << "#0B copies volume 3 bit 5 (vol3 in top.v)";
    EXPECT_EQ(card->getStatusRaw() & 0x80, 0x80) << "port 3 read set the data flag";
}

/// endregion </Ports>

/// region <DAC sink>

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_DacSinkFeedsMultiSoundDacsWithHostTimes)
{
    // LD B,0 : DJNZ $ (7 + 255 x 13 + 8 = 3330 T), then a sample fetch from
    // channel 1 (#6100) and a volume write to channel 1 (port 7)
    const std::vector<uint8_t> p = {
        0x06, 0x00, 0x10, 0xFE,  // T 0: LD B,0 ; T 7: DJNZ $
        0x3E, 0xC0,              // T 3330: LD A,#C0
        0x32, 0x00, 0x61,        // T 3337: LD (#6100),A (a write: no DAC strobe)
        0x3A, 0x00, 0x61,        // T 3350: LD A,(#6100) - sample strobe, channel 1
        0x3E, 0x3F,              // T 3363: LD A,#3F
        0xD3, 0x07,              // T 3370: OUT (7),A - volume strobe, channel 1
        0x76
    };

    MultiSoundDacs dacs;
    MultiSoundDacsConfig cfg;
    cfg.hostTickRate = 3500000;  // the host's audio T-states
    dacs.Configure(cfg);
    RecordingSink sink;
    sink.forward = &dacs;

    auto card = makeCard(GSProfile::MultiSound(1024, &sink));
    card->loadROM(writeRom("gs-profile-sink.rom", p));
    runFrames(*card, 1);
    ASSERT_TRUE(card->isCPUHalted());

    // Instruction start in 48 MHz units (3 per T) -> 3.5 MHz host tacts, truncated:
    // 3350 T = 10050 units = 732.8 tacts; 3370 T = 10110 units = 737.2 tacts
    ASSERT_EQ(sink.events.size(), 2u);
    EXPECT_FALSE(sink.events[0].volume);
    EXPECT_EQ(sink.events[0].channel, 1);
    EXPECT_EQ(sink.events[0].value, 0xC0);
    EXPECT_EQ(sink.events[0].time, 732u);
    EXPECT_TRUE(sink.events[1].volume);
    EXPECT_EQ(sink.events[1].channel, 1);
    EXPECT_EQ(sink.events[1].value, 0x3F);
    EXPECT_EQ(sink.events[1].time, 737u);

    // The shared DACs apply them at those times
    dacs.Run(736);
    EXPECT_EQ(dacs.Channel(1).sample, MultiSoundLogic::ConvertSample(0xC0));
    EXPECT_EQ(dacs.Channel(1).volume, 0) << "the volume strobe ends at tact 737";
    dacs.Run(737);
    EXPECT_EQ(dacs.Channel(1).volume, 0x3F);
    EXPECT_GT(dacs.OutputLeft(), 0);
    EXPECT_EQ(dacs.OutputRight(), 0);

    // The card's own mix is replaced: its buffer stays silent, its latches still report
    const int16_t* buffer = card->getBuffer();
    for (size_t i = 0; i < 2 * SAMPLES_PER_FRAME; i++)
        ASSERT_EQ(buffer[i], 0) << "sample " << i;
    EXPECT_EQ(card->getChannelSample(1), 0xC0);
    EXPECT_EQ(card->getChannelVolume(1), 0x3F);
}

/// endregion </DAC sink>

/// region <TTD>

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_TtdRoundTripContinuesIdentically)
{
    // IM 1, EI, a store loop; ISR: INC (#4000), EI, RET - exercises the pulse INT,
    // the timing fields in 48 MHz units and the 1 MB RAM image
    const std::vector<uint8_t> p = {
        0xED, 0x56, 0x31, 0x00, 0x60,  // IM 1 : LD SP,#6000
        0x3E, 0x01, 0xD3, 0x00,        // MPAG 1
        0xFB,                          // EI
        0x21, 0x00, 0x80,              // LD HL,#8000
        0x77, 0x2C, 0x18, 0xFC         // L: LD (HL),A : INC L : JR L (stays in #8000-#80FF)
    };
    const std::vector<uint8_t> isr = {0x3A, 0x00, 0x40, 0x3C, 0x32, 0x00, 0x40, 0xFB, 0xC9};
    const std::string rom = writeRom("gs-profile-ttd.rom", p, isr);

    auto a = makeCard(GSProfile::MultiSound(1024));
    a->loadROM(rom);
    runFrames(*a, 1);
    const std::vector<uint8_t> blobA = save(*a);
    ASSERT_EQ(blobA.size(), kFixed + 1024 * 1024);
    EXPECT_GT(a->getActivityCounters().interruptsAccepted, 0u);

    auto b = makeCard(GSProfile::MultiSound(1024));
    b->loadROM(rom);
    b->TTDLoadState(blobA.data());
    EXPECT_EQ(save(*b), blobA) << "load + save is the identity";
    EXPECT_EQ(b->TTDHashState(), a->TTDHashState());

    runFrames(*a, 2);
    runFrames(*b, 2);
    EXPECT_EQ(save(*b), save(*a)) << "a restored card continues bit for bit";
}

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_MatchesTheEngineDescriptorWithItsRamAsRegion)
{
    // The time-travel engine's contract for the MultiSound GS (registered by the
    // board, MS-5): the descriptor's size is the blob's, the firmware is
    // fingerprinted, the 1-2 MB RAM is one region of 4 KB pieces outside the
    // engine's blob, and the card is synced to its frame base after a frame
    const std::string rom = writeRom("gs-profile-engine.rom", {0xF3, 0x76});  // DI : HALT
    for (size_t ramKB : {size_t{1024}, size_t{2048}})
    {
        SCOPED_TRACE(ramKB);
        auto card = makeCard(GSProfile::MultiSound(ramKB), ramKB);
        card->loadROM(rom);

        const ttd::TTDDeviceDescriptor d = card->TTDDescribe();
        EXPECT_EQ(d.stateSize, kFixed + ramKB * 1024);
        EXPECT_TRUE(d.runsBehindCpu);
        std::vector<uint8_t> image(SoundChip_GeneralSound::ROM_SIZE, 0x00);
        image[0] = 0xF3;
        image[1] = 0x76;
        EXPECT_EQ(d.firmwareFingerprint, ttd::FirmwareFingerprint(image.data(), image.size()));

        // Its own ids next to a GS card's (MS-5): device MultiSoundGs, region MultiSoundGsRam
        EXPECT_EQ(card->TTDPeripheralId(), ttd::PeripheralId::MultiSoundGs);
        ttd::TTDPeripheralRegistry registry;
        registry.Register(ttd::PeripheralId::MultiSoundGs, card.get());
        std::string error;
        EXPECT_TRUE(registry.CheckDeviceTable(error)) << error;

        std::vector<ttd::TTDDeviceRegion> regions;
        card->TTDRegions(regions);
        ASSERT_EQ(regions.size(), 1u);
        EXPECT_EQ(regions[0].desc.id, ttd::TTDRegionId::MultiSoundGsRam);
        EXPECT_EQ(regions[0].desc.name, "multisound.gs.ram");
        EXPECT_EQ(regions[0].desc.bytes, ramKB * 1024);
        EXPECT_EQ(regions[0].desc.pieces, ramKB * 1024 / ttd::kTTDPieceSize);

        uint8_t id = 0;
        std::vector<uint8_t> state;
        ASSERT_TRUE(card->TTDStateWithoutRegions(id, state));
        EXPECT_EQ(id, static_cast<uint8_t>(ttd::PeripheralId::MultiSoundGs));
        EXPECT_EQ(state.size(), kFixed);

        runFrames(*card, 1);
        card->handleFrameStart();
        int64_t offset = -1;
        EXPECT_TRUE(card->TTDSyncedTime(offset)) << "offset " << offset;
        card->handleFrameEnd(SAMPLES_PER_FRAME);
    }
}

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_RegionTrackingMarksEveryChipAndRestoresWithoutTheBlobRam)
{
    // Writes through the CPLD map land in all four 512 KB chips (2 MB): the
    // armed tracker marks the piece of each; a restore of the fixed state plus
    // the region memory (the engine's path) continues like a whole-blob restore
    std::vector<uint8_t> p = {0x31, 0x00, 0x60};  // LD SP,#6000
    for (uint8_t page : {uint8_t{0x01}, uint8_t{0x11}, uint8_t{0x21}, uint8_t{0x31}})
        storeInPage(p, page, page);
    const uint8_t loop[] = {0x3E, 0x05, 0xD3, 0x00, 0x21, 0x00, 0x80, 0x34, 0x18, 0xFD};  // MPAG 5 : INC (#8000) loop
    p.insert(p.end(), std::begin(loop), std::end(loop));
    const std::string rom = writeRom("gs-profile-regions.rom", p);

    auto a = makeCard(GSProfile::MultiSound(2048), 2048);
    a->loadROM(rom);
    std::vector<ttd::TTDDeviceRegion> regions;
    a->TTDRegions(regions);
    ASSERT_EQ(regions.size(), 1u);
    ttd::TTDRegionTracker* tracker = regions[0].tracker;
    ASSERT_NE(tracker, nullptr);
    a->TTDArmRegions(true);
    runFrames(*a, 1);
    std::vector<uint32_t> dirty;
    tracker->CollectAndClear(dirty);
    std::vector<bool> chipWritten(4, false);
    for (uint32_t piece : dirty)
        chipWritten[size_t(piece) * ttd::kTTDPieceSize / (512 * 1024)] = true;
    for (size_t chip = 0; chip < 4; chip++)
        EXPECT_TRUE(chipWritten[chip]) << "RAM chip " << chip + 1;
    for (uint32_t piece : dirty)
        EXPECT_NE(std::memcmp(regions[0].desc.memory + size_t(piece) * ttd::kTTDPieceSize,
                              std::vector<uint8_t>(ttd::kTTDPieceSize, 0).data(), ttd::kTTDPieceSize),
                  0)
            << "a marked piece holds a write (piece " << piece << ")";

    uint8_t id = 0;
    std::vector<uint8_t> fixed;
    ASSERT_TRUE(a->TTDStateWithoutRegions(id, fixed));
    const std::vector<uint8_t> ram(regions[0].desc.memory, regions[0].desc.memory + regions[0].desc.bytes);
    const std::vector<uint8_t> whole = save(*a);
    a->TTDArmRegions(false);

    auto b = makeCard(GSProfile::MultiSound(2048), 2048);
    b->loadROM(rom);
    std::vector<ttd::TTDDeviceRegion> regionsB;
    b->TTDRegions(regionsB);
    std::memcpy(regionsB[0].desc.memory, ram.data(), ram.size());
    ASSERT_TRUE(b->TTDLoadStateWithoutRegions(fixed.data(), fixed.size()));
    EXPECT_EQ(save(*b), whole);

    runFrames(*a, 2);
    runFrames(*b, 2);
    EXPECT_EQ(save(*b), save(*a)) << "the region restore continues bit for bit";
}

/// endregion </TTD>

/// region <Firmware>

namespace
{
// Host-side mailbox helpers (the bootdiag suite's primitives)
bool waitFlagClear(SoundChip_GeneralSound& card, uint8_t mask, int maxFrames)
{
    for (int i = 0; i < maxFrames; i++)
    {
        if (!(card.readStatus() & mask))
            return true;
        card.handleFrameStart();
        card.handleFrameEnd(SAMPLES_PER_FRAME);
    }
    return false;
}

int readGsByte(SoundChip_GeneralSound& card, int maxFrames)
{
    for (int i = 0; i < maxFrames; i++)
    {
        if (card.readStatus() & 0x80)
            return card.portDeviceInMethod(SoundChip_GeneralSound::PORT_DATA);
        card.handleFrameStart();
        card.handleFrameEnd(SAMPLES_PER_FRAME);
    }
    return -1;
}
} // namespace

TEST_F(SoundChip_GeneralSound_Profile_Test, MultiSound_Gs105bBootsAndReportsTheBoardsRam)
{
    // Runtime (~230 ms, over the 50 ms budget on purpose): boots the real GS
    // 1.05b firmware (the board's ROM) through its POST and RAM probe on the
    // CPLD memory map, ~200 / ~390 emulated frames - the only faithful check
    // that the map, the 16 MHz clock and the pulse INT carry the firmware
    for (size_t ramKB : {size_t{1024}, size_t{2048}})
    {
        auto card = makeCard(GSProfile::MultiSound(ramKB), ramKB);
        card->loadROM(GSProfile::kMultiSoundRomPath);
        ASSERT_TRUE(card->isROMLoaded()) << GSProfile::kMultiSoundRomPath;

        int frames = 0;
        while (card->getActivityCounters().volumeLatchWrites < 4 && frames < 500)
        {
            runFrames(*card, 1);
            frames++;
        }
        ASSERT_LT(frames, 500) << ramKB << " KB: POST never completed";
        if (card->readStatus() & 0x80)
            (void)card->portDeviceInMethod(SoundChip_GeneralSound::PORT_DATA);  // POST reply (NUMPG)

        // COM20: total RAM for modules and samples, 3 bytes L, M, H
        card->portDeviceOutMethod(SoundChip_GeneralSound::PORT_COMMAND, 0x20);
        ASSERT_TRUE(waitFlagClear(*card, 0x01, 100)) << ramKB << " KB: no ack for #20";
        int total = 0;
        for (int b = 0; b < 3; b++)
            total |= readGsByte(*card, 100) << (8 * b);
        // The probe pages from 1 up and stops at the first page that aliases one
        // already seen; the 16 KB behind the fixed window (page 1's upper half)
        // is held back as on the classic card. 1 MB: pages 1-#20 are distinct -
        // #20 is RAM 1 gma 0 (bit 5 ignored), so all 32 pairs = 1008 KB. 2 MB:
        // pages 1-#3F (63 pairs; page 0 is the ROM) = 2000 KB; RAM 1's first
        // 32 KB is reachable only through page #40, which the firmware leaves alone
        const int expected = ramKB == 1024 ? (32 * 32 - 16) * 1024 : (63 * 32 - 16) * 1024;
        EXPECT_EQ(total, expected) << ramKB << " KB (POST took " << frames << " frames)";
    }
}

/// endregion </Firmware>
