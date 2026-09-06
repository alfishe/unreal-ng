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
#include "common/filehelper.h"
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
#include "loaders/disk/loader_udi.h"

/// Regression test for: "Zvezdnoe Nasledie.udi - catalog shows in TR-DOS, but following load fails".
/// Full-emulator flow with the user's original UDI: boot Pentagon -> insert disk -> enter TR-DOS ->
/// CAT -> LOAD "boot" (1 sector of BASIC at logical track 14, auto-run line 60, starts the game
/// loader) -> LOAD "BLOK" (3 sectors of BASIC at logical track 0, sectors 10..12).
///
/// Root cause that made the loads fail: the game's loader (through TR-DOS 5.04T COPY machinery)
/// skips sectors with a multi-sector READ SECTOR (0x9C) that polls only INTRQ and runs to the end
/// of the track. The controller treated the overrun past the last sector number as Record Not
/// Found (plus a multi-revolution search penalty); TR-DOS reports that as a disk error. Per
/// datasheet the multi-sector command terminates cleanly at the end of the track - see
/// WD1793::_multiSectorOverrun.
///
/// A port trace records every OUT to the WD1793 register ports (#1F/#3F/#5F/#7F) and the
/// Beta128 system port (#FF), and every IN after the first 0x9C command, so the command stream
/// the ROM issues is captured for diagnostics.

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
    int frame;
};

/// Outcome of one traced LOAD phase
struct PhaseResult
{
    bool sawMultiRead = false;  // A multi-sector READ SECTOR (0x9C) was issued in this phase
    int rnfStatusReads = 0;     // Status reads (#1F) that returned Record Not Found (bit 4)
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

    // STEP 2: insert the original UDI (raw MFM track streams, loaded losslessly into the
    // universal track model)
    std::string udiPath = TestPathHelper::GetTestDataPath("loaders/udi/Zvezdnoe Nasledie.udi");
    if (!FileHelper::FileExists(udiPath))
    {
        GTEST_SKIP() << "Test fixture not available: " << udiPath;
    }
    LoaderUDI udiLoader(_context, udiPath);
    ASSERT_TRUE(udiLoader.loadImage()) << "UDI not loaded: " << udiPath;

    WD1793* wd1793 = _context->pBetaDisk;
    ASSERT_NE(wd1793, nullptr);
    FDD* fdd = wd1793->getDrive();
    ASSERT_NE(fdd, nullptr);
    fdd->insertDisk(udiLoader.getImage());
    std::cout << "[STEP 2] UDI inserted: " << udiPath << "\n";

    // STEP 3: enter TR-DOS
    auto trdosEntry = BasicEncoder::runCommand(_emulator, "RANDOMIZE USR 15616");
    ASSERT_TRUE(trdosEntry.success) << trdosEntry.message;
    for (int i = 0; i < 100; i++)
    {
        mainLoop->RunFrame();
    }
    screen = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 3] Screen after TR-DOS entry:\n" << FirstLines(screen, 6) << "\n";
    ASSERT_TRUE(screen.find("A>") != std::string::npos) << "TR-DOS prompt expected. Got:\n" << screen;

    // STEP 4: port trace (OUTs to WD1793 registers and Beta128 system port).
    // Also captures INs, but only after the first multi-sector READ SECTOR command (0x9C)
    // is observed - that read is the one whose track overrun used to fail with RNF.
    Z80* cpu = _context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);

    int currentFrame = 0;
    int lastDiskIoFrame = 0;
    int diskIoEvents = 0;
    std::mutex traceMutex;
    std::vector<PortEvent> trace;    // OUT events
    std::vector<PortEvent> ins;      // IN events, only after CMD 0x9C
    bool captureIns = false;
    cpu->busTraceHook = [cpu, &traceMutex, &trace, &ins, &captureIns, &currentFrame, &lastDiskIoFrame, &diskIoEvents](char type, uint16_t port, uint8_t value)
    {
        uint16_t low = static_cast<uint16_t>(port & 0x00FF);
        if (low != 0x1F && low != 0x3F && low != 0x5F && low != 0x7F && low != 0xFF)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(traceMutex);
        if (type == 'O')
        {
            trace.push_back({cpu->t, 'O', low, cpu->m1_pc, value, currentFrame});
            if (low != 0xFF)
            {
                lastDiskIoFrame = currentFrame;
                diskIoEvents++;
            }
            if (low == 0x1F && value == 0x9C)
            {
                captureIns = true;
            }
        }
        else if (type == 'I' && captureIns && ins.size() < 250000)
        {
            ins.push_back({cpu->t, 'I', low, cpu->m1_pc, value, currentFrame});
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

    auto runTrdosCommand = [&](const std::string& command, int maxFrames,
                               std::function<bool(int framesInCmd, std::string& lastOcr)> isDone = nullptr) -> std::string
    {
        diskIoEvents = 0;
        lastDiskIoFrame = 0;
        auto result = BasicEncoder::injectToTRDOS(memory, command);
        EXPECT_TRUE(result.success) << result.message;
        BasicEncoder::injectEnter(memory);
        std::string last;
        for (int i = 0; i < maxFrames; i++)
        {
            mainLoop->RunFrame();
            currentFrame++;
            int framesInCmd = i + 1;
            if (isDone && isDone(framesInCmd, last))
            {
                break;
            }
        }
        if (last.empty())
        {
            last = ScreenOCR::ocrScreen(emulatorId);
        }
        return last;
    };

    // STEP 5: CAT (control - must work per user report)
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        trace.clear();
    }
    std::string catScreen = runTrdosCommand("CAT", 600, [&](int f, std::string& lastOcr) {
        if (f >= 50 && f % 25 == 0)
        {
            lastOcr = ScreenOCR::ocrScreen(emulatorId);
            if (lastOcr.find("BLOK") != std::string::npos && lastOcr.find("A>") != std::string::npos)
            {
                return true;
            }
        }
        return false;
    });
    std::cout << "[STEP 5] CAT screen:\n" << catScreen << "\n";
    bool catOk = catScreen.find("BLOK") != std::string::npos;
    dumpTrace("CAT");
    EXPECT_TRUE(catOk) << "Catalog should list BLOK";
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        trace.clear();
    }

    // Dump one LOAD phase: OUT aggregates, raw window around the first 0x9C command,
    // and the IN events captured after it (what the ROM polled / read). Returns the
    // phase outcome for the regression assertions below.
    auto dumpPhase = [&traceMutex, &trace, &ins, &captureIns](const char* label) -> PhaseResult
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        PhaseResult result;

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
        result.sawMultiRead = idx9C < trace.size();
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

            // Every status read (#1F) must be free of Record Not Found (bit 4): the multi-sector
            // overrun ends cleanly, TR-DOS never sees a disk error
            if (e.port == 0x1F && (e.value & 0x10))
            {
                result.rnfStatusReads++;
            }
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
        return result;
    };

    // STEP 6: LOAD "boot" - 1 sector of BASIC at logical track 14 sector 10. The program
    // auto-runs (line 60) and starts the game's own loader.
    std::string bootScreen = runTrdosCommand("LOAD \"boot\"", 400, [&](int f, std::string&) {
        return diskIoEvents > 20 && lastDiskIoFrame > 0 && (currentFrame - lastDiskIoFrame >= 25);
    });
    std::cout << "[STEP 6] LOAD \"boot\" screen:\n" << FirstLines(bootScreen, 8) << "\n";
    bool bootOk = bootScreen.find("Retry") == std::string::npos && bootScreen.find("Abort") == std::string::npos;
    std::cout << "[STEP 6] LOAD \"boot\" -> " << (bootOk ? "no error prompt" : "ERROR prompt") << "\n";
    PhaseResult bootPhase = dumpPhase("LOAD boot");
    EXPECT_TRUE(bootOk) << "LOAD \"boot\" ended with a TR-DOS error prompt";
    EXPECT_EQ(bootPhase.rnfStatusReads, 0) << "READ SECTOR must not report Record Not Found";

    // Back to TR-DOS prompt before the next command
    for (int i = 0; i < 20; i++)
    {
        mainLoop->RunFrame();
    }

    // STEP 7: LOAD "BLOK" - 3 consecutive sectors of BASIC at logical track 0 sectors 10..12.
    // The game's loader skips sectors with a multi-sector READ (0x9C) that runs to the end of
    // the track - the exact command that used to fail with RNF.
    std::string blokScreen = runTrdosCommand("LOAD \"BLOK\"", 900, [&](int f, std::string& lastOcr) {
        if (!captureIns || lastDiskIoFrame == 0 || (currentFrame - lastDiskIoFrame < 25))
        {
            return false;
        }
        if (f % 25 == 0)
        {
            lastOcr = ScreenOCR::ocrScreen(emulatorId);
            bool hasGfx = lastOcr.find("??????") != std::string::npos;
            return hasGfx;
        }
        return false;
    });
    std::cout << "[STEP 7] LOAD \"BLOK\" screen:\n" << FirstLines(blokScreen, 8) << "\n";
    bool blokOk = blokScreen.find("Retry") == std::string::npos && blokScreen.find("Abort") == std::string::npos;
    std::cout << "[STEP 7] LOAD \"BLOK\" -> " << (blokOk ? "no error prompt" : "ERROR prompt") << "\n";
    PhaseResult blokPhase = dumpPhase("LOAD BLOK");
    EXPECT_TRUE(blokOk) << "LOAD \"BLOK\" ended with a TR-DOS error prompt";
    EXPECT_TRUE(blokPhase.sawMultiRead) << "The game's loader issues a multi-sector READ SECTOR (0x9C)";
    EXPECT_EQ(blokPhase.rnfStatusReads, 0) << "Multi-sector overrun past the last sector must end without Record Not Found";

    // The game renders graphics - ScreenOCR decodes them as runs of '?' (unknown glyphs),
    // while a failed load leaves a plain TR-DOS text screen behind
    EXPECT_NE(blokScreen.find("??????"), std::string::npos)
        << "Game graphics expected on screen after loading. Got:\n"
        << blokScreen;

    cpu->busTraceHook = nullptr;
}
