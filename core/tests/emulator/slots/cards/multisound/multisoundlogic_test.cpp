#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/multisoundscenario.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/tsfmplayerharness.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/cards/multisound/multisoundlogic.h"
#include "multisoundrtltables.h"

/// ZX-MultiSound card logic (tdd-card-logic.md §5). The expectations come from the card's own CPLD source run in
/// Verilator (tools/verification/multisound): the decode sweep tables (multisoundrtltables.h) and the scenario corpus
/// (testdata/sound/multisound/scenarios/*.expected). The hand-written tests below restate rules L1-L17 directly.

namespace
{
using Kind = MultiSoundBusAction::Kind;
using Source = MultiSoundReadResult::Source;

MultiSoundOptions AllOn(MultiSoundCtrlMask mask = MultiSoundCtrlMask::Pro)
{
    MultiSoundOptions options;
    options.ctrlMask = mask;
    return options;
}

MultiSoundLogic MakeLogic(const MultiSoundOptions& options = AllOn())
{
    MultiSoundLogic logic(options);
    logic.Reset();
    return logic;
}

std::string ReadText(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}
}

class MultiSoundLogic_Test : public ::testing::Test
{
};

class MultiSoundLogicSweep_Test : public ::testing::TestWithParam<size_t>
{
};

/// L1, L2, L8, L9, L16 (and L3-L7, L11, L12 through the control bytes and reads the sweep contains): every port,
/// read and write, ROM lock off and on, one DIP setting and control mask per instance, against the RTL's hash.
TEST_P(MultiSoundLogicSweep_Test, DecodeSweep)
{
    const MultiSoundSweepRow& row = MultiSoundSweepRows[GetParam()];

    MultiSoundScenario header;
    header.options.ctrlMask = row.classic ? MultiSoundCtrlMask::Classic : MultiSoundCtrlMask::Pro;
    MultiSoundLogicBus bus(header);

    uint64_t hash = MultiSoundHashSeed;
    uint32_t iorqge = 0, ymWrites = 0, saaWrites = 0, sdWrites = 0, drivenReads = 0;
    ForEachMultiSoundSweepCycle(row.dip, [&](const MultiSoundCycle& cycle)
    {
        const MultiSoundCycleRecord record = bus.Execute(cycle);
        hash = HashMultiSoundRecord(hash, record);
        iorqge += record.iorqge == 1;
        ymWrites += record.ymWrite != 0;
        saaWrites += record.saaWrite != 0;
        sdWrites += record.sdWrite != 0;
        drivenReads += record.readDriven != 0;
    });

    EXPECT_EQ(iorqge, row.iorqge);
    EXPECT_EQ(ymWrites, row.ymWrites);
    EXPECT_EQ(saaWrites, row.saaWrites);
    EXPECT_EQ(sdWrites, row.sdWrites);
    EXPECT_EQ(drivenReads, row.drivenReads);
    EXPECT_EQ(hash, row.hash) << "classic=" << int(row.classic) << " dip=" << int(row.dip)
                              << ": run tools/verification/multisound (mscosim sweep) for the first differing cycle";
}

INSTANTIATE_TEST_SUITE_P(RtlRows, MultiSoundLogicSweep_Test,
                         ::testing::Range(size_t{ 0 }, sizeof(MultiSoundSweepRows) / sizeof(MultiSoundSweepRows[0])));

/// The all-enabled pro sweep port by port at the witness ports: names the first differing port when the hash fails.
TEST_F(MultiSoundLogic_Test, DecodeWitnessPorts)
{
    auto expected = std::make_unique<uint16_t[]>(4u * 0x10000u);
    for (const MultiSoundWitnessRow& row : MultiSoundWitnessRows)
    {
        const uint32_t key = (static_cast<uint32_t>(row.lock) << 17) | (static_cast<uint32_t>(row.write) << 16) | row.port;
        expected[key] = row.code;
    }

    MultiSoundLogicBus bus(MultiSoundScenario{});
    uint8_t lock = 0;
    int failures = 0;
    ForEachMultiSoundSweepCycle(0x0F, [&](const MultiSoundCycle& cycle)
    {
        const MultiSoundCycleRecord record = bus.Execute(cycle);
        if (cycle.op == MultiSoundCycle::Op::M1)
            lock = cycle.address < 0x4000 ? 1 : 0;
        if ((cycle.op != MultiSoundCycle::Op::Out && cycle.op != MultiSoundCycle::Op::In) ||
            !IsMultiSoundWitnessPort(cycle.address) || failures >= 5)
            return;
        const uint8_t write = cycle.op == MultiSoundCycle::Op::Out ? 1 : 0;
        const uint32_t key = (static_cast<uint32_t>(lock) << 17) | (static_cast<uint32_t>(write) << 16) | cycle.address;
        const uint16_t code = MultiSoundEventCode(record);
        if (code != expected[key])
        {
            failures++;
            ADD_FAILURE() << FormatMultiSoundCycle(cycle) << " lock=" << int(lock) << ": event code 0x" << std::hex
                          << code << ", RTL 0x" << expected[key];
        }
    });
}

/// L3, L5, L6, L7 with the pro mask (d[7:4] = 1111)
TEST_F(MultiSoundLogic_Test, ControlBytePro)
{
    MultiSoundLogic logic = MakeLogic();

    // #F0: chip 0, status read, FM on, SAA clock on; also an address write to chip 0
    MultiSoundBusActions actions = logic.Write(0xFFFD, 0xF0);
    EXPECT_EQ(actions[0].kind, Kind::Control);
    EXPECT_EQ(actions[1].kind, Kind::YmAddress);
    EXPECT_EQ(actions[1].chip, 0);
    EXPECT_EQ(actions[1].value, 0xF0);
    EXPECT_EQ(logic.Latches().ymChip, 0);
    EXPECT_TRUE(logic.Latches().ymReadStatus);
    EXPECT_FALSE(logic.Latches().fmMuted);
    EXPECT_TRUE(logic.Latches().saaClock);

    // #F1: the address write goes to the newly selected chip
    actions = logic.Write(0xFFFD, 0xF1);
    EXPECT_EQ(actions[1].kind, Kind::YmAddress);
    EXPECT_EQ(actions[1].chip, 1);

    // A plain TurboSound chip switch #FF: chip 1, register read, FM muted, SAA stopped
    logic.Write(0xFFFD, 0xFF);
    EXPECT_EQ(logic.Latches().ymChip, 1);
    EXPECT_FALSE(logic.Latches().ymReadStatus);
    EXPECT_TRUE(logic.Latches().fmMuted);
    EXPECT_FALSE(logic.Latches().saaClock);

    // #F0-#F7 are control bytes on this card (register addresses on a classic TSFM)
    actions = logic.Write(0xFFFD, 0xF7);
    EXPECT_EQ(actions[0].kind, Kind::Control);
    EXPECT_EQ(logic.Latches().ymChip, 1);

    // #EF is an ordinary address write
    actions = logic.Write(0xFFFD, 0xEF);
    EXPECT_EQ(actions[0].kind, Kind::YmAddress);
    EXPECT_EQ(actions[1].kind, Kind::None);

    // YM off, SAA on: the SAA clock still follows control bytes, nothing reaches a YM, no IORQGE
    MultiSoundOptions saaOnly = AllOn();
    saaOnly.ym = false;
    logic.Configure(saaOnly);
    actions = logic.Write(0xFFFD, 0xF0);
    EXPECT_EQ(actions[0].kind, Kind::Control);
    EXPECT_EQ(actions[1].kind, Kind::None);
    EXPECT_TRUE(logic.Latches().saaClock);
    EXPECT_EQ(logic.Latches().ymChip, 1);       // YM latches untouched
    EXPECT_FALSE(logic.Iorqge(0xFFFD));

    // SAA off: control bytes still switch the YM latches, the SAA clock keeps its state
    MultiSoundOptions ymOnly = AllOn();
    ymOnly.saa = false;
    logic.Configure(ymOnly);
    logic.Write(0xFFFD, 0xF8);
    EXPECT_EQ(logic.Latches().ymChip, 0);
    EXPECT_TRUE(logic.Latches().saaClock);
}

/// L4: the issue #11 patch compares five bits while the SAA DIP is off
TEST_F(MultiSoundLogic_Test, ControlByteClassic)
{
    MultiSoundOptions options = AllOn(MultiSoundCtrlMask::Classic);
    options.saa = false;
    MultiSoundLogic logic = MakeLogic(options);

    MultiSoundBusActions actions = logic.Write(0xFFFD, 0xF5);       // Ball Quest's register address
    EXPECT_EQ(actions[0].kind, Kind::YmAddress);
    EXPECT_EQ(actions[0].value, 0xF5);
    EXPECT_TRUE(logic.Latches().fmMuted);                           // reset state kept

    actions = logic.Write(0xFFFD, 0xF9);
    EXPECT_EQ(actions[0].kind, Kind::Control);
    EXPECT_EQ(logic.Latches().ymChip, 1);
    EXPECT_FALSE(logic.Latches().fmMuted);

    // SAA on: the four-bit mask, as pro
    options.saa = true;
    logic.Configure(options);
    actions = logic.Write(0xFFFD, 0xF4);
    EXPECT_EQ(actions[0].kind, Kind::Control);
    EXPECT_EQ(logic.Latches().ymChip, 0);
    EXPECT_TRUE(logic.Latches().fmMuted);
}

/// §3.2: #DFFD (A13 = 0) reaches the YM, as data byte and as control byte, without IORQGE; reads drive the bus
TEST_F(MultiSoundLogic_Test, DffdReachesChipWithoutIorqge)
{
    MultiSoundLogic logic = MakeLogic();

    EXPECT_FALSE(logic.Iorqge(0xDFFD));
    EXPECT_FALSE(logic.Iorqge(0xC00D));
    EXPECT_TRUE(logic.Iorqge(0xFFFD));
    EXPECT_TRUE(logic.Iorqge(0xE00D));
    EXPECT_TRUE(logic.Iorqge(0xBFFD));
    EXPECT_TRUE(logic.Iorqge(0x800D));

    MultiSoundBusActions actions = logic.Write(0xDFFD, 0x07);
    EXPECT_EQ(actions[0].kind, Kind::YmAddress);
    EXPECT_EQ(actions[0].value, 0x07);

    actions = logic.Write(0xDFFD, 0xF1);
    EXPECT_EQ(actions[0].kind, Kind::Control);
    EXPECT_EQ(actions[1].kind, Kind::YmAddress);
    EXPECT_EQ(actions[1].chip, 1);

    const MultiSoundReadResult read = logic.Read(0xDFFD);
    EXPECT_TRUE(read.Drives());
    EXPECT_EQ(read.chip, 1);

    // The data port asserts IORQGE but the card does not drive a read
    EXPECT_FALSE(logic.Read(0xBFFD).Drives());
}

/// L10, L8, L9: the ROM-fetch lock
TEST_F(MultiSoundLogic_Test, RomLock)
{
    MultiSoundLogic logic = MakeLogic();
    EXPECT_FALSE(logic.Latches().romLock);

    logic.OnM1(0x0000);
    EXPECT_TRUE(logic.Latches().romLock);
    EXPECT_EQ(logic.Write(0x00FF, 0x01)[0].kind, Kind::None);
    EXPECT_EQ(logic.Write(0x001F, 0x80)[0].kind, Kind::None);
    EXPECT_EQ(logic.Write(0xFFFD, 0x05)[0].kind, Kind::YmAddress);     // YM and GS ignore the lock
    EXPECT_EQ(logic.Write(0x00B3, 0x06)[0].kind, Kind::GsData);

    logic.OnM1(0x3FFF);
    EXPECT_TRUE(logic.Latches().romLock);

    logic.OnM1(0x4000);
    EXPECT_FALSE(logic.Latches().romLock);
    EXPECT_EQ(logic.Write(0x01FF, 0x1C)[0].kind, Kind::SaaAddress);
    EXPECT_EQ(logic.Write(0x00FF, 0x20)[0].kind, Kind::SaaData);
    const MultiSoundBusAction sd = logic.Write(0x005F, 0x80)[0];
    EXPECT_EQ(sd.kind, Kind::SoundriveSample);
    EXPECT_EQ(sd.chip, 3);

    logic.OnM1(0xC000);
    EXPECT_FALSE(logic.Latches().romLock);
}

/// L12, L13: the GS mailbox registers and both flags
TEST_F(MultiSoundLogic_Test, GsMailboxFlags)
{
    MultiSoundLogic logic = MakeLogic();
    EXPECT_EQ(logic.GsStatus(), 0x7E);

    EXPECT_EQ(logic.Write(0x12B3, 0xA5)[0].kind, Kind::GsData);
    EXPECT_EQ(logic.GsStatus(), 0xFE);
    EXPECT_EQ(logic.GsPortRead(0x02), 0xA5);           // GS reads the data register: clears the data flag
    EXPECT_EQ(logic.GsStatus(), 0x7E);

    EXPECT_EQ(logic.Write(0x00BB, 0x1C)[0].kind, Kind::GsCommand);
    EXPECT_EQ(logic.GsPortRead(0x01), 0x1C);           // no flag change
    EXPECT_EQ(logic.GsPortRead(0x04), 0x7F);           // status port
    MultiSoundReadResult status = logic.Read(0x00BB);
    EXPECT_EQ(status.source, Source::GsStatus);
    EXPECT_EQ(status.value, 0x7F);
    logic.GsPortWrite(0x05, 0x00);                      // any access to port 5 clears the command flag
    EXPECT_EQ(logic.GsStatus(), 0x7E);

    logic.GsPortWrite(0x03, 0x3C);                      // output register, data flag set
    EXPECT_EQ(logic.GsStatus(), 0xFE);
    EXPECT_EQ(logic.Peek(0x00B3).value, 0x3C);
    EXPECT_EQ(logic.GsStatus(), 0xFE);                  // Peek has no side effect
    const MultiSoundReadResult output = logic.Read(0x00B3);
    EXPECT_EQ(output.source, Source::GsOutput);
    EXPECT_EQ(output.value, 0x3C);
    EXPECT_EQ(logic.GsStatus(), 0x7E);                  // host read clears the data flag

    // Both directions act on the flags: a GS read of port 3 sets, a GS write to port 2 clears
    EXPECT_EQ(logic.GsPortRead(0x03), 0xFF);
    EXPECT_TRUE(logic.Latches().dataFlag);
    logic.GsPortWrite(0x02, 0x00);
    EXPECT_FALSE(logic.Latches().dataFlag);

    // Port #0A: data flag = ~page bit 0; port #0B: command flag = volume 3 bit 5
    logic.GsPortWrite(0x00, 0x02);
    logic.GsPortRead(0x0A);
    EXPECT_TRUE(logic.Latches().dataFlag);
    logic.GsPortWrite(0x00, 0x01);
    logic.GsPortWrite(0x0A, 0x00);
    EXPECT_FALSE(logic.Latches().dataFlag);
    logic.GsPortWrite(0x09, 0x20);
    logic.GsPortRead(0x0B);
    EXPECT_TRUE(logic.Latches().commandFlag);
    logic.GsPortWrite(0x09, 0x1F);
    logic.GsPortRead(0x0B);
    EXPECT_FALSE(logic.Latches().commandFlag);
    logic.Write(0x005F, 0x80);                          // the SounDrive sets volume 3 to 63
    logic.GsPortRead(0x0B);
    EXPECT_TRUE(logic.Latches().commandFlag);

    // Only A3-A0 decode
    logic.GsPortWrite(0x13, 0x77);
    EXPECT_EQ(logic.Latches().gsOutput, 0x77);
    EXPECT_EQ(logic.GsPortRead(0xF4), logic.GsStatus());
    EXPECT_EQ(logic.GsPortRead(0x07), 0xFF);

    // With the GS DIP off the host ports are not decoded
    MultiSoundOptions noGs = AllOn();
    noGs.gs = false;
    logic.Configure(noGs);
    EXPECT_FALSE(logic.Iorqge(0x00B3));
    EXPECT_FALSE(logic.Read(0x00BB).Drives());
    EXPECT_EQ(logic.Write(0x00B3, 0x01)[0].kind, Kind::None);
}

/// L14, L15: shared DAC channels and the sample conversion
TEST_F(MultiSoundLogic_Test, DacArbitration)
{
    MultiSoundLogic logic = MakeLogic();

    EXPECT_EQ(MultiSoundLogic::ConvertSample(0x00), 0x7F);
    EXPECT_EQ(MultiSoundLogic::ConvertSample(0x7F), 0x00);
    EXPECT_EQ(MultiSoundLogic::ConvertSample(0x80), 0x80);
    EXPECT_EQ(MultiSoundLogic::ConvertSample(0xFF), 0xFF);
    EXPECT_EQ(MultiSoundLogic::SampleLevel(MultiSoundLogic::ConvertSample(0x00)), -127);
    EXPECT_EQ(MultiSoundLogic::SampleLevel(MultiSoundLogic::ConvertSample(0x7F)), 0);
    EXPECT_EQ(MultiSoundLogic::SampleLevel(MultiSoundLogic::ConvertSample(0x80)), 0);
    EXPECT_EQ(MultiSoundLogic::SampleLevel(MultiSoundLogic::ConvertSample(0xFF)), 127);
    EXPECT_EQ(MultiSoundLogic::VolumeGain64(62), 62);
    EXPECT_EQ(MultiSoundLogic::VolumeGain64(63), 64);

    // SounDrive: sample + volume 63; channel = {A6, A4}
    logic.Write(0x004F, 0x40);
    EXPECT_EQ(logic.Dac(2).sample, 0x3F);
    EXPECT_EQ(logic.Dac(2).volume, 0x3F);

    // GS volume (6 bits) and samples from memory reads at #6000-#7FFF, channel = A9-A8
    logic.GsPortWrite(0x08, 0xE5);
    EXPECT_EQ(logic.Dac(2).volume, 0x25);
    logic.GsMemoryRead(0x6200, 0xC0);
    EXPECT_EQ(logic.Dac(2).sample, 0xC0);
    logic.GsMemoryRead(0x7F00, 0x01);
    EXPECT_EQ(logic.Dac(3).sample, 0x7E);
    logic.GsMemoryRead(0x5FFF, 0x80);
    logic.GsMemoryRead(0x8000, 0x80);
    EXPECT_EQ(logic.Dac(3).sample, 0x7E);

    // Last writer wins: the SounDrive takes the channel back with volume 63
    logic.Write(0x004F, 0x80);
    EXPECT_EQ(logic.Dac(2).sample, 0x80);
    EXPECT_EQ(logic.Dac(2).volume, 0x3F);

    // The SounDrive ports with the DIP off or under the ROM lock change nothing
    MultiSoundOptions noSd = AllOn();
    noSd.sd = false;
    logic.Configure(noSd);
    logic.Write(0x004F, 0x00);
    EXPECT_EQ(logic.Dac(2).sample, 0x80);
}

/// L17: the CPLD reset branch
TEST_F(MultiSoundLogic_Test, ResetState)
{
    MultiSoundLogic logic = MakeLogic();
    logic.Write(0xFFFD, 0xF3);
    logic.Write(0x00B3, 0x11);
    logic.Write(0x00BB, 0x22);
    logic.GsPortWrite(0x00, 0x05);
    logic.GsPortWrite(0x03, 0x33);
    logic.Write(0x000F, 0x44);
    logic.OnM1(0x0000);

    logic.Reset();
    const MultiSoundLatches& latches = logic.Latches();
    EXPECT_EQ(latches.ymChip, 0);
    EXPECT_FALSE(latches.ymReadStatus);     // IN #FFFD returns the register, not the status
    EXPECT_TRUE(latches.fmMuted);
    EXPECT_FALSE(latches.saaClock);
    EXPECT_FALSE(latches.romLock);
    EXPECT_EQ(latches.gsData, 0);
    EXPECT_EQ(latches.gsCommand, 0);
    EXPECT_EQ(latches.gsPage, 0);
    EXPECT_EQ(latches.gsOutput, 0);
    EXPECT_FALSE(latches.dataFlag);
    EXPECT_FALSE(latches.commandFlag);
    for (int channel = 0; channel < 4; channel++)
    {
        EXPECT_EQ(logic.Dac(channel).sample, 0);
        EXPECT_EQ(logic.Dac(channel).volume, 0);
    }
    EXPECT_EQ(logic.Read(0xFFFD).source, Source::YmRegister);
}

/// The GS bus controller (grom_n, gram*_n, gma) for both firmware builds
TEST_F(MultiSoundLogic_Test, GsMemoryMap)
{
    for (int twoMb = 0; twoMb < 2; twoMb++)
    {
        MultiSoundScenario header;
        header.options.gsRam = twoMb ? MultiSoundGsRam::TwoMb : MultiSoundGsRam::OneMb;
        MultiSoundLogicBus bus(header);
        uint64_t hash = MultiSoundHashSeed;
        ForEachMultiSoundGsMapCycle([&](const MultiSoundCycle& cycle) { hash = HashMultiSoundRecord(hash, bus.Execute(cycle)); });
        EXPECT_EQ(hash, twoMb ? MultiSoundGsMapHash2Mb : MultiSoundGsMapHash1Mb) << (twoMb ? "2 MB" : "1 MB");
    }

    MultiSoundLogic logic = MakeLogic();
    using Chip = MultiSoundGsMapping::Chip;
    EXPECT_EQ(logic.GsMemoryMap(0x0000).chip, Chip::Rom);
    EXPECT_EQ(logic.GsMemoryMap(0x8000).chip, Chip::Rom);       // page 0
    EXPECT_EQ(logic.GsMemoryMap(0x4000).chip, Chip::Ram1);
    EXPECT_EQ(logic.GsMemoryMap(0x4000).ChipOffset(0x4000), 0xC000u);
    logic.GsPortWrite(0x00, 0x1F);
    EXPECT_EQ(logic.GsMemoryMap(0xC000).chip, Chip::Ram2);
    EXPECT_EQ(logic.GsMemoryMap(0xC000).ChipOffset(0xC000), 0x7C000u);
}

/// The scenario corpus: every .msc played into MultiSoundLogic must reproduce its RTL records line for line
TEST_F(MultiSoundLogic_Test, ScenarioCorpus)
{
    const std::filesystem::path folder = TestPathHelper::GetTestDataPath("sound/multisound/scenarios");
    ASSERT_TRUE(std::filesystem::is_directory(folder)) << folder.string();

    int scenarios = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() != ".msc")
            continue;
        scenarios++;
        const std::string name = entry.path().filename().string();
        std::filesystem::path expectedPath = entry.path();
        expectedPath.replace_extension(".expected");

        MultiSoundScenario scenario;
        std::string error;
        ASSERT_TRUE(ParseMultiSoundScenario(ReadText(entry.path().string()), scenario, error)) << name << ": " << error;

        std::vector<std::string> expected;
        std::stringstream lines(ReadText(expectedPath.string()));
        std::string line;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line[0] != '#')
                expected.push_back(line);
        }
        ASSERT_EQ(expected.size(), scenario.cycles.size()) << name << ": regenerate " << expectedPath.filename().string();

        MultiSoundLogicBus bus(scenario);
        for (size_t i = 0; i < scenario.cycles.size(); i++)
        {
            const MultiSoundCycle& cycle = scenario.cycles[i];
            const std::string actual = FormatMultiSoundCycle(cycle) + " => " + FormatMultiSoundRecord(bus.Execute(cycle));
            ASSERT_EQ(actual, expected[i]) << name << ":" << cycle.sourceLine << " (first difference)";
        }
    }
    EXPECT_GE(scenarios, 10);
}

/// The scenario parser rejects malformed lines with the line number
TEST_F(MultiSoundLogic_Test, ScenarioParserErrors)
{
    MultiSoundScenario scenario;
    std::string error;
    EXPECT_FALSE(ParseMultiSoundScenario("out FFFD\n", scenario, error));
    EXPECT_NE(error.find("line 1"), std::string::npos);
    EXPECT_FALSE(ParseMultiSoundScenario("out 0000 00\nmask pro\n", scenario, error));
    EXPECT_NE(error.find("line 2"), std::string::npos);
    EXPECT_FALSE(ParseMultiSoundScenario("par out 000F 11 gmr 6000 22 @0\n", scenario, error));
    EXPECT_TRUE(ParseMultiSoundScenario("mask classic\nram 2m\ndip ym,gs\nout FFFD F0 # comment\n", scenario, error)) << error;
    EXPECT_EQ(scenario.options.ctrlMask, MultiSoundCtrlMask::Classic);
    EXPECT_EQ(scenario.options.gsRam, MultiSoundGsRam::TwoMb);
    EXPECT_FALSE(scenario.options.saa);
    ASSERT_EQ(scenario.cycles.size(), 1u);
}

/// CL-2 corpus generator, not a check (run with --gtest_also_run_disabled_tests): plays the real TFM Music Compiler
/// 1.12 player (testdata/sound/tsfm/TSFM-EL.TAP, tune 1) for 100 frames on a Pentagon with the TSFM device and
/// writes its #FFFD / #BFFD traffic as a scenario to scratch/multisound-tfm-player.msc. The player runs from RAM
/// (#61A8), so the trace starts with an M1 there. Copy the file into testdata/sound/multisound/scenarios/ and run
/// tools/verification/multisound/regenerate.sh to freeze the RTL records.
TEST_F(MultiSoundLogic_Test, DISABLED_CaptureTsfmPlayerTrace)
{
    Emulator* emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::FM, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    TsfmPlayerHarness harness;
    std::string error;
    ASSERT_TRUE(harness.Setup(emulator, 0, &error)) << error;
    harness.RunFrames(100);

    std::string text = "# Real-program trace (CL-2): TFM Music Compiler 1.12 player, TSFM-EL.TAP tune 1 (" + harness.GetTuneName() +
                       "), 100 frames\n# Captured by MultiSoundLogic_Test.DISABLED_CaptureTsfmPlayerTrace (#FFFD / #BFFD writes only)\n"
                       "m1 61A8\n";
    char line[32];
    for (const TsfmPlayerHarness::PortWrite& write : harness.GetWrites())
    {
        std::snprintf(line, sizeof(line), "out %04X %02X\n", write.port, write.value);
        text += line;
    }
    std::ofstream out(TestPathHelper::GetTestScratchPath("multisound-tfm-player.msc"), std::ios::binary);
    out << text;

    harness.Detach();
    emulator->GetContext()->pAudioCallback.store(nullptr, std::memory_order_release);
    emulator->GetContext()->pAudioManagerObj.store(nullptr, std::memory_order_release);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
