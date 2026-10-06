/// @file snapshotsave_test.cpp
/// @brief The save side of the snapshot pipeline (P6, PLAN #84, tests SP-3 / SP-6): what a machine writes is the 128K view
/// of its state, written from a captured image, and loads back as the same state; a machine or a format that cannot hold
/// the state refuses with the reason and writes nothing.
///
/// The golden table of the files themselves is snapshotsavegolden_test.cpp; the Sprinter's cell-table capture is in
/// emulator/machines/sprinter/sprinterzxsnapshot_test.cpp.

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"
#include "loaders/snapshot/snapshotcapture.h"

namespace
{
struct Machine
{
    const char* model;
    uint32_t ramKb;
    const char* tag;
    uint16_t pages;   ///< RAM pages the machine has
};

void PrintTo(const Machine& machine, std::ostream* os)
{
    *os << machine.tag;
}

Emulator* Create(const Machine& machine)
{
    SoundCardScope sound(TestSound::TurboSound);
    if (machine.ramKb == 0)
        return EmulatorTestHelper::CreateStandardEmulator(machine.model, LoggerLevel::LogError, RamPowerOn::Zero);
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM(
        "test-emulator", machine.model, machine.ramKb, LoggerLevel::LogError, nullptr,
        Config::RamPowerOnOverride(RamPowerOn::Zero));
    return emulator ? emulator.get() : nullptr;
}

/// A pattern per page, a CPU with its stack in RAM, a visible #7FFD
void Prepare(Emulator* emulator, const Machine& machine, uint8_t p7ffd)
{
    EmulatorContext* context = emulator->GetContext();
    Memory& memory = *context->pMemory;
    for (uint16_t page = 0; page < machine.pages; page++)
    {
        uint8_t* bytes = memory.RAMPageAddress(page);
        for (uint32_t i = 0; i < 16384; i++)
            bytes[i] = static_cast<uint8_t>((i * 7 + page * 37 + (i >> 8)) & 0xFF);
    }
    if (context->config.mem_model != MM_SPECTRUM48)
    {
        // The test machines reset into the 48K ROM with paging locked (+2A, +3); a program has unlocked it
        context->pPortDecoder->UnlockPaging();
        context->pPortDecoder->DecodePortOut(0x7FFD, p7ffd, 0x8000);
    }
    context->pPortDecoder->DecodePortOut(0x00FE, 0x05, 0x8000);
    Z80& z80 = *context->pCore->GetZ80();
    z80.pc = 0x8123;
    z80.sp = 0xBF00;
    z80.af = 0x1234;
    z80.bc = 0x2345;
    z80.de = 0x3456;
    z80.hl = 0x4567;
    z80.ix = 0x5678;
    z80.iy = 0x6789;
    z80.alt.af = 0x789A;
    z80.alt.bc = 0x89AB;
    z80.alt.de = 0x9ABC;
    z80.alt.hl = 0xABCD;
    z80.i = 0x3F;
    z80.im = 2;
    z80.iff1 = 1;
    z80.iff2 = 1;
}

std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool Exists(const std::string& path)
{
    return std::ifstream(path).good();
}

std::string ScratchFile(const std::string& format)
{
    return TestPathHelper::GetUniqueTestScratchPath("save-test." + format);
}

const Machine kMachines[] = {
    {"48K", 0, "48K", 3},          {"128k", 0, "128K", 8},       {"PLUS2", 0, "PLUS2", 8},
    {"PLUS2A", 0, "PLUS2A", 8},    {"PLUS3", 0, "PLUS3", 8},     {"PENTAGON", 0, "PENTAGON128", 8},
    {"SCORPION", 0, "SCORPION", 16},
};

const char* kFormats[] = {"sna", "z80", "szx"};

/// A Scorpion has 16 banks, which a .sna cannot hold
bool Expected(const Machine& machine, const std::string& format)
{
    return !(format == "sna" && machine.pages > 8);
}
}  // namespace

class SnapshotSaveRoundTrip_Test : public ::testing::TestWithParam<Machine>
{
};

// SP-3: every machine saves into every format it can, and the file loads on a fresh machine of the same kind as the same
// state: RAM, CPU, paging, border. The one thing a 48K .sna cannot keep is the two stack bytes the PC went onto
TEST_P(SnapshotSaveRoundTrip_Test, EveryFormatLoadsBackAsTheSameState)
{
    const Machine& machine = GetParam();
    for (const char* format : kFormats)
    {
        Emulator* source = Create(machine);
        ASSERT_NE(source, nullptr);
        Prepare(source, machine, 0x13);
        EmulatorContext* from = source->GetContext();
        const std::string path = ScratchFile(format);

        const bool saved = source->SaveSnapshot(path);
        ASSERT_EQ(saved, Expected(machine, format)) << machine.tag << " ." << format << ": " << source->LastSaveResult().text;
        if (!saved)
        {
            EXPECT_FALSE(Exists(path)) << "a refusal writes nothing";
            EXPECT_FALSE(source->LastSaveResult().reason.empty());
            EmulatorTestHelper::CleanupEmulator(source);
            continue;
        }

        Emulator* target = Create(machine);
        ASSERT_NE(target, nullptr);
        ASSERT_TRUE(target->LoadSnapshot(path)) << machine.tag << " ." << format;
        EmulatorContext* to = target->GetContext();
        const bool is48 = from->config.mem_model == MM_SPECTRUM48;

        
        for (uint16_t index = 0; index < machine.pages; index++)
        {
            // A 48K machine's snapshot banks are 5, 2 and 0
            const uint16_t page = is48 ? std::array<uint16_t, 3>{5, 2, 0}[index] : index;
            const uint8_t* a = from->pMemory->RAMPageAddress(page);
            const uint8_t* b = to->pMemory->RAMPageAddress(page);
            // A 48K .sna holds the PC on the stack: the two bytes at SP - 2 differ in the loaded copy
            const bool stackPage = is48 && std::string(format) == "sna" && page == 2;   // #BF00 - 2 is in bank 2
            size_t differing = 0;
            for (uint32_t i = 0; i < 16384; i++)
                differing += a[i] != b[i];
            EXPECT_LE(differing, stackPage ? 2u : 0u) << machine.tag << " ." << format << " bank " << page;
        }

        const Z80& x = *from->pCore->GetZ80();
        const Z80& y = *to->pCore->GetZ80();
        EXPECT_EQ(y.pc, x.pc) << machine.tag << " ." << format;
        EXPECT_EQ(y.sp, x.sp) << machine.tag << " ." << format;
        EXPECT_EQ(y.af, x.af);
        EXPECT_EQ(y.bc, x.bc);
        EXPECT_EQ(y.de, x.de);
        EXPECT_EQ(y.hl, x.hl);
        EXPECT_EQ(y.ix, x.ix);
        EXPECT_EQ(y.iy, x.iy);
        EXPECT_EQ(y.alt.af, x.alt.af);
        EXPECT_EQ(y.alt.bc, x.alt.bc);
        EXPECT_EQ(y.alt.de, x.alt.de);
        EXPECT_EQ(y.alt.hl, x.alt.hl);
        EXPECT_EQ(y.i, x.i);
        EXPECT_EQ(y.im, x.im);
        EXPECT_EQ(y.iff1, x.iff1);
        if (!is48)
            EXPECT_EQ(to->emulatorState.p7FFD, from->emulatorState.p7FFD) << machine.tag << " ." << format;
        EXPECT_EQ(to->pScreen->GetBorderColor() & 7, 5) << machine.tag << " ." << format;

        std::remove(path.c_str());
        EmulatorTestHelper::CleanupEmulator(source);
        EmulatorTestHelper::CleanupEmulator(target);
    }
}

INSTANTIATE_TEST_SUITE_P(Machines, SnapshotSaveRoundTrip_Test, ::testing::ValuesIn(kMachines),
                         [](const ::testing::TestParamInfo<Machine>& info) { return std::string(info.param.tag); });

class SnapshotSave_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Make(const Machine& machine, uint8_t p7ffd = 0x13)
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
        _machine = machine;
        _emulator = Create(machine);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        Prepare(_emulator, machine, p7ffd);
    }
    uint64_t RamHash()
    {
        uint64_t h = 14695981038346656037ull;
        for (uint16_t page = 0; page < _machine.pages; page++)
        {
            const uint8_t* bytes = _context->pMemory->RAMPageAddress(page);
            for (uint32_t i = 0; i < 16384; i++)
            {
                h ^= bytes[i];
                h *= 1099511628211ull;
            }
        }
        return h;
    }

    Machine _machine{};
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
};

// The old 48K .sna writer put the PC into the LIVE RAM of the running machine before it wrote the file; the new one
// writes into its own copy
TEST_F(SnapshotSave_Test, A48kSnaLeavesTheMachinesRamAlone)
{
    Make(kMachines[0]);
    const uint64_t before = RamHash();
    const std::string path = ScratchFile("sna");
    ASSERT_TRUE(_emulator->SaveSnapshot(path));
    EXPECT_EQ(RamHash(), before) << "the PC was pushed on the stack in the machine, not in the file's copy";
    EXPECT_EQ(ReadFile(path).size(), 49179u);
    std::remove(path.c_str());
}

// A 48K .sna keeps the PC on the stack: a stack in ROM (or wrapping through it) cannot take it
TEST_F(SnapshotSave_Test, A48kSnaRefusesAStackInRom)
{
    Make(kMachines[0]);
    Z80& z80 = *_context->pCore->GetZ80();
    const std::string path = ScratchFile("sna");

    for (uint16_t sp : {0x0000, 0x4002, 0xFFFE})   // these have room (SP = 0 pushes to #FFFE)
    {
        z80.sp = sp;
        EXPECT_TRUE(_emulator->SaveSnapshot(path)) << "SP " << sp << ": " << _emulator->LastSaveResult().text;
        std::remove(path.c_str());
    }
    for (uint16_t sp : {0x0001, 0x0002, 0x3FFF, 0x4001})
    {
        z80.sp = sp;
        EXPECT_FALSE(_emulator->SaveSnapshot(path)) << "SP " << sp;
        EXPECT_FALSE(Exists(path)) << "a refusal writes nothing";
        const snapshot::SaveResult& result = _emulator->LastSaveResult();
        EXPECT_NE(result.reason.find("ROM"), std::string::npos) << result.reason;
        EXPECT_NE(result.reason.find(".z80"), std::string::npos) << "it says what would work";
        EXPECT_EQ(result.needs, "format:z80");
        // ...and .z80 / .szx take it
        EXPECT_TRUE(_emulator->SaveSnapshot(ScratchFile("z80")));
    }
}

// The 48K layout of a .sna / .z80 is for a 48K program: a 128K machine whose paging is locked with bank 0 on top and the
// normal screen; any other state keeps all 8 banks
TEST_F(SnapshotSave_Test, A128kLockedInA48kProgramSavesTheThreePages)
{
    Make(kMachines[1], 0x30);
    const std::string sna = ScratchFile("sna");
    const std::string z80 = ScratchFile("z80");
    ASSERT_TRUE(_emulator->SaveSnapshot(sna));
    ASSERT_TRUE(_emulator->SaveSnapshot(z80));
    EXPECT_EQ(ReadFile(sna).size(), 49179u) << "a 48K .sna";
    const std::vector<uint8_t> z = ReadFile(z80);
    EXPECT_EQ(z[34], 0) << "hardware mode 0 = 48K";
    std::remove(sna.c_str());
    std::remove(z80.c_str());

    Make(kMachines[1], 0x34);   // locked, but bank 4 on top: the program can still reach nothing else, yet bank 4 is its RAM
    ASSERT_TRUE(_emulator->SaveSnapshot(sna));
    EXPECT_EQ(ReadFile(sna).size(), 131103u) << "bank 4 is on top: the full 128K layout";
    std::remove(sna.c_str());
}

// Pentagon 512 / 1024: the .sna and .z80 formats cannot hold them; .szx can
TEST_F(SnapshotSave_Test, ABigPentagonSavesAsSzxOnly)
{
    for (const Machine& machine : {Machine{"PENTAGON", 512, "PENTAGON512", 32}, Machine{"PENTAGON", 1024, "PENTAGON1024", 64}})
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
        Make(machine);
        const snapshot::SaveFormats formats = _emulator->SnapshotSaveFormats();
        EXPECT_TRUE(formats.viewAvailable);
        EXPECT_FALSE(formats.For(snapshot::SaveFormat::Sna).available) << machine.tag;
        EXPECT_FALSE(formats.For(snapshot::SaveFormat::Z80).available) << machine.tag;
        EXPECT_TRUE(formats.For(snapshot::SaveFormat::Szx).available) << machine.tag;
        EXPECT_EQ(formats.For(snapshot::SaveFormat::Sna).needs, "format:szx");
        EXPECT_NE(formats.For(snapshot::SaveFormat::Z80).reason.find(".szx"), std::string::npos);

        const std::string sna = ScratchFile("sna");
        EXPECT_FALSE(_emulator->SaveSnapshot(sna));
        EXPECT_FALSE(Exists(sna));
        EXPECT_EQ(_emulator->LastSaveResult().needs, "format:szx");
        EXPECT_NE(_emulator->LastSaveResult().text.find("szx"), std::string::npos);

        const std::string szx = ScratchFile("szx");
        ASSERT_TRUE(_emulator->SaveSnapshot(szx)) << _emulator->LastSaveResult().text;
        Emulator* target = Create(machine);
        ASSERT_NE(target, nullptr);
        EXPECT_TRUE(target->LoadSnapshot(szx));
        for (uint16_t page = 0; page < machine.pages; page++)
            EXPECT_EQ(0, std::memcmp(_context->pMemory->RAMPageAddress(page), target->GetContext()->pMemory->RAMPageAddress(page), 16384))
                << machine.tag << " bank " << page;
        EmulatorTestHelper::CleanupEmulator(target);
        std::remove(szx.c_str());
    }
}

// An unknown extension is refused with the formats it knows; a refusal leaves the machine as it was
TEST_F(SnapshotSave_Test, AnUnknownExtensionIsRefusedWithTheKnownOnes)
{
    Make(kMachines[1]);
    const uint64_t before = RamHash();
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("save-test.sp");
    EXPECT_FALSE(_emulator->SaveSnapshot(path));
    EXPECT_FALSE(Exists(path));
    EXPECT_NE(_emulator->LastSaveResult().reason.find(".szx"), std::string::npos) << _emulator->LastSaveResult().reason;
    EXPECT_EQ(RamHash(), before);
}

// The save query follows the machine: the report names what the file will say it was made on
TEST_F(SnapshotSave_Test, TheQueryNamesTheMachineAndTheFormats)
{
    Make(kMachines[5]);   // Pentagon 128
    const snapshot::SaveFormats formats = _emulator->SnapshotSaveFormats();
    EXPECT_TRUE(formats.viewAvailable);
    EXPECT_EQ(formats.machine, "Pentagon 128");
    for (const snapshot::FormatStatus& status : formats.formats)
        EXPECT_TRUE(status.available) << snapshot::ToText(status.format) << ": " << status.reason;
    const StateNode node = formats.ToStateNode();
    ASSERT_NE(node.find("formats"), nullptr);
    EXPECT_EQ(node.find("machine")->s, "Pentagon 128");
}

// A machine with its own memory manager gives a snapshot view only while it is laid out as a Spectrum 128K: a freshly
// reset ATM (memory manager off, every window ROM) has none
TEST_F(SnapshotSave_Test, AnAtmInItsOwnModeHasNoView)
{
    Machine atm{"ATM710", 0, "ATM710", 8};
    _machine = atm;
    _emulator = Create(atm);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    const snapshot::SaveFormats formats = _emulator->SnapshotSaveFormats();
    EXPECT_FALSE(formats.viewAvailable);
    for (const snapshot::FormatStatus& status : formats.formats)
    {
        EXPECT_FALSE(status.available);
        EXPECT_EQ(status.needs, "mode:128k");
        EXPECT_NE(status.reason.find("128K"), std::string::npos) << status.reason;
    }
    const std::string path = ScratchFile("szx");
    EXPECT_FALSE(_emulator->SaveSnapshot(path));
    EXPECT_FALSE(Exists(path));
    EXPECT_EQ(_emulator->LastSaveResult().needs, "mode:128k");
}

// ...and after a Spectrum 128K program is loaded into it (the load puts the machine into that layout) it saves, and the
// file restores on a plain 128K machine, which is the point of the view
TEST_F(SnapshotSave_Test, AnAtmRunningA128kProgramSavesAFileAny128kTakes)
{
    Machine atm{"ATM710", 0, "ATM710", 8};
    _machine = atm;
    _emulator = Create(atm);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna")));

    const snapshot::SaveFormats formats = _emulator->SnapshotSaveFormats();
    ASSERT_TRUE(formats.viewAvailable) << formats.view;
    EXPECT_EQ(formats.machine, "ZX Spectrum 128K");

    const std::string path = ScratchFile("z80");
    ASSERT_TRUE(_emulator->SaveSnapshot(path)) << _emulator->LastSaveResult().text;
    Emulator* plain = Create(kMachines[1]);
    ASSERT_NE(plain, nullptr);
    ASSERT_TRUE(plain->LoadSnapshot(path));
    for (uint16_t page = 0; page < 8; page++)
        EXPECT_EQ(0, std::memcmp(_context->pMemory->RAMPageAddress(page), plain->GetContext()->pMemory->RAMPageAddress(page), 16384))
            << "bank " << page;
    EXPECT_EQ(plain->GetContext()->emulatorState.p7FFD, _context->emulatorState.p7FFD);
    EmulatorTestHelper::CleanupEmulator(plain);
    std::remove(path.c_str());
}

// A save waits until the emulation thread has parked: on a RUNNING emulator the file is of one frame boundary, and the
// emulator runs again afterwards
TEST_F(SnapshotSave_Test, ARunningEmulatorIsStoppedForTheSaveAndResumedAfterIt)
{
    Make(kMachines[1]);
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsRunning(); }, std::chrono::milliseconds(2000)));

    const std::string path = ScratchFile("szx");
    ASSERT_TRUE(_emulator->SaveSnapshot(path)) << _emulator->LastSaveResult().text;
    EXPECT_FALSE(_emulator->IsPaused()) << "it was running before the save: it runs again";
    EXPECT_TRUE(_emulator->LastSaveResult().ok);
    EXPECT_GT(ReadFile(path).size(), 1000u);
    std::remove(path.c_str());
    _emulator->Stop();
}
