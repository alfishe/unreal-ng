#ifdef UNREALNG_HAVE_OPL4

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"
#include "loaders/disk/loader_trd.h"

/// Guest-level demo re-verification of the MoonSound port-claim override
/// (integration doc sections 2.5/2.6 - the moonsound_2.trd "FM timings
/// totally wrong" report).
///
/// The report environment was a ZX-Evo BaseConf (ATM3) with Pentagon
/// timings: Pentagon timing base plus the strict FPGA decode that keeps
/// dirty-high-byte FM register writes from double-delivering into the
/// motherboard's partial decodes. unreal-ng models that strictness at
/// runtime - the armed card's full-decode claims make the motherboard
/// decode stand down for ports #C4..#C7 / #7E / #7F - so the equivalent
/// host here is the plain PENTAGON model with the card staged.
///
/// This test boots the author's actual demo disk (see
/// testdata/sound/moonsound/SOURCES.md: TR-DOS image, `boot` file chains
/// into the player) and asserts the machine-state invariant the original
/// bug broke: while FM traffic with dirty high address bytes flows to the
/// card, the #7FFD paging latch may only ever change on a write to the
/// exact port address #7FFD. Pre-fix, `out (c),a` FM data writes with the
/// register number in B aliased into the latch and paged RAM at random -
/// the "timings totally wrong" symptom.
class MoonSoundDemoGuest_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();

        // Same staging as the disk boot regression suite: the staged ini
        // keeps MoonSound=1 while the TurboSound slot drops to AY, so the
        // FM cores of the idle TSFM pair do not burn a third of the wall
        // time for output nothing here observes
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("Pentagon", TurboSoundKind::AY, LoggerLevel::LogError);
        if (_emulator)
        {
            _context = _emulator->GetContext();

            // Deterministic 48K BASIC boot (same shape as the UDI boot test)
            _context->config.reset_rom = RM_SOS;
            _emulator->Reset();
            if (FeatureManager* features = _context->pFeatureManager)
            {
                features->setFeature(Features::kScreenHQ, false);
                features->setFeature(Features::kSoundHQ, false);
                // Every assertion reads emulated bus / machine state, so the
                // audio output stage can be skipped per frame (chip cores
                // keep advancing, machine state stays bit-identical)
                features->setFeature(Features::kSoundGeneration, false);
            }

            // Boot-bound: a real demo disk runs to its player. Turbo keeps
            // the wall time low; nothing here asserts on rendered pixels
            _emulator->EnableTurboMode();
        }
    }

    void TearDown() override
    {
        if (_emulator)
        {
            // Bare emulator (never registered with EmulatorManager): release
            // directly, the same way the UDI boot test fixture does
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            _emulator->Release();
            delete _emulator;
            _emulator = nullptr;
        }
        MessageCenter::DisposeDefaultMessageCenter();
    }

    /// OCR the current screen through the public per-cell API (same as the
    /// UDI boot test: the helper returns a bare, manager-unregistered
    /// emulator, so ScreenOCR::ocrScreen(emulatorId) cannot resolve it)
    std::string ocrCurrentScreen() const
    {
        Memory* memory = _emulator->GetMemory();
        std::string screen;
        screen.reserve(24 * (32 + 1));
        for (int row = 0; row < 24; row++)
        {
            for (int col = 0; col < 32; col++)
            {
                screen += ScreenOCR::ocrCell(memory, row, col);
            }
            screen += '\n';
        }
        return screen;
    }

    MainLoop_CUT* MainLoop()
    {
        return reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
    }

    /// Per-frame and cumulative bus statistics, updated from the CPU trace
    /// hook. RunFrame() is synchronous on the test thread, so no locking.
    struct BusStats
    {
        // per frame (reset by the driver loop)
        int fmOuts = 0;       // OUTs with low byte #C4..#C7 (FM register traffic)
        int waveOuts = 0;     // OUTs with low byte #7E/#7F (wave/PCM traffic)
        int pagingOuts = 0;   // OUTs to the exact address #7FFD

        // cumulative
        int fmTotal = 0;
        int fmDirtyHighTotal = 0;  // FM OUTs whose high address byte is nonzero (out (c),a form)
        int waveTotal = 0;
        int pagingTotal = 0;
        int statusInTotal = 0;     // INs of the FM status port #C4
        int waveInTotal = 0;       // INs of the wave data port #7F
        int lastCardFrame = -1;    // frame with the most recent FM/wave OUT

        // OUT low-byte histogram (diagnostics for the failing path)
        uint32_t outHist[256] = {};

        void ResetFrame()
        {
            fmOuts = 0;
            waveOuts = 0;
            pagingOuts = 0;
        }
    };
};

/// One #7FFD latch change that no exact-port write accounts for
struct PagingViolation
{
    int frame;
    uint8_t before;
    uint8_t after;
};

TEST_F(MoonSoundDemoGuest_Test, AuthorDisk_Moonsound2_PlaysFmTrafficWithoutPagingAlias)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    // Precondition: the staged config chain constructed the card
    SoundManager* soundManager = _context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    ASSERT_TRUE(soundManager->hasMoonSound());

    // STEP 1: ROM init to 48K BASIC
    std::string screen;
    for (int i = 0; i < 100; i++)
    {
        MainLoop()->RunFrame();
        if ((i + 1) % 5 == 0)
        {
            screen = ocrCurrentScreen();
            if (screen.find("1982") != std::string::npos || screen.find("Sinclair") != std::string::npos)
            {
                break;
            }
        }
    }
    ASSERT_TRUE(screen.find("1982") != std::string::npos || screen.find("Sinclair") != std::string::npos)
        << "48K BASIC expected. Got:\n"
        << screen;

    // STEP 2: insert the author's demo disk
    const std::string trdPath = TestPathHelper::GetTestDataPath("sound/moonsound/moonsound_2.trd");
    ASSERT_TRUE(FileHelper::FileExists(trdPath)) << "missing fixture: " << trdPath;
    LoaderTRD trdLoader(_context, trdPath);
    ASSERT_TRUE(trdLoader.loadImage()) << "TR-DOS image not loaded: " << trdPath;

    WD1793* wd1793 = _context->pBetaDisk;
    ASSERT_NE(wd1793, nullptr);
    FDD* fdd = wd1793->getDrive();
    ASSERT_NE(fdd, nullptr);
    fdd->insertDisk(trdLoader.getImage());

    // STEP 3: enter TR-DOS
    auto trdosEntry = BasicEncoder::runCommand(_emulator, "RANDOMIZE USR 15616");
    ASSERT_TRUE(trdosEntry.success) << trdosEntry.message;
    for (int i = 0; i < 100; i++)
    {
        MainLoop()->RunFrame();
        if ((i + 1) % 5 == 0)
        {
            screen = ocrCurrentScreen();
            if (screen.find("A>") != std::string::npos)
            {
                break;
            }
        }
    }
    ASSERT_TRUE(screen.find("A>") != std::string::npos) << "TR-DOS prompt expected. Got:\n" << screen;

    // STEP 4: bus statistics hook. FM register traffic (#C4..#C7) and wave
    // traffic (#7E/#7F) count regardless of the high address byte - the
    // demo's player uses out (c),a with the register number in B, so the
    // alias-prone dirty-high-byte form is counted separately
    Z80* cpu = _context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr);

    BusStats stats;
    std::vector<PagingViolation> violations;
    std::deque<std::string> fdcTrace;  // rolling FDC register access log (diagnostics)
    std::deque<std::string> ffPollTrace;  // rolling Beta128 #FF status-poll log (diagnostics)
    int frame = 0;

    cpu->busTraceHook = [&stats, &frame, &fdcTrace, &ffPollTrace, cpu](char type, uint16_t port, uint8_t value)
    {
        const uint16_t low = static_cast<uint16_t>(port & 0x00FF);
        const bool isPort = (type == 'I' || type == 'O');
        // Commands/params/status only - the Beta128 #FF ready-poll spam of a
        // stuck wait loop would evict the interesting history from the window
        const bool traceIt = isPort && ((type == 'O' && (low == 0x1F || low == 0x3F || low == 0x5F || low == 0x7F || low == 0xFF)) ||
                                        (type == 'I' && low == 0x1F));
        if (traceIt)
        {
            char buf[80];
            snprintf(buf, sizeof(buf), "f%-5d %c #%04X v=%02X pc=%04X", frame, type, port, value, cpu->m1_pc);
            fdcTrace.push_back(buf);
            if (fdcTrace.size() > 200)
            {
                fdcTrace.pop_front();
            }
        }
        // Pentagon #FF decode rule: (low & 0x83) == 0x83 - keep the last polls
        // with PC so a stuck ROM wait loop can be located in the TR-DOS ROM
        if (isPort && type == 'I' && (low & 0x83) == 0x83)
        {
            char buf[80];
            snprintf(buf, sizeof(buf), "f%-5d %c #%04X v=%02X pc=%04X", frame, type, port, value, cpu->m1_pc);
            ffPollTrace.push_back(buf);
            if (ffPollTrace.size() > 48)
            {
                ffPollTrace.pop_front();
            }
        }
        if (type == 'I')
        {
            if (low == 0xC4)
            {
                stats.statusInTotal++;
            }
            else if (low == 0x7F)
            {
                stats.waveInTotal++;
            }
            return;
        }
        if (type != 'O')
        {
            return;
        }
        stats.outHist[low]++;
        if (low >= 0xC4 && low <= 0xC7)
        {
            stats.fmOuts++;
            stats.fmTotal++;
            stats.lastCardFrame = frame;
            if ((port >> 8) != 0)
            {
                stats.fmDirtyHighTotal++;
            }
        }
        else if (low == 0x7E || low == 0x7F)
        {
            stats.waveOuts++;
            stats.waveTotal++;
            stats.lastCardFrame = frame;
        }
        else if (port == 0x7FFD)
        {
            stats.pagingOuts++;
            stats.pagingTotal++;
        }
    };

    /// Runs frames while checking the paging invariant; returns the frames run
    auto runFrames = [&](int frames) -> int
    {
        int active = 0;
        for (int i = 0; i < frames; i++)
        {
            const uint8_t latchBefore = _context->emulatorState.p7FFD;
            stats.ResetFrame();
            MainLoop()->RunFrame();
            frame++;

            const uint8_t latchAfter = _context->emulatorState.p7FFD;
            if (latchAfter != latchBefore && stats.pagingOuts == 0)
            {
                violations.push_back({frame, latchBefore, latchAfter});
            }
            if (stats.fmOuts > 0)
            {
                active++;
            }
        }
        return active;
    };

    // STEP 5: run the demo. `boot` (a 41-sector BASIC stub) chains into the
    // player, which shows its title picture and waits for a tune key
    // (A-F per SOURCES.md) - let the load settle first
    auto run = BasicEncoder::injectToTRDOS(_context->pMemory, "RUN \"boot\"");
    ASSERT_TRUE(run.success) << run.message;
    BasicEncoder::injectEnter(_context->pMemory);
    runFrames(400);

    // STEP 6: select tune 1, then wait for the player to program the card
    // (detection protocol plus the first tune's register traffic)
    Keyboard* keyboard = _context->pKeyboard;
    ASSERT_NE(keyboard, nullptr);
    keyboard->PressKey(ZXKEY_A);
    runFrames(5);
    keyboard->ReleaseKey(ZXKEY_A);

    int loadFrames = 405;
    for (int i = 0; i < 4800 && stats.fmTotal < 300; i++)
    {
        runFrames(1);
        loadFrames++;
        if (loadFrames % 500 == 0)
        {
            std::cout << "[poll] frame " << loadFrames << " fdcDataIn(7F)=" << stats.waveInTotal
                      << " fdWrites=" << stats.outHist[0xFD] << " fmTotal=" << stats.fmTotal << "\n";
        }
    }

    if (stats.fmTotal < 300)
    {
        std::cout << "[STEP 6] demo never reached card-driving code after " << loadFrames << " frames.\n"
                  << "fmTotal=" << stats.fmTotal << " waveTotal=" << stats.waveTotal
                  << " statusIn(C4)=" << stats.statusInTotal << " waveIn(7F)=" << stats.waveInTotal
                  << " lastCardFrame=" << stats.lastCardFrame << "\n";
        std::cout << "OUT low-byte histogram (top 12):";
        for (int pass = 0; pass < 12; pass++)
        {
            int best = -1;
            for (int i = 0; i < 256; i++)
            {
                if (best < 0 || stats.outHist[i] > stats.outHist[best])
                {
                    best = i;
                }
            }
            if (best < 0 || stats.outHist[best] == 0)
            {
                break;
            }
            char buf[40];
            snprintf(buf, sizeof(buf), " #%02X:%u", best, stats.outHist[best]);
            std::cout << buf;
            stats.outHist[best] = 0;
        }
        std::cout << "\nLast screen:\n"
                  << ocrCurrentScreen() << "\n";
        const WD1793& wdConst = *wd1793;  // const ref picks the public getStatusRegister() const overload
        std::cout << "WD1793 snapshot: fsm=" << WD1793::WDSTATEToString(wd1793->getFSMState())
                  << " cmd=#" << std::hex << static_cast<int>(wd1793->getCommandRegister())
                  << " status=#" << static_cast<int>(wdConst.getStatusRegister())
                  << " beta128=#" << static_cast<int>(wd1793->getBeta128Status())
                  << " trackReg=" << std::dec << static_cast<int>(wd1793->getTrackRegister())
                  << " sectorReg=" << static_cast<int>(wd1793->getSectorRegister())
                  << " driveTrack=" << static_cast<int>(fdd->getTrack())
                  << " motor=" << (fdd->getMotor() ? "on" : "off") << "\n";
        std::cout << "FDC trace (last " << fdcTrace.size() << " events):\n";
        for (const std::string& e : fdcTrace)
        {
            std::cout << "  " << e << "\n";
        }
        std::cout << "#FF poll trace (last " << ffPollTrace.size() << " polls):\n";
        for (const std::string& e : ffPollTrace)
        {
            std::cout << "  " << e << "\n";
        }
    }
    ASSERT_GE(stats.fmTotal, 300) << "demo did not drive the FM side of the card (see screen dump above)";

    constexpr int kPlaybackFrames = 100;
    const int fmActiveFrames = runFrames(kPlaybackFrames);

    // Clear the hook before any teardown path can run the CPU again
    cpu->busTraceHook = nullptr;

    std::cout << "[stats] loadFrames=" << loadFrames << " fmTotal=" << stats.fmTotal
              << " fmDirtyHigh=" << stats.fmDirtyHighTotal << " waveTotal=" << stats.waveTotal
              << " pagingTotal=" << stats.pagingTotal << " fmActiveFrames=" << fmActiveFrames << "/"
              << kPlaybackFrames << "\n";

    // The demo must actually be playing: sustained FM register traffic through
    // the playback window (envelope/timbre updates per tick), and the PCM path
    // (wave-ROM sample playback) alive as well
    EXPECT_GE(fmActiveFrames, kPlaybackFrames / 2) << "FM traffic stopped inside the playback window";
    EXPECT_GT(stats.waveTotal, 0) << "no wave/PCM traffic at all";

    // The regression core: dirty-high-byte FM writes were present, and none of
    // them leaked into the #7FFD paging latch. Pre-fix the latch flipped on
    // frames with zero exact #7FFD writes (FM data values as page numbers)
    EXPECT_GT(stats.fmDirtyHighTotal, 0) << "alias-prone traffic never appeared - test exercised nothing";
    EXPECT_TRUE(violations.empty()) << [&]()
    {
        std::string message = "paging latch changed without an exact #7FFD write";
        for (size_t i = 0; i < violations.size() && i < 8; i++)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "\n  frame %d: %02X -> %02X",
                     violations[i].frame, violations[i].before, violations[i].after);
            message += buf;
        }
        if (violations.size() > 8)
        {
            char buf[48];
            snprintf(buf, sizeof(buf), "\n  ... and %zu more", violations.size() - 8);
            message += buf;
        }
        return message;
    }();
}

#endif // UNREALNG_HAVE_OPL4
