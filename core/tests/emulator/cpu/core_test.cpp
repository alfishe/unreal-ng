// Memory-interface selection and the host bus overlay slot
// (neogs-zxdma-design.md §5.2-§5.3, §8.1-§8.2).
//
// Guards two promises:
//   - with no overlay installed, the host Z80 uses the very same FastMemIf /
//     DbgMemIf objects as before overlays existed, on every model and after
//     every way of switching debug mode (the zero-cost promise);
//   - with an overlay, the right one of the four interfaces is selected in
//     every combination, the normal access still happens first, and switching
//     is safe from two threads.

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/memory/memory.h"
#include "emulator/video/ulacontention.h"

namespace
{
/// Records what it sees; replaces reads in its window with `replacement`
struct FakeOverlay : HostBusOverlay
{
    std::vector<uint16_t> reads, writes;
    std::vector<uint8_t> normals, written;
    bool replace = true;
    uint8_t replacement = 0x5A;
    uint32_t waitPerRead = 0;
    Z80* z80 = nullptr;
    bool lastRomPaged = false;

    uint8_t onRead(uint16_t addr, uint8_t normal, bool, bool romPaged) override
    {
        reads.push_back(addr);
        normals.push_back(normal);
        lastRomPaged = romPaged;
        if (waitPerRead && z80)
            z80->AddWaitStates(waitPerRead);
        return replace ? replacement : normal;
    }
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override
    {
        writes.push_back(addr);
        written.push_back(value);
        lastRomPaged = romPaged;
    }
};

class Core_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _ctx = nullptr;
    Core* _core = nullptr;
    Z80* _z80 = nullptr;

    void create(const char* model = "PENTAGON")
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
        _ctx = _emulator->GetContext();
        _core = _ctx->pCore;
        _z80 = _core->GetZ80();
    }

    void TearDown() override
    {
        if (_core)
            _core->SetBusOverlay(nullptr);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void setDebug(bool on)
    {
        _ctx->pFeatureManager->setFeature(Features::kDebugMode, on);
        _ctx->pMemory->UpdateFeatureCache();
    }

    /// Runs `code` from #8000 until the CPU reaches the end of it
    void runProgram(const std::vector<uint8_t>& code)
    {
        for (size_t i = 0; i < code.size(); i++)
            _z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), code[i]);
        _z80->pc = 0x8000;
        const uint16_t end = static_cast<uint16_t>(0x8000 + code.size());
        for (int guard = 0; guard < 1000 && _z80->pc != end; guard++)
            _emulator->RunNCPUCycles(1, true);
        ASSERT_EQ(_z80->pc, end);
    }

    /// The interface without an overlay: Fast / Debug, contended on machines
    /// whose video contention is in effect (48K, 128K, +3) - never an overlay one
    bool isPlain(bool debug) const
    {
        const bool contended = _core->IsContentionEffective();
        if (debug)
            return _z80->MemIf == (contended ? _z80->DbgContendedMemIf : _z80->DbgMemIf);
        return _z80->MemIf == (contended ? _z80->FastContendedMemIf : _z80->FastMemIf);
    }
};
} // namespace

/// region <No overlay: the plain interfaces, on every model>

TEST_F(Core_Test, EveryModelUsesThePlainInterfacesInEveryDebugState)
{
    for (const char* model : {"48K", "128k", "PLUS3", "PENTAGON", "SCORPION", "PROFSCORP", "PROFI", "ATM710", "ATM3"})
    {
        SCOPED_TRACE(model);
        create(model);
        EXPECT_TRUE(isPlain(false)) << "after creation";
        EXPECT_EQ(_core->GetBusOverlay(), nullptr);
        EXPECT_EQ(_ctx->pMemory->GetBusOverlay(), nullptr);
        _emulator->RunNFrames(3);
        EXPECT_TRUE(isPlain(false)) << "after frames";

        setDebug(true);
        EXPECT_TRUE(isPlain(true)) << "debug feature on";
        _emulator->RunNFrames(3);
        EXPECT_TRUE(isPlain(true)) << "frames in debug mode";
        setDebug(false);
        EXPECT_TRUE(isPlain(false)) << "debug feature off";

        _emulator->DebugOn();
        EXPECT_TRUE(isPlain(true)) << "DebugOn";
        _emulator->RunNFrames(2);
        EXPECT_TRUE(isPlain(true));
        _emulator->DebugOff();
        EXPECT_TRUE(isPlain(false)) << "DebugOff";

        // The main loop's frame (Core::CPUFrameCycle) follows Z80::isDebugMode,
        // as it always did. RunNFrames steps instructions itself, so the frame
        // cycle is driven directly here
        _z80->isDebugMode = true;
        _core->CPUFrameCycle();
        EXPECT_TRUE(isPlain(true)) << "isDebugMode set directly";
        _z80->isDebugMode = false;
        _core->CPUFrameCycle();
        EXPECT_TRUE(isPlain(false));

        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
        _core = nullptr;
    }
}

/// endregion

/// region <Overlay selection and switching>

TEST_F(Core_Test, OverlaySelectionFollowsBothInputsInEveryOrder)
{
    create();
    FakeOverlay overlay;
    const auto expect = [&](bool debug, bool installed, const char* step)
    {
        const MemoryInterface* want = installed ? (debug ? _z80->OverlayDbgMemIf : _z80->OverlayFastMemIf)
                                                : (debug ? _z80->DbgMemIf : _z80->FastMemIf);
        EXPECT_EQ(_z80->MemIf, want) << step;
        EXPECT_EQ(_ctx->pMemory->GetBusOverlay(), installed ? &overlay : nullptr) << step;
    };

    expect(false, false, "start");
    ASSERT_TRUE(_core->SetBusOverlay(&overlay));
    expect(false, true, "installed, fast");
    setDebug(true);
    expect(true, true, "installed, debug on");
    _emulator->RunNFrames(2);
    expect(true, true, "frames keep the overlay (debug)");
    setDebug(false);
    expect(false, true, "installed, debug off");
    _emulator->RunNFrames(2);
    expect(false, true, "frames keep the overlay (fast)");
    _emulator->DebugOn();
    expect(true, true, "DebugOn keeps the overlay");
    _core->SetBusOverlay(nullptr);
    expect(true, false, "removed in debug mode");
    _emulator->DebugOff();
    expect(false, false, "debug off again");
    ASSERT_TRUE(_core->SetBusOverlay(&overlay));
    ASSERT_TRUE(_core->SetBusOverlay(&overlay)) << "installing the same overlay again is a no-op";
    expect(false, true, "reinstalled");
}

TEST_F(Core_Test, OnlyOneOverlayAtATime)
{
    create();
    FakeOverlay a, b;
    ASSERT_TRUE(_core->SetBusOverlay(&a));
    EXPECT_FALSE(_core->SetBusOverlay(&b));
    EXPECT_EQ(_core->GetBusOverlay(), &a);
    EXPECT_EQ(_ctx->pMemory->GetBusOverlay(), &a);
    ASSERT_TRUE(_core->SetBusOverlay(nullptr));
    EXPECT_TRUE(_core->SetBusOverlay(&b));
}

TEST_F(Core_Test, SwitchingFromTwoThreadsNeverLeavesAWrongInterface)
{
    create();
    FakeOverlay overlay;
    std::atomic<bool> go{false};
    std::thread ui(
        [&]
        {
            while (!go.load())
                std::this_thread::yield();
            for (int i = 0; i < 10000; i++)
            {
                _z80->isDebugMode = (i & 1) != 0;
                _core->SelectMemoryInterface();
            }
        });
    go.store(true);
    for (int i = 0; i < 10000; i++)
        _core->SetBusOverlay((i & 1) ? nullptr : &overlay);
    ui.join();

    // Last operations: debug = true (i = 9999), overlay removed (i = 9999)
    _core->SelectMemoryInterface(); // both threads done: the final inputs decide
    EXPECT_TRUE(_z80->isDebugMode);
    EXPECT_EQ(_core->GetBusOverlay(), nullptr);
    EXPECT_EQ(_z80->MemIf, _z80->DbgMemIf);
    EXPECT_EQ(_ctx->pMemory->GetBusOverlay(), nullptr);
}

/// The overlay on a machine with video contention: the four overlay
/// interfaces wrap the contended ones, so the ULA's wait still comes first and
/// the overlay then decides the byte
TEST_F(Core_Test, OverlayOnAContendedMachineKeepsTheContention)
{
    create("48K");
    ASSERT_TRUE(_core->IsContentionEffective());
    FakeOverlay overlay;
    overlay.windowStart = 0x4000; // contended slot 1
    overlay.windowEnd = 0x8000;
    overlay.replace = false;

    ASSERT_TRUE(_core->SetBusOverlay(&overlay));
    EXPECT_EQ(_z80->MemIf, _z80->OverlayFastContendedMemIf);
    EXPECT_STREQ(_core->GetMemoryInterfaceName(), "fast_contended_overlay");
    setDebug(true);
    _core->SelectMemoryInterface();
    EXPECT_EQ(_z80->MemIf, _z80->OverlayDbgContendedMemIf);
    setDebug(false);
    _core->SelectMemoryInterface();

    // The same contended reads with and without the overlay take the same time
    // (LD A,(#4000) x 64 while the screen is fetched), and the overlay sees them
    std::vector<uint8_t> code;
    for (int i = 0; i < 64; i++)
        code.insert(code.end(), {0x3A, 0x00, 0x40});
    // A T-state where the ULA holds the CPU back: inside the screen fetch
    UlaContention* ula = _ctx->pUlaContention;
    uint32_t contendedT = 0;
    while (contendedT < 70000 && ula->DelayAt(contendedT) == 0)
        contendedT++;
    ASSERT_LT(contendedT, 70000u);
    const auto timed = [&]
    {
        for (size_t i = 0; i < code.size(); i++)
            _z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), code[i]);
        _z80->pc = 0x8000;
        _z80->t = contendedT;
        for (int i = 0; i < 64; i++)
            _z80->Z80Step();
        return _z80->t - contendedT;
    };
    const uint32_t withOverlay = timed();
    EXPECT_GE(overlay.reads.size(), 64u);
    _core->SetBusOverlay(nullptr);
    EXPECT_EQ(_z80->MemIf, _z80->FastContendedMemIf);
    const uint32_t without = timed();
    EXPECT_EQ(withOverlay, without) << "the contention wait is kept under the overlay";
    EXPECT_GT(without, 64u * 13u) << "and there is a wait: 64 x 13 T uncontended";
}

/// endregion

/// region <What an installed overlay sees>

TEST_F(Core_Test, OverlaySeesItsWindowAfterTheNormalAccessInFastAndDebugMode)
{
    create();
    for (bool debug : {false, true})
    {
        SCOPED_TRACE(debug ? "debug" : "fast");
        setDebug(debug);
        FakeOverlay overlay; // window #0000-#3FFF
        overlay.windowEnd = 0x4000;
        const uint8_t romByte = _z80->DirectRead(0x0000);
        const uint8_t romAt1234 = _z80->DirectRead(0x1234);
        _z80->DirectWrite(0x9000, 0x11);
        ASSERT_TRUE(_core->SetBusOverlay(&overlay));

        runProgram({
            0x3A, 0x00, 0x00, // LD A,(#0000)   in the window: replaced
            0x32, 0x00, 0xA0, // LD (#A000),A
            0x3A, 0x00, 0x90, // LD A,(#9000)   outside: normal
            0x32, 0x01, 0xA0, // LD (#A001),A
            0x3E, 0x77,       // LD A,#77
            0x32, 0x34, 0x12, // LD (#1234),A   ROM write in the window
        });
        _core->SetBusOverlay(nullptr);

        EXPECT_EQ(_z80->DirectRead(0xA000), 0x5A) << "the overlay's byte reached the CPU";
        EXPECT_EQ(_z80->DirectRead(0xA001), 0x11) << "outside the window: untouched";
        ASSERT_EQ(overlay.reads.size(), 1u) << "only #0000 is in the window (the program runs at #8000)";
        EXPECT_EQ(overlay.reads[0], 0x0000);
        EXPECT_EQ(overlay.normals[0], romByte) << "the normal read happened first";
        ASSERT_EQ(overlay.writes.size(), 1u);
        EXPECT_EQ(overlay.writes[0], 0x1234);
        EXPECT_EQ(overlay.written[0], 0x77);
        EXPECT_TRUE(overlay.lastRomPaged);
        EXPECT_EQ(_z80->DirectRead(0x1234), romAt1234) << "ROM unchanged";
    }
}

TEST_F(Core_Test, OverlayWriteToRamStillReachesRam)
{
    create();
    FakeOverlay overlay; // sees the program's own fetches too: pass them through
    overlay.windowStart = 0x8000;
    overlay.windowEnd = 0x10000;
    overlay.replace = false;
    ASSERT_TRUE(_core->SetBusOverlay(&overlay));
    for (bool debug : {false, true})
    {
        setDebug(debug);
        runProgram({0x3E, debug ? uint8_t(0x42) : uint8_t(0x24), 0x32, 0x00, 0xC0}); // LD A,n : LD (#C000),A
        EXPECT_EQ(_z80->DirectRead(0xC000), debug ? 0x42 : 0x24);
        EXPECT_EQ(overlay.writes.back(), 0xC000);
    }
}

TEST_F(Core_Test, OverlayWaitStatesLengthenTheInstruction)
{
    create();
    const std::vector<uint8_t> program = {0x3A, 0x00, 0x00}; // LD A,(#0000): 13 T
    runProgram(program);
    const uint32_t plainT = _z80->t;
    runProgram(program);
    const uint32_t plainDelta = _z80->t - plainT;

    FakeOverlay overlay;
    overlay.windowEnd = 0x4000;
    overlay.waitPerRead = 5;
    overlay.z80 = _z80;
    ASSERT_TRUE(_core->SetBusOverlay(&overlay));
    const uint32_t before = _z80->t;
    runProgram(program);
    EXPECT_EQ(_z80->t - before, plainDelta + 5);
}

/// endregion
