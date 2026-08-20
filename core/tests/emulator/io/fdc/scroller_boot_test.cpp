#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/test_path_helper.h"
#include "common/stringhelper.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "loaders/disk/loader_trd.h"

/// Repro test for the "scroller by Demarche" Covox demo (TRD boot + MegaLZ decrunch chain).
///
/// Boot flow under test (from the demo sources):
///   - TR-DOS BASIC loader: RANDOMIZE USR 15619 : REM : LOAD "SCROLLnn" CODE (8 parts)
///   - RANDOMIZE USR 25094 ($6206): depack dispatcher, OUT (#7FFD),page + MegaLZ depacker at $6244
///   - RANDOMIZE USR 25088 ($6200): LD SP,$6200 ; JP $9B6B -> demo starts
///
/// The demo reportedly crashes somewhere in this chain instead of decrunching into RAM pages.

class Scroller_Boot_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
        if (_emulator)
        {
            _context = _emulator->GetContext();
        }
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
        MessageCenter::DisposeDefaultMessageCenter();
    }
};

static std::string FirstScreenLines(const std::string& screen, size_t lines)
{
    std::string result;
    size_t count = 0;
    size_t pos = 0;
    while (count < lines && pos < screen.size())
    {
        size_t end = screen.find('\n', pos);
        if (end == std::string::npos)
            end = screen.size();
        std::string line = screen.substr(pos, end - pos);
        if (!line.empty())
            count++;
        result += line + "\n";
        pos = end + 1;
    }
    return result;
}

TEST_F(Scroller_Boot_Test, BootScrollerDemoTRD)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    std::cout << "\n========================================\n";
    std::cout << "[SCROLLER] Boot TRD and trace decrunch chain\n";
    std::cout << "========================================\n";

    Memory* memory = _context->pMemory;
    std::string emulatorId = _emulator->GetId();
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);

    // STEP 1: ROM init
    for (int i = 0; i < 100; i++)
    {
        mainLoop->RunFrame();
    }
    std::string screen = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 1] Screen after ROM init:\n" << FirstScreenLines(screen, 6) << "\n";
    ASSERT_TRUE(screen.find("128") != std::string::npos || screen.find("Tape") != std::string::npos ||
                screen.find("BASIC") != std::string::npos)
        << "128K menu should be visible. Got:\n"
        << screen;

    // STEP 2: Insert the demo disk
    std::string trdPath = TestPathHelper::GetTestDataPath("sound/covox/scroller_by_demarche.trd");
    LoaderTRD trdLoader(_context, trdPath);
    ASSERT_TRUE(trdLoader.loadImage()) << "TRD not loaded: " << trdPath;

    WD1793* wd1793 = _context->pBetaDisk;
    ASSERT_NE(wd1793, nullptr);
    FDD* fdd = wd1793->getDrive();
    ASSERT_NE(fdd, nullptr);
    fdd->insertDisk(trdLoader.getImage());
    std::cout << "[STEP 2] TRD inserted: " << trdPath << "\n";

    // STEP 3: Navigate to TR-DOS
    BasicEncoder::navigateToTRDOS(memory);
    for (int i = 0; i < 200; i++)
    {
        mainLoop->RunFrame();
    }
    screen = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 3] Screen after navigate to TR-DOS:\n" << FirstScreenLines(screen, 6) << "\n";
    ASSERT_TRUE(screen.find("A>") != std::string::npos) << "TR-DOS prompt expected. Got:\n" << screen;

    // STEP 4: Breakpoint instrumentation
    Z80* cpu = _context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    BreakpointManager* bpMgr = _context->pDebugManager->GetBreakpointsManager();
    ASSERT_NE(bpMgr, nullptr);
    EmulatorTestHelper::EnableDebugFeatures(_emulator);

    std::mutex logMutex;
    std::vector<std::string> events;
    std::atomic<int> dosEntryHits{0};
    std::atomic<int> depackHits{0};
    std::atomic<int> startHits{0};
    std::atomic<int> mainHits{0};
    std::atomic<int> resetHits{0};

    auto logEvent = [&](const std::string& s)
    {
        std::lock_guard<std::mutex> lock(logMutex);
        events.push_back(s);
        std::cout << "[BP] " << s << std::endl;
    };

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    // Raw bus trace: ordered interleaved trace of every OUT in the #7FFD window + bank3 result
    std::atomic<int> seqCounter{0};
    std::atomic<int> bus7FFDWrites{0};
    std::map<uint16_t, int> hookCallers;
    cpu->busTraceHook = [&, cpu](char type, uint16_t port, uint8_t value)
    {
        if (type == 'O' && (port & 0x8006) == 0x0004)
        {
            int seq = ++seqCounter;
            int bank3 = memory->GetRAMPageForBank3();
            bus7FFDWrites++;
            // Identify caller of the #5B00 hook: after PUSH AF+PUSH BC the return
            // address sits at SP+4 (hook is CALLed; OUT happens at $5B0E inside)
            uint16_t caller = 0;
            if (cpu->pc == 0x5B10 && cpu->sp <= 0xFFFB)
            {
                caller = memory->DirectReadFromZ80Memory(cpu->sp + 4) |
                         (memory->DirectReadFromZ80Memory(cpu->sp + 5) << 8);
                hookCallers[caller]++;
            }
            std::lock_guard<std::mutex> lock(logMutex);
            std::cout << "[SEQ " << seq << "][BUS] OUT #7FFD value=" << std::hex << (int)value
                      << " pc=" << cpu->pc << " caller=" << caller
                      << " -> p7FFD=" << (int)_context->emulatorState.p7FFD
                      << " bank3=page" << std::dec << bank3 << std::endl;
        }
    };

    auto handler = [&](int id, Message* msg)
    {
        uint16_t pc = cpu->pc;
        EmulatorState& state = _context->emulatorState;

        if (pc == 0x0000)
        {
            resetHits++;
            logEvent("RESET detected (PC=0x0000)");
        }
        else if (pc == 0x3D03)
        {
            dosEntryHits++;
            int seq = ++seqCounter;
            int bank3 = memory->GetRAMPageForBank3();
            logEvent(StringHelper::Format("[SEQ %d][BP] DOS entry USR 15619 (hit #%d), p7FFD=%02X bank3=page%d",
                                           seq, dosEntryHits.load(), state.p7FFD, bank3));
        }
        else if (pc == 0x6206)
        {
            depackHits++;
            int seq = ++seqCounter;
            int bank3 = memory->GetRAMPageForBank3();
            uint16_t entryPtr = memory->DirectReadFromZ80Memory(0x620A) |
                                (memory->DirectReadFromZ80Memory(0x620B) << 8);
            if (entryPtr >= 0x6226 && entryPtr <= 0x6240)
            {
                uint8_t page = memory->DirectReadFromZ80Memory(entryPtr);
                uint16_t dest = memory->DirectReadFromZ80Memory(entryPtr + 1) |
                                (memory->DirectReadFromZ80Memory(entryPtr + 2) << 8);
                uint16_t src = memory->DirectReadFromZ80Memory(entryPtr + 3) |
                                (memory->DirectReadFromZ80Memory(entryPtr + 4) << 8);
                uint32_t srcBytes = 0;
                for (int b = 0; b < 4; b++)
                {
                    srcBytes = (srcBytes << 8) | memory->DirectReadFromZ80Memory(src + b);
                }
                logEvent(StringHelper::Format("[SEQ %d][BP] DEPACK call #%d: entry=%04X page=%02X dest=%04X src=%04X src[0..3]=%04X p7FFD=%02X bank3=page%d",
                                               seq, depackHits.load(), entryPtr, page, dest, src, srcBytes, state.p7FFD, bank3));
            }
            else
            {
                logEvent(StringHelper::Format("DEPACK call #%d: BAD table ptr %04X", depackHits.load(), entryPtr));
            }
        }
        else if (pc == 0x6223)
        {
            // POP IY right after CALL DEPACK: check what the depacker just produced
            int seq = ++seqCounter;
            std::string pages;
            for (uint16_t page : {0, 1, 3, 4})
            {
                uint8_t* addr = memory->RAMPageAddress(page);
                pages += StringHelper::Format(" p%d=%02X%02X%02X%02X", page, addr[0], addr[1], addr[2], addr[3]);
            }
            // SCROLL17 depacks to $DB00 in page 7 (page offset 0x1B00)
            uint8_t* p7 = memory->RAMPageAddress(7) + 0x1B00;
            pages += StringHelper::Format(" p7@DB00=%02X%02X%02X%02X", p7[0], p7[1], p7[2], p7[3]);
            uint32_t a8000 = 0;
            for (int b = 0; b < 4; b++)
            {
                a8000 = (a8000 << 8) | memory->DirectReadFromZ80Memory(0x8000 + b);
            }
            pages += StringHelper::Format(" @8000=%04X", a8000);
            logEvent(StringHelper::Format("[SEQ %d][BP] POST-DEPACK (after call #%d):%s",
                                           seq, depackHits.load(), pages.c_str()));
        }
        else if (pc == 0x6200)
        {
            startHits++;
            logEvent("DEMO START (USR 25088, JP $9B6B expected)");
        }
        else if (pc == 0x9B6B)
        {
            mainHits++;
            logEvent("DEMO MAIN reached at $9B6B");
        }

        _emulator->Resume();
    };
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);

    bpMgr->AddExecutionBreakpoint(0x3D03, "scroller");
    bpMgr->AddExecutionBreakpoint(0x6206, "scroller");
    bpMgr->AddExecutionBreakpoint(0x6223, "scroller");
    bpMgr->AddExecutionBreakpoint(0x6200, "scroller");
    bpMgr->AddExecutionBreakpoint(0x9B6B, "scroller");
    bpMgr->AddExecutionBreakpoint(0x0000, "scroller");
    std::cout << "[STEP 4] Breakpoints set ($0000,$3D03,$6200,$6206,$6223,$9B6B)\n";

    // STEP 5a: CAT first - check whether the catalog is visible at all
    auto catResult = BasicEncoder::injectToTRDOS(memory, "CAT");
    EXPECT_TRUE(catResult.success) << catResult.message;
    BasicEncoder::injectEnter(memory);
    for (int i = 0; i < 100; i++)
    {
        mainLoop->RunFrame();
    }
    screen = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 5a] Screen after CAT:\n" << screen << "\n";

    // STEP 5b: RUN the BASIC loader from TR-DOS (uppercase - TR-DOS command line is uppercase-oriented)
    auto result = BasicEncoder::injectToTRDOS(memory, "RUN \"SCROLLER\"");
    EXPECT_TRUE(result.success) << "Failed to inject RUN command: " << result.message;
    BasicEncoder::injectEnter(memory);
    std::cout << "[STEP 5b] RUN \"SCROLLER\" injected\n";

    // STEP 6: Run async and monitor
    _emulator->StartAsync();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    int lastDepack = -1;
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!_emulator->IsRunning())
        {
            std::cout << "[STEP 6] Emulator stopped unexpectedly\n";
            break;
        }

        if (resetHits > 0)
        {
            std::cout << "[STEP 6] Reset detected - demo crashed into reset\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            break;
        }

        if (mainHits > 0)
        {
            // Let the demo run for a while after start
            std::this_thread::sleep_for(std::chrono::seconds(8));
            break;
        }

        int dep = depackHits.load();
        if (dep != lastDepack)
        {
            lastDepack = dep;
            screen = ScreenOCR::ocrScreen(emulatorId);
            std::cout << "[STEP 6] depack hits=" << dep << " screen: "
                      << FirstScreenLines(screen, 2) << std::flush;
        }
    }

    screen = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "\n[STEP 6] Final screen:\n" << screen << "\n";
    std::cout << "[STEP 6] Hits: dosEntry=" << dosEntryHits << " depack=" << depackHits
              << " start=" << startHits << " main=" << mainHits << " reset=" << resetHits << "\n";

    _emulator->Stop();

    // STEP 7a: Probe the port decoder directly (emulator stopped): does OUT #7FFD work at all?
    {
        EmulatorState& st = _context->emulatorState;
        std::cout << "[PROBE] before: p7FFD=" << std::hex << (int)st.p7FFD << std::dec << "\n";
        _context->pPortDecoder->DecodePortOut(0x7FFD, 0x14, 0x0000);
        std::cout << "[PROBE] after OUT #7FFD,0x14: p7FFD=" << std::hex << (int)st.p7FFD << std::dec;
        uint8_t bank3viaCPU = memory->DirectReadFromZ80Memory(0xC000);
        uint8_t* page4 = memory->RAMPageAddress(4);
        std::cout << " C000=" << std::hex << (int)bank3viaCPU
                  << " page4[0]=" << (int)page4[0] << std::dec << "\n";
    }

    // STEP 7: Memory diagnostics
    uint8_t loaderBytes[0x44];
    for (int i = 0; i < 0x44; i++)
    {
        loaderBytes[i] = memory->DirectReadFromZ80Memory(0x6200 + i);
    }
    std::cout << "Loader: ";
    for (int i = 0; i < 0x44; i++)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)loaderBytes[i] << " ";
        if ((i & 15) == 15) std::cout << "\n       ";
    }
    std::cout << std::dec << "\n";

    std::cout << "PC=" << std::hex << cpu->pc << " SP=" << cpu->sp << std::dec
              << " I=" << std::hex << (int)cpu->i << std::dec
              << " IM=" << (int)cpu->im << " IFF1=" << (int)cpu->iff1 << "\n";

    // Dump the mysterious #5B10 routine (TR-DOS 5.04T RAM hook area in printer buffer)
    std::cout << "RAM $5AF0..$5B3F: ";
    for (uint32_t a = 0x5AF0; a < 0x5B40; a++)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory->DirectReadFromZ80Memory(a) << " ";
    }
    std::cout << std::dec << "\n";

    // Dump first bytes of each decrunch-target RAM page
    for (uint16_t page : {0, 1, 3, 4, 7})
    {
        uint8_t* addr = memory->RAMPageAddress(page);
        std::cout << "RAM page " << page << " @C000: ";
        for (int i = 0; i < 12; i++)
        {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)addr[i] << " ";
        }
        std::cout << std::dec << "\n";
    }

    // Demo main code should be depacked at $8000.. (fixed bank 2)
    std::cout << "Z80 $9B6B (demo main): ";
    for (int i = 0; i < 12; i++)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory->DirectReadFromZ80Memory(0x9B6B + i) << " ";
    }
    std::cout << std::dec << "\n";

    // Summary expectations
    std::cout << "[SUMMARY] bus7FFDWrites=" << bus7FFDWrites.load() << "\n";
    std::cout << "[SUMMARY] #5B00 hook callers (return address -> count):\n";
    for (auto& kv : hookCallers)
    {
        std::cout << "  caller " << std::hex << kv.first << std::dec << " : " << kv.second << "\n";
    }
    EXPECT_GE(depackHits.load(), 1) << "Depack dispatcher $6206 was never reached - BASIC loading failed";
    EXPECT_EQ(resetHits.load(), 0) << "Emulator was reset during demo boot - crash confirmed";
    EXPECT_GE(mainHits.load(), 1) << "Demo main code at $9B6B was never reached";
}
