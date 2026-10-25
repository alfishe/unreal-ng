// NedoOsAnalyzer tests: pure unit tests of the detection and audit math,
// plus one integration test that boots the real NedoOS from the host-folder
// fixture (the ZXEvoErs_Test.NedoOsBootsFromAHostFolder flow) with the
// analyzer armed, and checks the audience-question deliverable end to end:
// a synthetic leak.com that takes pages and exits must produce a FINDING.

#include "pch.h"


#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/nedoos/nedoosanalyzer.h"
#include "debugger/analyzers/osaware/osaware_event.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_atm3.h"

#include <algorithm>
#include <functional>
#include <filesystem>
#include <fstream>

namespace
{

/// A 16K page image carrying the pgsys entry stubs of a known profile.
std::vector<uint8_t> makePgsysPage(const NedoProfile& profile)
{
    std::vector<uint8_t> page(PAGE_SIZE, 0);
    auto putJp = [&](uint16_t at, uint16_t target) {
        page[at] = 0xC3;
        page[at + 1] = target & 0xFF;
        page[at + 2] = target >> 8;
    };
    putJp(0x0004, profile.sysQuit);
    putJp(0x0009, profile.callBdos);
    putJp(0x0038, profile.sysSysint);
    return page;
}

} // namespace

TEST(NedoOsAnalyzer_Unit, MatchProfileAcceptsKnownKernelAndRejectsOthers)
{
    const auto& profiles = NedoOsAnalyzer::knownProfiles();
    ASSERT_FALSE(profiles.empty());

    const auto page = makePgsysPage(profiles.front());
    EXPECT_EQ(NedoOsAnalyzer::matchProfile(page.data()), 0);

    std::vector<uint8_t> empty(PAGE_SIZE, 0);
    EXPECT_EQ(NedoOsAnalyzer::matchProfile(empty.data()), -1);
    EXPECT_EQ(NedoOsAnalyzer::matchProfile(nullptr), -1);

    std::vector<uint8_t> drift = page;
    drift[0x000A] ^= 0xFF; // callbdos target drifted -> unknown build
    EXPECT_EQ(NedoOsAnalyzer::matchProfile(drift.data()), -1);
}

TEST(NedoOsAnalyzer_Unit, CountOwnedPagesIgnoresFreeAndSystemPages)
{
    const uint8_t owners[] = {0x00, 0x05, 0x05, 0xFF, 0x05, 0x01, 0x00};
    EXPECT_EQ(NedoOsAnalyzer::countOwnedPages(owners, 7, 0x05), 3);
    EXPECT_EQ(NedoOsAnalyzer::countOwnedPages(owners, 7, 0x01), 1);
    EXPECT_EQ(NedoOsAnalyzer::countOwnedPages(owners, 7, 0x00), 0);  // free is not an owner
    EXPECT_EQ(NedoOsAnalyzer::countOwnedPages(owners, 7, 0xFF), 1);  // 0xFF (system) matches only if asked
}

TEST(NedoOsAnalyzer_Unit, OsEventFormatCarriesKindTaskAndPc)
{
    OsEvent event;
    event.kind = OsEventKind::FINDING;
    event.severity = OsSeverity::WARNING;
    event.taskId = 3;
    event.message = "NedoOS task 3 exits owning 2 page(s)";
    event.pc = 0x0103;
    const std::string text = event.format();
    EXPECT_NE(text.find("FINDING"), std::string::npos);
    EXPECT_NE(text.find("warning"), std::string::npos);
    EXPECT_NE(text.find("task=3"), std::string::npos);
    EXPECT_NE(text.find("pc=#103"), std::string::npos);
}

namespace
{

/// The ERS boot flow of ZXEvoErs_Test, with the NedoOS analyzer armed from
/// the start and a synthetic leak.com added to the boot folder.
class NedoOsAnalyzer_Boot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    /// Copy the shipped sdcard fixture, add bin/leak.com (NEWPAGE x2 then QUIT)
    /// and run it from autoexec.bat
    std::filesystem::path MakeCardWithLeakProgram()
    {
        const auto source = TestPathHelper::FindProjectRoot() / "testdata/machines/zxevo/nedoos/sdcard";
        EXPECT_TRUE(std::filesystem::is_directory(source));
        const auto scratch = std::filesystem::path(TestPathHelper::GetUniqueTestScratchPath("nedoos-analyzer"));
        std::filesystem::remove_all(scratch);
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(source, scratch, std::filesystem::copy_options::recursive);

        // org #0100: ld c,#FC (CMD_NEWPAGE); call #0005 (BDOS=5) - twice, then
        // QUIT is RST 0 (the SDK's QUIT macro), which lands on pgsys's
        // "jp sys_quit" after the user-kernel page switch.
        static const uint8_t leakCom[] = {
            0x0E, 0xFC, 0xCD, 0x05, 0x00,  // ld c,#FC ; call #0005
            0x0E, 0xFC, 0xCD, 0x05, 0x00,  // ld c,#FC ; call #0005
            0xC7,                          // rst 0 (QUIT)
        };
        std::ofstream bin(scratch / "bin" / "leak.com", std::ios::binary);
        bin.write(reinterpret_cast<const char*>(leakCom), sizeof(leakCom));
        bin.close();

        std::ofstream autoexec(scratch / "bin" / "autoexec.bat", std::ios::binary | std::ios::app);
        autoexec << "leak.com\r\n";
        autoexec.close();
        return scratch;
    }

    void Create()
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("nedoos-analyzer", "ATM3", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_EQ(std::string(_context->config.atm3_rom_path), "rom/zxevo-fe.rom");

        if (auto* decoder = static_cast<PortDecoder_ATM3*>(_context->pPortDecoder))
            decoder->GetRtc().SetFixedTime(1767268830);

        _emulator->EnableTurboMode();
    }

    bool RunToMainMenu()
    {
        Memory* memory = _context->pMemory;
        Z80* z80 = _context->pCore->GetZ80();
        // RunFrame(false): the analyzer traps fires only with breakpoints on
        // (EmulatorTestHelper::RunUntil hardcodes the skip-breakpoints mode)
        for (int i = 0; i < 300; i++)
        {
            if (z80->pc == 0x6117 && memory->IsBank0ROM() && memory->GetROMPage() == 28u)
                break;
            _emulator->RunFrame(false);
        }
        return z80->pc == 0x6117 && memory->IsBank0ROM() && memory->GetROMPage() == 28u;
    }

    void Tap(ZXKeysEnum key)
    {
        _context->pKeyboard->PressKey(key);
        _emulator->RunNFrames(4, false);
        _context->pKeyboard->ReleaseKey(key);
        _emulator->RunNFrames(4, false);
    }

    /// RunUntil with breakpoints active (the helper's own version skips them)
    void RunFramesUntil(const std::function<bool()>& condition, int maxFrames, int checkInterval)
    {
        for (int i = 0; i < maxFrames; i++)
        {
            _emulator->RunFrame(false);
            if ((i + 1) % checkInterval == 0 && condition())
                return;
        }
    }

    bool InRam(const std::string& needle)
    {
        for (uint16_t page = 0; page < 256; page++)
        {
            const uint8_t* bytes = _context->pMemory->RAMPageAddress(page);
            if (bytes
                && std::search(bytes, bytes + PAGE_SIZE, needle.begin(), needle.end()) != bytes + PAGE_SIZE)
                return true;
        }
        return false;
    }

    IOSAnalyzer* OsAnalyzer()
    {
        AnalyzerManager* manager = _context->pDebugManager->GetAnalyzerManager();
        return dynamic_cast<IOSAnalyzer*>(manager->getAnalyzer("nedoos"));
    }
};

} // namespace

/// NK-1/NK-7/NK-15 and the leak finding, on the real kernel: the analyzer
/// detects pgsys, streams BDOS calls, tracks task/page state, and flags the
/// synthetic program that exits owning pages. Real-ROM + OS boot: slow by nature.
TEST_F(NedoOsAnalyzer_Boot_Test, DetectsKernelStreamsCallsAndFlagsLeakedPages)
{
    const auto card = MakeCardWithLeakProgram();
    ASSERT_TRUE(std::filesystem::is_directory(card / "bin"));

    Create();

    AnalyzerManager* manager = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_TRUE(manager->activate("nedoos"));
    IOSAnalyzer* osAnalyzer = OsAnalyzer();
    ASSERT_NE(osAnalyzer, nullptr);

    MediaSource source;
    source.path = card.string();
    InsertOptions options;
    options.freeBytes = 16 * 1024 * 1024;
    const MediaResult inserted = _context->pMediaManager->Insert("sd.zc", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    ASSERT_TRUE(RunToMainMenu());

    Tap(ZXKEY_5); // "5. SDcard boot" -> SD_BOOT.$C -> the NedoOS kernel
    RunFramesUntil([&] { return InRam("M:/bin>"); }, 800, 20);
    ASSERT_TRUE(InRam("M:/bin>")) << "the NedoOS shell prompt";
    ASSERT_TRUE(InRam("UNREALNGSDBOOT")) << "autoexec.bat ran";

    const auto events = osAnalyzer->getOsEvents();

    const auto has = [&](OsEventKind kind) {
        return std::any_of(events.begin(), events.end(),
                           [kind](const OsEvent& e) { return e.kind == kind; });
    };
    EXPECT_TRUE(has(OsEventKind::OS_DETECTED));
    EXPECT_TRUE(has(OsEventKind::SYSCALL));
    EXPECT_TRUE(has(OsEventKind::TASK_START));

    const auto leakFinding = std::find_if(
        events.begin(), events.end(), [](const OsEvent& e) {
            return e.kind == OsEventKind::FINDING && e.severity == OsSeverity::WARNING;
        });
    ASSERT_NE(leakFinding, events.end()) << "leak.com exits owning pages";
    EXPECT_NE(leakFinding->message.find("exits owning"), std::string::npos) << leakFinding->message;
    EXPECT_NE(leakFinding->message.find("page"), std::string::npos) << leakFinding->message;

    // NEWPAGE was called at least twice by leak.com
    const size_t newpageCalls = std::count_if(
        events.begin(), events.end(), [](const OsEvent& e) {
            return e.kind == OsEventKind::SYSCALL && e.syscall == 0xFC;
        });
    EXPECT_GE(newpageCalls, 2u);

    const OsSnapshot snapshot = osAnalyzer->getOsSnapshot();
    EXPECT_TRUE(snapshot.detected);
    EXPECT_FALSE(snapshot.profile.empty());
    ASSERT_GE(snapshot.tasks.size(), 3u) << "idle -> term -> cmd must be visible";

    // Task names come from the command line in each task's own main page
    const auto byName = [&](const char* needle) {
        return std::any_of(snapshot.tasks.begin(), snapshot.tasks.end(),
                           [needle](const OsTask& t) { return t.name.find(needle) != std::string::npos; });
    };
    EXPECT_TRUE(byName("term")) << "the terminal task is named";
    EXPECT_TRUE(byName("cmd")) << "the shell task is named";

    // The terminal wired cmd's stdio through pipes (handles 0x80+)
    EXPECT_FALSE(snapshot.pipes.empty()) << "term's stdin/stdout pipes must be visible";
    for (const auto& pipe : snapshot.pipes)
    {
        EXPECT_GE(pipe.handle, 0x80);
        EXPECT_LE(pipe.handle, 0x87);
    }

    // Time quanta: the idle task always ran, the shell too
    const OsTask* idle = nullptr;
    const OsTask* cmd = nullptr;
    for (const auto& t : snapshot.tasks)
    {
        if (t.name.find("cmd") != std::string::npos)
            cmd = &t;
        if (t.parent == 0)
            idle = &t;
    }
    ASSERT_NE(cmd, nullptr);
    ASSERT_NE(idle, nullptr);
    EXPECT_GT(idle->lastTime, 0u);
    EXPECT_GT(cmd->lastTime, 0u);
    EXPECT_NE(cmd->stdinFd, 0u) << "cmd reads from a pipe";
}
