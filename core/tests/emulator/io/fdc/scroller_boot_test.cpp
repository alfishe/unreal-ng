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
#include "_helpers/testpathhelper.h"
#include "common/stringhelper.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
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
/// Outcome (diagnosis, authentic hardware behavior - deliberately NOT compensated):
///   - 48K BASIC path: demo boots and runs (the editor SWAP hooks were never installed).
///   - 128K-menu path: demo dies - its BASIC line 80 uses OUT VAL"32765",VAL"20" to select
///     page 4 for SCROLL12, but in the editor environment the $5B00 SWAP hook re-derives
///     #7FFD from the BANK_M shadow ($5B5C) at every statement boundary. Neither stock
///     ROM half updates BANK_M on an OUT command, so the stale shadow reverts the page
///     selection one statement later: SCROLL12 loads into page 0, the final depack reads
///     an empty page 4, page 2 stays zeroes and JP $8000/$9B6B executes a NOP slide.
///     The 128K-safe idiom would have been POKE VAL"23388" (set BANK_M, let the editor
///     do the OUT). See docs/disasm/demo/scroller/TRIAGE.md.
///   - Variant of the same cause, later split offset (user report #2): the SWAP-hook
///     flip can land mid-LOAD inside one of TR-DOS 5.03's per-sector EI windows
///     ($3F16 DI .. $3F32 EI in trdos503.rom), splitting the raw stream at sector k
///     between page 4 and page 0. The depacker then decodes only the valid prefix:
///     low k leaves page 2 dead (no menu, NOP slide -> wrap -> reset); k >= ~22
///     keeps INIT1/menu/STARTDEMO valid but corrupts the IM2 top ($BF02/$BFBF) -
///     the covox menu shows, SPACE starts the demo, the init chain climbs into the
///     dead top and slides past $FFFF into ROM reset. See TRIAGE.md section 9.

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

    /// Full boot + run scenario.
    /// via128KMenu=true reproduces the user-reported path:
    ///   RESET=128 -> 128K service menu -> TR-DOS (menu index 4) -> RUN "SCROLLER"
    /// via128KMenu=false uses plain 48K BASIC + RANDOMIZE USR 15616.
    void BootAndRunScroller(bool via128KMenu);

    /// Realtime GUI-flow repro: same menu path, but none of the harness
    /// perturbations (no breakpoints, no CAT). Records which side of the
    /// page-selection window the untouched run lands on: covox menu present
    /// (user's GUI symptom) or page 2 zeroed (instrumented-harness symptom).
    void RealtimeGuiFlow(const char* label);
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

void Scroller_Boot_Test::BootAndRunScroller(bool via128KMenu)
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
    if (via128KMenu)
    {
        ASSERT_TRUE(screen.find("BASIC") != std::string::npos)
            << "128K service menu expected after RESET=128 boot. Got:\n"
            << screen;
    }
    else
    {
        ASSERT_TRUE(screen.find("1982") != std::string::npos || screen.find("Sinclair") != std::string::npos)
            << "48K BASIC should be visible after RESET=BASIC boot. Got:\n"
            << screen;
    }

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

    // STEP 3: Enter TR-DOS
    // Two paths:
    //  - 48K BASIC: RANDOMIZE USR 15616 -> $3D00 entry trap (verified working)
    //  - 128K service menu "TR-DOS" item: enters through the 128-editor
    //    trampoline environment, where the $5B00 RAM hook recomputes #7FFD
    //    from the stale $5B5C shadow after BASIC OUT commands (both genuine
    //    ROM halves lack the shadow sync) - user-reported crash path
    if (via128KMenu)
    {
        BasicEncoder::navigateToTRDOS(memory);
    }
    else
    {
        auto trdosEntry = BasicEncoder::runCommand(_emulator, "RANDOMIZE USR 15616");
        EXPECT_TRUE(trdosEntry.success) << trdosEntry.message;
    }
    for (int i = 0; i < 200; i++)
    {
        mainLoop->RunFrame();
    }
    screen = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 3] Screen after TR-DOS entry"
              << (via128KMenu ? " (128K menu path)" : " (48K USR 15616)") << ":\n"
              << FirstScreenLines(screen, 6) << "\n";
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
    std::atomic<int> startDemoHits{0};
    std::atomic<int> preJumpHits{0};
    std::atomic<int> demoEntryHits{0};
    std::atomic<int> im2SetupHits{0};
    std::atomic<int> im2HandlerHits{0};

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
    std::vector<std::string> lastOuts;   // ring: last 8 OUT #7FFD events
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
            MemoryPageDescriptor bank0 = memory->MapZ80AddressToPhysicalPage(0x0000);
            const char* romName = "?";
            if (bank0.mode == MemoryBankModeEnum::BANK_ROM)
                romName = bank0.page == 0 ? "sys" : bank0.page == 1 ? "dos" : bank0.page == 2 ? "128" : "48k";
            else if (bank0.mode == MemoryBankModeEnum::BANK_RAM)
                romName = "ram";
            lastOuts.push_back(StringHelper::Format("value=%02X pc=%04X p7FFD=%02X bank3=page%d trdos=%d",
                                                    value, cpu->pc, _context->emulatorState.p7FFD, bank3,
                                                    (_context->emulatorState.flags & CF_TRDOS) ? 1 : 0));
            if (lastOuts.size() > 8)
                lastOuts.erase(lastOuts.begin());
            // During the demo the IM2 handler switches pages on every interrupt:
            // cap the per-event prints, the ring buffer keeps the tail
            if (seq <= 300 || seq % 300 == 0)
                std::cout << "[SEQ " << seq << "][BUS] OUT #7FFD value=" << std::hex << (int)value
                          << " pc=" << cpu->pc << " caller=" << caller
                          << " -> p7FFD=" << (int)_context->emulatorState.p7FFD
                          << " bank0=" << romName
                          << " trdos=" << ((_context->emulatorState.flags & CF_TRDOS) ? 1 : 0)
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
        else if (pc == 0x9CD6)
        {
            startDemoHits++;
            logEvent("SPACE accepted -> STARTDEMO ($9CD6)");
        }
        else if (pc == 0x9D48)
        {
            preJumpHits++;
            logEvent("fade+seek done -> JP $8000 imminent ($9D48)");
        }
        else if (pc == 0x8000)
        {
            demoEntryHits++;
            logEvent("DEMO ENTRY $8000 (CALL INIT1)");
        }
        else if (pc == 0xBF02)
        {
            im2SetupHits++;
            logEvent("IM2INI reached ($BF02)");
        }
        else if (pc == 0xBFBF)
        {
            im2HandlerHits++;   // hot path: fires on every demo interrupt, no log
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
    bpMgr->AddExecutionBreakpoint(0x9CD6, "scroller");
    bpMgr->AddExecutionBreakpoint(0x9D48, "scroller");
    bpMgr->AddExecutionBreakpoint(0x8000, "scroller");
    bpMgr->AddExecutionBreakpoint(0xBF02, "scroller");
    bpMgr->AddExecutionBreakpoint(0xBFBF, "scroller");
    std::cout << "[STEP 4] Breakpoints set ($0000,$3D03,$6200,$6206,$6223,$9B6B,$9CD6,$9D48,$8000,$BF02,$BFBF)\n";

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

    MessageCenter& keyMc = MessageCenter::DefaultMessageCenter();

    // Phase A: wait until the demo menu is up ($9B6B reached)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    int lastDepack = -1;
    while (std::chrono::steady_clock::now() < deadline && mainHits == 0 && resetHits == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        int dep = depackHits.load();
        if (dep != lastDepack)
        {
            lastDepack = dep;
            screen = ScreenOCR::ocrScreen(emulatorId);
            std::cout << "[STEP 6] depack hits=" << dep << " screen: "
                      << FirstScreenLines(screen, 2) << std::flush;
        }
    }

    // Phase B: menu is up - press SPACE to start the demo (KEYLP polls IN #7F bit 0)
    std::cout << "[STEP 6] menu reached (main=" << mainHits << "), pressing SPACE\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    keyMc.Post(MC_KEY_PRESSED, new KeyboardEvent(ZXKEY_SPACE, KEY_PRESSED, _emulator->GetUUID()));
    std::this_thread::sleep_for(std::chrono::seconds(2));
    keyMc.Post(MC_KEY_RELEASED, new KeyboardEvent(ZXKEY_SPACE, KEY_RELEASED, _emulator->GetUUID()));

    // Phase C: watch the demo start (STARTDEMO $9CD6 -> JP $8000 at $9D48 -> INIT1 -> IM2 $BF02)
    auto phaseC = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    int stuckRomSamples = 0;
    while (std::chrono::steady_clock::now() < phaseC)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (resetHits > 0)
        {
            std::cout << "[STEP 6C] Reset detected - demo crashed into reset\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            break;
        }

        std::cout << "[STEP 6C] start=" << startDemoHits << " preJump=" << preJumpHits
                  << " entry=" << demoEntryHits << " im2ini=" << im2SetupHits
                  << " im2int=" << im2HandlerHits
                  << " PC=" << std::hex << cpu->pc << std::dec
                  << " I=" << std::hex << (int)cpu->i << std::dec
                  << " IM=" << (int)cpu->im << " IFF1=" << (int)cpu->iff1
                  << " p7FFD=" << std::hex << (int)_context->emulatorState.p7FFD << std::dec << "\n" << std::flush;

        // Crash signature: demo started but PC sits in ROM with IM1 (interrupt handler gone)
        if (startDemoHits > 0 && cpu->pc < 0x4000 && cpu->im != 2)
        {
            if (++stuckRomSamples >= 20)
            {
                std::cout << "[STEP 6C] CRASH: PC stuck in ROM with IM" << (int)cpu->im << "\n";
                break;
            }
        }
        else
        {
            stuckRomSamples = 0;
        }

        // Healthy demo: IM2 handler firing - let it run a bit more, then stop
        if (im2HandlerHits > 100)
        {
            std::cout << "[STEP 6C] demo alive (im2 interrupts=" << im2HandlerHits << ")\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            break;
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

    // Ground-truth comparison (offline depack of the same TRD via build/trd_mlz.py)
    {
        auto dump = [&](uint16_t addr, int n, const char* what)
        {
            std::cout << what << " @" << std::hex << addr << ": ";
            for (int i = 0; i < n; i++)
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << (int)memory->DirectReadFromZ80Memory(addr + i) << " ";
            std::cout << std::dec << "\n";
        };
        dump(0x8000, 6, "demo entry  (expect CD 93 85 FB 76 3E)");
        dump(0x9CD6, 6, "STARTDEMO   (expect FB 76 FB 76 3E 08)");
        dump(0x9D48, 3, "JP $8000    (expect C3 00 80)");
        dump(0xBF02, 6, "IM2INI      (expect F3 3E BE ED 47 ED)");

        int bfFill = 0, nonZero = 0, last = -1;
        for (int i = 0; i < 0x4000; i++)
        {
            uint8_t v = memory->DirectReadFromZ80Memory(0x8000 + i);
            if (v == 0xBF && i < 0x3F00)
                bfFill++;
            if (v != 0)
            {
                nonZero++;
                last = i;
            }
        }
        std::cout << "bank2 $8000-$BFFF: nonzero=" << nonZero << "/16384 last_nonzero=$"
                  << std::hex << (0x8000 + last) << std::dec << " IM2table BF-fill=" << bfFill << "\n";

        std::cout << "last 8 OUT #7FFD events:\n";
        for (auto& s : lastOuts)
            std::cout << "  " << s << "\n";
    }

    // Summary expectations
    std::cout << "[SUMMARY] bus7FFDWrites=" << bus7FFDWrites.load() << "\n";
    std::cout << "[SUMMARY] #5B00 hook callers (return address -> count):\n";
    for (auto& kv : hookCallers)
    {
        std::cout << "  caller " << std::hex << kv.first << std::dec << " : " << kv.second << "\n";
    }
    if (via128KMenu)
    {
        // Authentic stock-ROM outcome: the editor SWAP hook reverts the OUT 32765,20
        // page selection (stale BANK_M, neither ROM half syncs it), so the demo dies.
        // These assertions pin the diagnosis - this is what a real stock-ROM machine
        // does on this path, preserved by design (no emulator-side compensation).
        EXPECT_GE(depackHits.load(), 6)
            << "All 6 depack calls must still run - the loader chain itself is fine";
        // True liveness markers: only the demo's IM2INI ($BF02) can set IM 2 + I=$BE,
        // and only a real final depack can fill page 2. A pc hit on $BFBF is NOT a
        // marker - the post-crash NOP slide loops through it (141 crossings observed).
        EXPECT_FALSE(cpu->im == 2 && cpu->i == 0xBE)
            << "Demo must NOT come alive via the 128K menu path (authentic failure)";
        int bank2NonZero = 0;
        for (int i = 0; i < 0x4000; i++)
        {
            if (memory->DirectReadFromZ80Memory(0x8000 + i) != 0)
                bank2NonZero++;
        }
        EXPECT_EQ(bank2NonZero, 0)
            << "Page 2 must be zeroes - the final depack overwrote it from an empty page 4";

        uint8_t* page4 = memory->RAMPageAddress(4);
        int page4NonZero = 0;
        for (int i = 0; i < 16; i++)
        {
            if (page4[i] != 0)
                page4NonZero++;
        }
        EXPECT_EQ(page4NonZero, 0)
            << "Page 4 must stay empty - the SWAP hook reverted the page selection "
               "before LOAD *\"SCROLL12\" CODE, so the sectors never reached page 4";

        // The packed stream (header CD FF 93 85, from the TRD catalog) lands in page 0
        // instead - the page still mapped at $C000 when TR-DOS did the load
        uint8_t* page0 = memory->RAMPageAddress(0);
        uint32_t page0Head = (uint32_t)page0[0] << 24 | (uint32_t)page0[1] << 16 |
                             (uint32_t)page0[2] << 8 | page0[3];
        EXPECT_EQ(page0Head, 0xCDFF9385u)
            << "SCROLL12 packed data expected in page 0 (wrong page selected)";

        int mainNonZero = 0;
        for (int i = 0; i < 8; i++)
        {
            if (memory->DirectReadFromZ80Memory(0x9B6B + i) != 0)
                mainNonZero++;
        }
        EXPECT_EQ(mainNonZero, 0) << "$9B6B must hold zeros - final depack read an empty page 4";
    }
    else
    {
        EXPECT_GE(depackHits.load(), 6) << "All 6 depack calls must run (got " << depackHits.load() << ")";
        EXPECT_EQ(resetHits.load(), 0) << "Emulator was reset during demo boot - crash confirmed";
        EXPECT_GE(mainHits.load(), 1) << "Demo main code at $9B6B was never reached";

        // The menu must accept SPACE and the demo must actually start (STARTDEMO -> JP $8000)
        EXPECT_GE(startDemoHits.load(), 1) << "SPACE press never reached STARTDEMO $9CD6 - menu poll broken";
        EXPECT_GE(demoEntryHits.load(), 1) << "Demo never entered its main code at $8000 - "
                                              "either fade/seek crashed or the jump target was empty";
        // Demo must bring up IM2 (INIT1 -> IM2INI at $BF02, handler $BFBF firing every frame)
        EXPECT_TRUE(im2SetupHits.load() >= 1 || (cpu->im == 2 && cpu->i == 0xBE))
            << "IM2 never initialized";
        EXPECT_GE(im2HandlerHits.load(), 20) << "IM2 interrupt handler at $BFBF never fired - demo dead";

        // The demo must have depacked real data into the paged banks: part 6 depacks
        // its final output (demo code incl. the $9B6B entry) from SCROLL12, which the
        // loader places into page 4 at $C000 via a BASIC OUT 32765,20 command
        uint8_t* page4 = memory->RAMPageAddress(4);
        int page4NonZero = 0;
        for (int i = 0; i < 16; i++)
        {
            if (page4[i] != 0)
                page4NonZero++;
        }
        EXPECT_GT(page4NonZero, 0) << "Page 4 stayed empty - SCROLL12 was loaded into the wrong RAM page";

        int mainNonZero = 0;
        for (int i = 0; i < 8; i++)
        {
            if (memory->DirectReadFromZ80Memory(0x9B6B + i) != 0)
                mainNonZero++;
        }
        EXPECT_GT(mainNonZero, 0) << "$9B6B holds zeros - final depack produced garbage (NOP slide)";
    }
}

TEST_F(Scroller_Boot_Test, BootScrollerDemoTRD)
{
    BootAndRunScroller(false);
}

// User-reported path: default RESET=128 boot -> 128K service menu -> TR-DOS.
// Asserts the AUTHENTIC failure (see file header): stock ROM halves do not sync
// BANK_M on BASIC OUT commands, so the editor SWAP hook reverts the demo's page
// selection and the demo dies. No emulator compensation exists for this by design.
TEST_F(Scroller_Boot_Test, BootScrollerDemoTRD_Via128KMenu)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }
    _context->config.reset_rom = RM_128;
    _emulator->Reset();
    BootAndRunScroller(true);
}

void Scroller_Boot_Test::RealtimeGuiFlow(const char* label)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }
    std::cout << "\n========================================\n"
              << "[SCROLLER-REALTIME] " << label << "\n"
              << "========================================\n";

    Memory* memory = _context->pMemory;
    Z80* cpu = _context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
    std::string emulatorId = _emulator->GetId();

    // Boot into the 128K service menu, insert disk, enter TR-DOS - same as the
    // instrumented test, up to and including the command injection.
    for (int i = 0; i < 100; i++) mainLoop->RunFrame();
    std::string screen = ScreenOCR::ocrScreen(emulatorId);
    ASSERT_TRUE(screen.find("BASIC") != std::string::npos) << "128K menu expected:\n" << screen;

    std::string trdPath = TestPathHelper::GetTestDataPath("sound/covox/scroller_by_demarche.trd");
    LoaderTRD trdLoader(_context, trdPath);
    ASSERT_TRUE(trdLoader.loadImage()) << trdPath;
    WD1793* wd1793 = _context->pBetaDisk;
    ASSERT_NE(wd1793, nullptr);
    FDD* fdd = wd1793->getDrive();
    ASSERT_NE(fdd, nullptr);
    fdd->insertDisk(trdLoader.getImage());

    BasicEncoder::navigateToTRDOS(memory);
    for (int i = 0; i < 200; i++) mainLoop->RunFrame();
    screen = ScreenOCR::ocrScreen(emulatorId);
    ASSERT_TRUE(screen.find("A>") != std::string::npos) << "TR-DOS prompt expected:\n" << screen;

    // Write watchpoints: who materializes the menu body at $9B6B, and who
    // zeroes $8000? Auto-disarm after a few hits so a hot writer cannot stall
    // the run. (Memory breakpoints post the same NC_EXECUTION_BREAKPOINT.)
    BreakpointManager* bpMgr = _context->pDebugManager->GetBreakpointsManager();
    ASSERT_NE(bpMgr, nullptr);
    EmulatorTestHelper::EnableDebugFeatures(_emulator);
    std::atomic<int> watchHits{0};
    uint16_t brk9B6B = bpMgr->AddMemWriteBreakpoint(0x9B6B, "who_writes_menu");
    uint16_t brk8000 = bpMgr->AddMemWriteBreakpoint(0x8000, "who_writes_8000");
    // Sector-stream attribution: writes to $C000 land in whatever page bank 3
    // currently maps - logging value+bank3+pc per hit shows whether SCROLL12's
    // 48 sectors get split between page 4 and page 0 by the SWAP-hook revert.
    std::atomic<int> sectorWatchHits{0};
    uint16_t brkC000 = bpMgr->AddMemWriteBreakpoint(0xC000, "sector_stream");
    // Wandering-pc probe: does the CPU really execute at these page-2 landmarks?
    bpMgr->AddExecutionBreakpoint(0x9B6B, "probe_9B6B");
    bpMgr->AddExecutionBreakpoint(0xBFBF, "probe_BFBF");
    auto watchHandler = [&](int id, Message* msg)
    {
        (void)id; (void)msg;
        int hit = ++watchHits;
        uint8_t v9B6B = memory->DirectReadFromZ80Memory(0x9B6B);
        uint8_t v8000 = memory->DirectReadFromZ80Memory(0x8000);
        if (cpu->pc == 0x9B6B || cpu->pc == 0xBFBF)
        {
            std::cout << "[EXEC-PROBE pc=" << std::hex << cpu->pc << std::dec
                      << "] bytes=" << std::hex
                      << (int)memory->DirectReadFromZ80Memory(cpu->pc) << " "
                      << (int)memory->DirectReadFromZ80Memory(cpu->pc + 1) << " "
                      << (int)memory->DirectReadFromZ80Memory(cpu->pc + 2) << std::dec
                      << " prev_pc=" << std::hex << cpu->prev_pc << std::dec
                      << " im=" << (int)cpu->im << " i=" << std::hex << (int)cpu->i << std::dec << std::endl;
            _emulator->Resume();
            return;
        }
        if (cpu->pc != 0x3FEE && cpu->pc != 0x3FEB && cpu->pc != 0x3FE5 && cpu->pc != 0x3FE7 &&
            sectorWatchHits < 60)
        {
            // $3FEx = TR-DOS sector write loop (known); log everything else
            std::cout << "[WATCH hit " << hit << "] pc=" << std::hex << cpu->pc << std::dec
                      << " prev_pc=" << std::hex << cpu->prev_pc << std::dec
                      << " opcode=" << std::hex << (int)cpu->opcode << std::dec
                      << " IM=" << (int)cpu->im << " I=" << std::hex << (int)cpu->i << std::dec
                      << " IFF1=" << (int)cpu->iff1
                      << " bank3=page" << memory->GetRAMPageForBank3()
                      << " now[$9B6B]=" << std::hex << (int)v9B6B << " [$8000]=" << (int)v8000 << std::dec
                      << std::endl;
        }
        if (cpu->pc >= 0x3FE0 && cpu->pc <= 0x3FFF)
            sectorWatchHits++;  // count DOS sector writes separately
        if (hit == 60)
        {
            bpMgr->RemoveBreakpointByID(brk9B6B);
            bpMgr->RemoveBreakpointByID(brk8000);
            bpMgr->RemoveBreakpointByID(brkC000);
            std::cout << "[WATCH] mem watchpoints disarmed after 60 hits\n";
        }
        _emulator->Resume();
    };
    MessageCenter::DefaultMessageCenter().AddObserver(NC_EXECUTION_BREAKPOINT, watchHandler);

    // Per-frame CPU sampler (frame-refresh notifications fire at frame end):
    // dense trajectory of pc/opcode to identify what actually executes during
    // the post-depack phases (NOP slide? ROM reboot? a data-processing loop?).
    std::mutex sampleMutex;
    std::vector<std::string> frameSamples;
    int frameNo = 0;
    uint16_t lastSamplePc = 0xFFFF;
    auto frameHandler = [&](int id, Message* msg)
    {
        (void)id; (void)msg;
        int f = ++frameNo;
        uint16_t pc = cpu->pc;
        bool regionChanged = (pc ^ lastSamplePc) & 0xC000;
        if (f % 20 == 0 || regionChanged)
        {
            std::lock_guard<std::mutex> lock(sampleMutex);
            frameSamples.push_back(StringHelper::Format(
                "f=%d pc=%04X ppc=%04X op=%02X im=%d i=%02X iff1=%d p7FFD=%02X b=%02X%02X%02X",
                f, pc, cpu->prev_pc, cpu->opcode, cpu->im, cpu->i, cpu->iff1,
                _context->emulatorState.p7FFD,
                memory->DirectReadFromZ80Memory(pc),
                memory->DirectReadFromZ80Memory(pc + 1),
                memory->DirectReadFromZ80Memory(pc + 2)));
        }
        lastSamplePc = pc;
    };
    MessageCenter::DefaultMessageCenter().AddObserver(NC_VIDEO_FRAME_REFRESH, frameHandler);

    // From here on: NO execution breakpoints, NO CAT. Only the passive bus trace.
    std::mutex traceMutex;
    std::vector<std::string> outTrace;
    int seq = 0;
    cpu->busTraceHook = [&, cpu](char type, uint16_t port, uint8_t value)
    {
        if (type != 'O' || (port & 0x8006) != 0x0004) return;
        int bank3 = memory->GetRAMPageForBank3();
        std::lock_guard<std::mutex> lock(traceMutex);
        outTrace.push_back(StringHelper::Format("seq=%d value=%02X pc=%04X bank3=page%d trdos=%d",
                                                ++seq, value, cpu->pc, bank3,
                                                (_context->emulatorState.flags & CF_TRDOS) ? 1 : 0));
        // Live-print only page-relevant events: skip the steady editor $00/$10 toggle
        if ((value & 0x07) != 0 || seq <= 4)
            std::cout << "[TRACE] " << outTrace.back() << std::endl;
    };

    auto result = BasicEncoder::injectToTRDOS(memory, "RUN \"SCROLLER\"");
    EXPECT_TRUE(result.success) << result.message;
    BasicEncoder::injectEnter(memory);
    std::cout << "[REALTIME] RUN \"SCROLLER\" submitted - no CAT, no breakpoints\n";
    _emulator->StartAsync();

    MessageCenter& keyMc = MessageCenter::DefaultMessageCenter();
    bool menuSeen = false, spaceSent = false, resetSeen = false;
    int highPcSamples = 0, zeroPcSamples = 0, romStuckSamples = 0;
    auto statusLog = std::chrono::steady_clock::now();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(150);
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        int menuBytes = 0, bank2NonZero = 0;
        for (int i = 0; i < 8; i++)
            if (memory->DirectReadFromZ80Memory(0x9B6B + i)) menuBytes++;
        for (int i = 0; i < 0x4000; i += 7)  // sparse sample is plenty for liveness
            if (memory->DirectReadFromZ80Memory(0x8000 + i)) bank2NonZero++;

        if (!menuSeen && menuBytes >= 8)
        {
            menuSeen = true;
            std::cout << "[REALTIME] *** COVOX MENU PRESENT - page 2 depacked (user's boot symptom) ***\n";
            std::cout << "[REALTIME] $9B60-$9BAF dump: ";
            for (uint32_t a = 0x9B60; a < 0x9BB0; a++)
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << (int)memory->DirectReadFromZ80Memory(a) << " ";
            std::cout << std::dec << "\n";
        }
        if (menuSeen && !spaceSent && bank2NonZero > 100)
        {
            spaceSent = true;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            keyMc.Post(MC_KEY_PRESSED, new KeyboardEvent(ZXKEY_SPACE, KEY_PRESSED, _emulator->GetUUID()));
            std::this_thread::sleep_for(std::chrono::seconds(2));
            keyMc.Post(MC_KEY_RELEASED, new KeyboardEvent(ZXKEY_SPACE, KEY_RELEASED, _emulator->GetUUID()));
            std::cout << "[REALTIME] SPACE sent - watching for the post-SPACE crash\n";
        }

        uint16_t pc = cpu->pc;
        if (pc >= 0xFF00) highPcSamples++;
        if (pc == 0x0000) zeroPcSamples++;
        if (spaceSent && pc < 0x4000 && !(cpu->im == 2 && cpu->i == 0xBE))
            romStuckSamples++;

        if (std::chrono::steady_clock::now() - statusLog > std::chrono::seconds(5))
        {
            statusLog = std::chrono::steady_clock::now();
            std::cout << "[REALTIME] menu=" << menuBytes << "/8 bank2~" << bank2NonZero
                      << " PC=" << std::hex << pc << std::dec
                      << " pcbytes=" << std::hex
                      << (int)memory->DirectReadFromZ80Memory(pc) << " "
                      << (int)memory->DirectReadFromZ80Memory(pc + 1) << " "
                      << (int)memory->DirectReadFromZ80Memory(pc + 2) << std::dec
                      << " $9B6B=" << std::hex
                      << (int)memory->DirectReadFromZ80Memory(0x9B6B)
                      << (int)memory->DirectReadFromZ80Memory(0x9B6C) << std::dec
                      << " $8000=" << std::hex
                      << (int)memory->DirectReadFromZ80Memory(0x8000)
                      << (int)memory->DirectReadFromZ80Memory(0x8001) << std::dec
                      << " IM=" << (int)cpu->im << " I=" << std::hex << (int)cpu->i << std::dec
                      << " IFF1=" << (int)cpu->iff1
                      << " p7FFD=" << std::hex << (int)_context->emulatorState.p7FFD << std::dec
                      << " hiPC=" << highPcSamples << " zeroPC=" << zeroPcSamples << "\n" << std::flush;
        }

        // Reset signature after SPACE: ROM banner back on screen
        if (spaceSent && (romStuckSamples > 20 || highPcSamples > 0))
        {
            screen = ScreenOCR::ocrScreen(emulatorId);
            if (screen.find("1982") != std::string::npos || screen.find("BASIC") != std::string::npos)
            {
                resetSeen = true;
                std::cout << "[REALTIME] *** RESET AFTER SPACE (user's crash symptom) - screen shows ROM banner ***\n";
                break;
            }
        }
        if (spaceSent && romStuckSamples > 100)
        {
            resetSeen = true;  // call it a reset even if OCR can't read the banner
            std::cout << "[REALTIME] *** PC STUCK IN ROM after SPACE - treated as reset ***\n";
            break;
        }
    }
    _emulator->Stop();

    // ---- Final state dump ----
    std::cout << "\n[REALTIME] outcome: menuSeen=" << menuSeen << " spaceSent=" << spaceSent
              << " resetSeen=" << resetSeen << "\n";
    std::cout << "[REALTIME] PC=" << std::hex << cpu->pc << std::dec
              << " IM=" << (int)cpu->im << " I=" << std::hex << (int)cpu->i << std::dec
              << " IFF1=" << (int)cpu->iff1
              << " p7FFD=" << std::hex << (int)_context->emulatorState.p7FFD << std::dec << "\n";

    for (uint16_t page : {0, 1, 3, 4, 7})
    {
        uint8_t* addr = memory->RAMPageAddress(page);
        int nonZero = 0, first = -1, last = -1;
        for (int i = 0; i < 0x4000; i++)
        {
            if (addr[i])
            {
                nonZero++;
                if (first < 0) first = i;
                last = i;
            }
        }
        std::cout << "[REALTIME] page " << page << ": nonzero=" << nonZero << "/16384 first=$"
                  << std::hex << (first < 0 ? 0 : first) << " last=$" << (last < 0 ? 0 : last) << std::dec
                  << " head=" << std::hex << (int)addr[0] << (int)addr[1] << (int)addr[2] << (int)addr[3] << std::dec << "\n";
    }
    int bank2NonZero = 0;
    for (int i = 0; i < 0x4000; i++)
        if (memory->DirectReadFromZ80Memory(0x8000 + i)) bank2NonZero++;
    std::cout << "[REALTIME] bank2 $8000 nonzero=" << bank2NonZero << "/16384  $9B6B=";
    for (int i = 0; i < 6; i++)
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory->DirectReadFromZ80Memory(0x9B6B + i) << " ";
    std::cout << " $8000=";
    for (int i = 0; i < 6; i++)
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory->DirectReadFromZ80Memory(0x8000 + i) << " ";
    std::cout << std::dec << "\n";

    // Where did the SCROLL12 packed stream land? (header CD FF 93 85)
    for (uint16_t page : {0, 4})
    {
        uint8_t* addr = memory->RAMPageAddress(page);
        bool hasHeader = addr[0] == 0xCD && addr[1] == 0xFF && addr[2] == 0x93 && addr[3] == 0x85;
        std::cout << "[REALTIME] SCROLL12 packed header at page " << page << " offset 0: "
                  << (hasHeader ? "YES" : "no") << "\n";
    }

    // Loader page-OUTs (pc=$6210): the LOADTBL entry selections, DI-protected
    std::cout << "[REALTIME] loader OUTs (pc=6210) and other page-changing OUTs:\n";
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        int shown = 0;
        for (auto& s : outTrace)
        {
            if (s.find("pc=6210") != std::string::npos ||
                (s.find("value=") != std::string::npos &&
                 (s.find("value=04") != std::string::npos || s.find("value=05") != std::string::npos ||
                  s.find("value=07") != std::string::npos || s.find("value=14") != std::string::npos ||
                  s.find("value=15") != std::string::npos || s.find("value=10") != std::string::npos ||
                  s.find("value=11") != std::string::npos || s.find("value=13") != std::string::npos ||
                  s.find("value=17") != std::string::npos)))
            {
                // value=10 is the editor toggle too - keep it but cap output
                if (shown++ < 80)
                    std::cout << "  " << s << "\n";
            }
        }
        std::cout << "[REALTIME] total OUT #7FFD events: " << outTrace.size() << "\n";
        std::cout << "[REALTIME] last 12 events:\n";
        int skip = outTrace.size() > 12 ? (int)outTrace.size() - 12 : 0;
        for (size_t i = skip; i < outTrace.size(); i++)
            std::cout << "  " << outTrace[i] << "\n";
    }
    cpu->busTraceHook = nullptr;

    std::cout << "[REALTIME] frame samples (every 20th frame + region changes), "
              << frameNo << " frames total:\n";
    {
        std::lock_guard<std::mutex> lock(sampleMutex);
        int perLine = 0;
        for (auto& s : frameSamples)
        {
            std::cout << "  " << s;
            if (++perLine % 3 == 0) std::cout << "\n";
        }
        if (perLine % 3) std::cout << "\n";
    }

    SUCCEED() << "diagnostic run - see log above for outcome";
}

// Diagnostic only (run with --gtest_also_run_disabled_tests): the untouched,
// realtime version of the 128K-menu path. If this lands on the covox-menu side
// while the breakpoint/CAT-instrumented run lands on the zeroed-page-2 side,
// the harness perturbation - not emulator timing - decides the outcome.
TEST_F(Scroller_Boot_Test, DISABLED_RealtimeGuiFlow_NoPerturbation)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }
    _context->config.reset_rom = RM_128;
    _emulator->Reset();
    RealtimeGuiFlow("menu -> TR-DOS -> RUN, no breakpoints, no CAT");
}
