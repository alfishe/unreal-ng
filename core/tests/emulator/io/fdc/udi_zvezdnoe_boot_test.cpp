#include <gtest/gtest.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/modulelogger.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "loaders/disk/loader_trd.h"

/// Live repro for: "Zvezdnoe Nasledie.udi - catalog shows in TR-DOS, but following load fails".
/// Full-emulator flow: boot Pentagon -> insert UDI -> enter TR-DOS -> CAT -> LOAD "boot" (1 sector,
/// logical track 9 = cylinder 4 side 1) -> LOAD "BLOK" (3 sectors, same track).
/// A port trace records every OUT to the WD1793 register ports (#1F/#3F/#5F/#7F) and the
/// Beta128 system port (#FF) so the exact command stream the ROM issues is captured.

class UdiZvezdnoeBoot_Test : public ::testing::Test
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

    static std::string FirstLines(const std::string& screen, size_t lines)
    {
        std::string result;
        size_t count = 0;
        size_t pos = 0;
        while (count < lines && pos < screen.size())
        {
            size_t end = screen.find('\n', pos);
            if (end == std::string::npos)
            {
                end = screen.size();
            }
            std::string line = screen.substr(pos, end - pos);
            if (!line.empty())
            {
                count++;
            }
            result += line + "\n";
            pos = end + 1;
        }
        return result;
    }
};

namespace
{
struct PortEvent
{
    uint64_t t;
    char type;  // 'O' = OUT, 'I' = IN
    uint16_t port;
    uint16_t pc;
    uint8_t value;
};

std::string DecodeBeta128(uint8_t value)
{
    char buf[96];
    snprintf(buf, sizeof(buf),
             "drive=%d side=%d reset=%d hlt=%d bits=%02X",
             value & 3,
             (value & 0x10) ? 0 : 1,  // side bit is inverted: bit4=0 -> side 1
             (value & 0x04) ? 0 : 1,
             (value & 0x08) ? 1 : 0,
             value);
    return buf;
}

std::string DecodeCommand(uint8_t value)
{
    if ((value & 0x80) == 0x00)
    {
        static const char* names[] = {"RESTORE", "SEEK", "STEP", "STEP-IN", "STEP-OUT", "TYPE1-5", "TYPE1-6", "TYPE1-7"};
        std::string s = names[(value >> 5) & 0x07];
        if (value & 0x10)
        {
            s += " u";  // update track register
        }
        if (value & 0x04)
        {
            s += " v";  // verify
        }
        return s;
    }
    if ((value & 0xE0) == 0x80)
    {
        std::string s = value & 0x10 ? "READ SECTOR multi" : "READ SECTOR";
        if (value & 0x04)
        {
            s += " +15ms";
        }
        if (value & 0x02)
        {
            s += " +sidecmp";
        }
        if (value & 0x08)
        {
            s += " +side1";
        }
        return s;
    }
    if ((value & 0xE0) == 0xA0)
    {
        std::string s = value & 0x10 ? "WRITE SECTOR multi" : "WRITE SECTOR";
        if (value & 0x02)
        {
            s += " +sidecmp";
        }
        return s;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "CMD 0x%02X", value);
    return buf;
}
}  // namespace

TEST_F(UdiZvezdnoeBoot_Test, CatThenLoadFiles)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    Memory* memory = _context->pMemory;
    std::string emulatorId = _emulator->GetId();
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);

    // STEP 1: ROM init
    for (int i = 0; i < 100; i++)
    {
        mainLoop->RunFrame();
    }
    std::string screen = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 1] Screen after ROM init:\n" << FirstLines(screen, 4) << "\n";
    ASSERT_TRUE(screen.find("1982") != std::string::npos || screen.find("Sinclair") != std::string::npos)
        << "48K BASIC expected. Got:\n"
        << screen;

    // STEP 2: insert the disk. The original repro image is the UDI (testdata/loaders/udi/Zvezdnoe Nasledie.udi);
    // it is converted 1:1 to a TRD in scratch (same TR-DOS sector payload order) because the UDI loader
    // is concurrently being reworked. TR-DOS sees the exact same catalog and file chain either way.
    std::string trdPath = TestPathHelper::GetTestScratchPath("ZvezdnoeNasledie.trd");
    LoaderTRD trdLoader(_context, trdPath);
    ASSERT_TRUE(trdLoader.loadImage()) << "TRD not loaded: " << trdPath;

    WD1793* wd1793 = _context->pBetaDisk;
    ASSERT_NE(wd1793, nullptr);
    FDD* fdd = wd1793->getDrive();
    ASSERT_NE(fdd, nullptr);
    fdd->insertDisk(trdLoader.getImage());
    std::cout << "[STEP 2] TRD (converted from UDI) inserted: " << trdPath << "\n";

    // STEP 3: enter TR-DOS
    auto trdosEntry = BasicEncoder::runCommand(_emulator, "RANDOMIZE USR 15616");
    ASSERT_TRUE(trdosEntry.success) << trdosEntry.message;
    for (int i = 0; i < 200; i++)
    {
        mainLoop->RunFrame();
    }
    screen = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 3] Screen after TR-DOS entry:\n" << FirstLines(screen, 6) << "\n";
    ASSERT_TRUE(screen.find("A>") != std::string::npos) << "TR-DOS prompt expected. Got:\n" << screen;

    // STEP 4: port trace (OUTs to WD1793 registers and Beta128 system port).
    // Also captures INs, but only after the first multi-sector READ SECTOR command (0x9C)
    // is observed - that read is the one that fails with a Lost Data storm.
    Z80* cpu = _context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);

    std::mutex traceMutex;
    std::vector<PortEvent> trace;    // OUT events
    std::vector<PortEvent> ins;      // IN events, only after CMD 0x9C
    bool captureIns = false;
    cpu->busTraceHook = [cpu, &traceMutex, &trace, &ins, &captureIns](char type, uint16_t port, uint8_t value)
    {
        uint16_t low = static_cast<uint16_t>(port & 0x00FF);
        if (low != 0x1F && low != 0x3F && low != 0x5F && low != 0x7F && low != 0xFF)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(traceMutex);
        if (type == 'O')
        {
            trace.push_back({cpu->t, 'O', low, cpu->m1_pc, value});
            if (low == 0x1F && value == 0x9C)
            {
                captureIns = true;
            }
        }
        else if (type == 'I' && captureIns && ins.size() < 250000)
        {
            ins.push_back({cpu->t, 'I', low, cpu->m1_pc, value});
        }
    };

    auto dumpTrace = [&traceMutex, &trace](const char* label)
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        std::cout << "--- port trace (" << label << ", " << trace.size() << " events) ---\n";
        for (const auto& e : trace)
        {
            char line[160];
            switch (e.port)
            {
                case 0x1F:
                    snprintf(line, sizeof(line), "t=%-10llu #1F CMD  0x%02X  %s", (unsigned long long)e.t, e.value,
                             DecodeCommand(e.value).c_str());
                    break;
                case 0x3F:
                    snprintf(line, sizeof(line), "t=%-10llu #3F TRK  %d", (unsigned long long)e.t, e.value);
                    break;
                case 0x5F:
                    snprintf(line, sizeof(line), "t=%-10llu #5F SEC  %d", (unsigned long long)e.t, e.value);
                    break;
                case 0x7F:
                    snprintf(line, sizeof(line), "t=%-10llu #7F DATA 0x%02X", (unsigned long long)e.t, e.value);
                    break;
                default:
                    snprintf(line, sizeof(line), "t=%-10llu #FF BETA 0x%02X  %s", (unsigned long long)e.t, e.value,
                             DecodeBeta128(e.value).c_str());
                    break;
            }
            std::cout << line << "\n";
        }
        std::cout << "--- end trace ---\n";
    };

    // Helper: inject a TR-DOS command and run until screen stabilizes into one of the markers
    auto runTrdosCommand = [&](const std::string& command, int maxFrames) -> std::string
    {
        auto result = BasicEncoder::injectToTRDOS(memory, command);
        EXPECT_TRUE(result.success) << result.message;
        BasicEncoder::injectEnter(memory);
        std::string last;
        for (int i = 0; i < maxFrames; i++)
        {
            mainLoop->RunFrame();
            if (i % 25 == 24)
            {
                last = ScreenOCR::ocrScreen(emulatorId);
                if (last.find("Retry") != std::string::npos || last.find("Abort") != std::string::npos ||
                    last.find("0 OK") != std::string::npos || last.find("0  OK") != std::string::npos)
                {
                    break;
                }
            }
        }
        return ScreenOCR::ocrScreen(emulatorId);
    };

    // STEP 5: CAT (control - must work per user report)
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        trace.clear();
    }
    std::string catScreen = runTrdosCommand("CAT", 600);
    std::cout << "[STEP 5] CAT screen:\n" << catScreen << "\n";
    bool catOk = catScreen.find("BLOK") != std::string::npos;
    dumpTrace("CAT");
    EXPECT_TRUE(catOk) << "Catalog should list BLOK";
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        trace.clear();
    }

    // Dump one LOAD phase: OUT aggregates, raw window around the first 0x9C command,
    // and the IN events captured after it (what the ROM polled / read while it failed).
    auto dumpPhase = [&traceMutex, &trace, &ins, &captureIns](const char* label)
    {
        std::lock_guard<std::mutex> lock(traceMutex);

        // Aggregate OUT events by (pc, port, value)
        std::cout << "--- OUT aggregates (" << label << ", " << trace.size() << " events) ---\n";
        std::map<std::string, int> outAgg;
        for (const auto& e : trace)
        {
            char key[48];
            snprintf(key, sizeof(key), "pc=%04X port=%02X val=%02X", e.pc, e.port, e.value);
            outAgg[key]++;
        }
        for (const auto& kv : outAgg)
        {
            std::cout << "  " << kv.first << "  x" << kv.second << "\n";
        }

        // Raw window around the first 0x9C command
        size_t idx9C = trace.size();
        for (size_t i = 0; i < trace.size(); i++)
        {
            if (trace[i].port == 0x1F && trace[i].value == 0x9C)
            {
                idx9C = i;
                break;
            }
        }
        std::cout << "--- raw OUT window around CMD 0x9C (index " << idx9C << ") ---\n";
        for (size_t i = (idx9C > 6 ? idx9C - 6 : 0); i < trace.size() && i < idx9C + 25; i++)
        {
            const auto& e = trace[i];
            std::cout << "  t=" << e.t << " OUT #" << std::hex << std::uppercase << e.port << std::dec
                      << " val=0x" << std::hex << std::setw(2) << std::setfill('0') << (int)e.value << std::dec
                      << " pc=" << std::hex << std::uppercase << e.pc << std::dec << "\n";
        }

        // IN events captured after CMD 0x9C: raw head + aggregates
        std::cout << "--- IN events after CMD 0x9C (" << ins.size() << " captured) ---\n";
        size_t rawCount = ins.size() < 100 ? ins.size() : 100;
        for (size_t i = 0; i < rawCount; i++)
        {
            const auto& e = ins[i];
            std::cout << "  t=" << e.t << " IN  #" << std::hex << std::uppercase << e.port << std::dec
                      << " val=0x" << std::hex << std::setw(2) << std::setfill('0') << (int)e.value << std::dec
                      << " pc=" << std::hex << std::uppercase << e.pc << std::dec << "\n";
        }
        std::map<std::string, int> inAgg;
        for (const auto& e : ins)
        {
            char key[48];
            snprintf(key, sizeof(key), "pc=%04X port=%02X val=%02X", e.pc, e.port, e.value);
            inAgg[key]++;
        }
        std::cout << "--- IN aggregates (top 40 of " << inAgg.size() << " distinct) ---\n";
        size_t shown = 0;
        for (const auto& kv : inAgg)
        {
            std::cout << "  " << kv.first << "  x" << kv.second << "\n";
            if (++shown >= 40)
            {
                break;
            }
        }

        trace.clear();
        ins.clear();
        captureIns = false;
    };

    // STEP 6: LOAD "boot" - single sector, logical track 9 (cylinder 4, side 1), sector 14+1
    // Enable WD1793 FSM logging to see the FDC-side outcome of every command
    _context->pModuleLogger->TurnOnLoggingForModule(PlatformModulesEnum::MODULE_DISK,
                                                    PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);
    _context->pModuleLogger->SetLoggingLevel(LoggerLevel::LogInfo);
    std::string bootScreen = runTrdosCommand("LOAD \"boot\"", 800);
    std::cout << "[STEP 6] LOAD \"boot\" screen:\n" << FirstLines(bootScreen, 8) << "\n";
    bool bootOk = bootScreen.find("Retry") == std::string::npos && bootScreen.find("Abort") == std::string::npos;
    std::cout << "[STEP 6] LOAD \"boot\" -> " << (bootOk ? "no error prompt" : "ERROR prompt") << "\n";
    dumpPhase("LOAD boot");

    // Back to TR-DOS prompt before the next command
    for (int i = 0; i < 50; i++)
    {
        mainLoop->RunFrame();
    }

    // STEP 7: LOAD "BLOK" - 3 consecutive sectors from logical track 9 (cylinder 4, side 1)
    std::string blokScreen = runTrdosCommand("LOAD \"BLOK\"", 3000);
    std::cout << "[STEP 7] LOAD \"BLOK\" screen:\n" << FirstLines(blokScreen, 8) << "\n";
    bool blokOk = blokScreen.find("Retry") == std::string::npos && blokScreen.find("Abort") == std::string::npos;
    std::cout << "[STEP 7] LOAD \"BLOK\" -> " << (blokOk ? "no error prompt" : "ERROR prompt") << "\n";
    dumpPhase("LOAD BLOK");

    cpu->busTraceHook = nullptr;
}
