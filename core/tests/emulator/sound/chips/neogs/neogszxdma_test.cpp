// NeoGS ZX-DMA model with a fake host (neogs-zxdma-design.md §2.3, §5.4-§5.6):
// the host's time, speed and the card's catch-up are under the test's
// control, so the mode rules, the one-byte read lag, the pending byte and
// the /WAIT arithmetic are checked exactly. The same behaviour on an emulated
// Pentagon is in soundchip_neogs_zxdma_test.cpp.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "emulator/io/flash/flash29f040b.h"
#include "emulator/sound/chips/neogs/neogsdma.h"
#include "emulator/sound/chips/neogs/neogsinterrupts.h"
#include "emulator/sound/chips/neogs/neogsmemory.h"
#include "emulator/sound/chips/neogs/neogszxdma.h"

namespace
{
constexpr int64_t kUnitsPerCycle = 10; // 12 MHz card in 120 MHz ticks

struct DmaHost : NeoGSDma::Host
{
    void dmaStall(int64_t) override {}
    int64_t dmaUnitsPerCycle() const override { return kUnitsPerCycle; }
    void dmaSdByteDone(uint8_t) override {}
};

struct ZxHost : NeoGSZxDma::Host
{
    NeoGSZxDma* zx = nullptr;
    int64_t hostNow = 0;          // host time in card units
    double unitsPerT = 34.2857;   // 3.5 MHz host
    int64_t stallUntil = 0;
    uint32_t frame = 100;
    std::vector<uint32_t> waits;
    std::vector<bool> installs;
    int64_t stalled = 0;
    std::vector<int64_t> late;

    // The card "runs" to the host's time: due bytes complete
    void zxCatchUp() override { zx->run(hostNow); }
    bool zxHostNowUnits(int64_t& now) const override
    {
        now = hostNow;
        return true;
    }
    double zxUnitsPerHostT() const override { return unitsPerT; }
    void zxAddHostWait(uint32_t t) override
    {
        waits.push_back(t);
        hostNow += std::llround(t * unitsPerT);
    }
    int64_t zxUnitsPerCycle() const override { return kUnitsPerCycle; }
    void zxStall(int64_t units) override { stalled += units; }
    int64_t zxStallUntil() const override { return stallUntil; }
    bool zxInstall(bool installed) override
    {
        installs.push_back(installed);
        return true;
    }
    uint32_t zxFrame() const override { return frame; }
    void zxReschedule() override {}
    void zxLateStart(int64_t units) override { late.push_back(units); }
    void zxTrace(bool, uint32_t, uint8_t) override {}
};

struct Fixture
{
    Flash29F040B flash{120e6};
    NeoGSMemory mem{2048, &flash};
    NeoGSInterrupts irq;
    DmaHost dmaHost;
    NeoGSDma dma{dmaHost, mem, irq};
    ZxHost host;
    NeoGSZxDma zx{host, dma, mem};

    Fixture() { host.zx = &zx; }

    void select(uint8_t module)
    {
        dma.writeModuleSelect(module);
        zx.onModuleSelectWritten();
    }
    void setAddress(uint32_t address)
    {
        dma.writeRegister(0, static_cast<uint8_t>(address >> 16), 0);
        dma.writeRegister(1, static_cast<uint8_t>(address >> 8), 0);
        dma.writeRegister(2, static_cast<uint8_t>(address), 0);
    }
    void control(uint8_t value)
    {
        const bool was = dma.running(NeoGSDma::ZX);
        dma.writeRegister(3, value, host.hostNow);
        zx.onControlWritten(was, host.hostNow);
    }
    /// A host read `gapT` T-states after the previous one (the call comes at the access end)
    uint8_t read(double gapT, bool romPaged = true, uint8_t normal = 0xC3)
    {
        host.hostNow += std::llround(gapT * host.unitsPerT);
        return zx.onRead(0x0000, normal, false, romPaged);
    }
    void write(double gapT, uint8_t value)
    {
        host.hostNow += std::llround(gapT * host.unitsPerT);
        zx.onWrite(0x0000, value, true);
    }
    void startAt(uint32_t address)
    {
        select(1);
        setAddress(address);
        control(0x80);
    }
};
} // namespace

/// region <Mode and watch window>

TEST(NeoGSZxDma, ModeFollowsSelectionRunAndTheWatchWindow)
{
    Fixture f;
    f.zx.setWatchFrames(5);
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Off);
    EXPECT_FALSE(f.zx.installed());

    f.select(2); // SD module: nothing to watch
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Off);

    f.select(1);
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Watch);
    EXPECT_TRUE(f.zx.installed());
    EXPECT_EQ(f.zx.watchFramesLeft(), 5);

    f.control(0x80);
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Divert);
    f.select(2);
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Divert) << "CST belongs to the module, not to the selection";
    f.select(1);

    f.host.frame += 3;
    f.control(0x00); // transfer ends: the window is renewed
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Watch);
    EXPECT_EQ(f.zx.watchFramesLeft(), 5);

    f.host.frame += 4;
    f.zx.onFrameEnd();
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Watch) << "one frame left";
    f.zx.onHostPortAccess(); // a port access with the module selected renews it
    f.host.frame += 4;
    f.zx.onFrameEnd();
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Watch);
    f.host.frame += 1;
    f.zx.onFrameEnd();
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Off) << "5 quiet frames";
    EXPECT_FALSE(f.zx.installed());
    EXPECT_EQ(f.zx.watchFramesLeft(), -1);

    f.zx.onHostPortAccess();
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Watch) << "re-armed by a port access, module still selected";
    f.select(3);
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Off) << "another module selected";

    EXPECT_EQ(f.host.installs, (std::vector<bool>{true, false, true, false})) << "install only on a change";
}

TEST(NeoGSZxDma, AlwaysWatchesAndUnavailableNeverInstalls)
{
    Fixture f;
    f.zx.setWatchAlways(true);
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Watch);
    f.host.frame += 1000;
    f.zx.onFrameEnd();
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Watch);

    Fixture d;
    d.zx.setAvailable(false); // Fpga=D
    d.startAt(0x1000);
    EXPECT_EQ(d.zx.mode(), NeoGSZxDma::Mode::Off);
    EXPECT_TRUE(d.host.installs.empty());
}

TEST(NeoGSZxDma, StartWhileNotWatchedIsCountedLate)
{
    Fixture f;
    f.zx.setWatchFrames(1);
    f.select(1);
    f.host.frame += 2;
    f.zx.onFrameEnd();
    ASSERT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Off);

    // The card starts at card time 1000 while the host is already at 5000
    const bool was = f.dma.running(NeoGSDma::ZX);
    f.host.hostNow = 5000;
    f.dma.writeRegister(3, 0x80, 1000);
    f.zx.onControlWritten(was, 1000);
    EXPECT_EQ(f.zx.lateStarts(), 1u);
    EXPECT_EQ(f.zx.lateStartUnits(), 4000u);
    ASSERT_EQ(f.host.late.size(), 1u);

    Fixture w; // watched: never late
    w.select(1);
    w.host.hostNow = 5000;
    const bool wasW = w.dma.running(NeoGSDma::ZX);
    w.dma.writeRegister(3, 0x80, 1000);
    w.zx.onControlWritten(wasW, 1000);
    EXPECT_EQ(w.zx.lateStarts(), 0u);
}

TEST(NeoGSZxDma, WindowIsTheHostRomArea)
{
    Fixture f;
    EXPECT_EQ(f.zx.windowStart, 0x0000);
    EXPECT_EQ(f.zx.windowEnd, 0x4000u) << "Memory calls the overlay only inside its window";
}

/// endregion

/// region <Bytes>

TEST(NeoGSZxDma, ReadsLagByOneByteAndMoveTheAddress)
{
    Fixture f;
    for (int i = 0; i < 16; i++)
        f.mem.ram()[0x012000 + i] = static_cast<uint8_t>(0xA0 + i);
    f.startAt(0x012000);

    EXPECT_EQ(f.read(21), 0xFF) << "the first read returns the old latch";
    for (int i = 0; i < 8; i++)
        EXPECT_EQ(f.read(21), 0xA0 + i) << i;
    f.host.hostNow += 1000;
    f.zx.run(f.host.hostNow);
    EXPECT_EQ(f.dma.address(NeoGSDma::ZX), 0x012009u) << "9 fetches";
    EXPECT_EQ(f.zx.bytesRead(), 9u);
    EXPECT_TRUE(f.host.waits.empty()) << "21 T apart at 3.5 MHz: no waits";
    EXPECT_EQ(f.host.stalled, 9 * NeoGSZxDma::CARD_STALL_CLOCKS * kUnitsPerCycle);
}

TEST(NeoGSZxDma, ReadsWithRamPagedMakeNoDmaCycle)
{
    Fixture f;
    f.startAt(0x2000);
    EXPECT_EQ(f.read(21, false, 0x77), 0x77) << "host RAM";
    EXPECT_EQ(f.dma.address(NeoGSDma::ZX), 0x2000u);
    EXPECT_EQ(f.zx.bytesRead(), 0u);
}

TEST(NeoGSZxDma, WritesLandInCardRamAtTheGrant)
{
    Fixture f;
    f.startAt(0x030000);
    for (int i = 0; i < 4; i++)
        f.write(21, static_cast<uint8_t>(0x10 + i));
    EXPECT_EQ(f.zx.pending(), NeoGSZxDma::Pending::Write) << "the last one waits for its grant";
    f.host.hostNow += NeoGSZxDma::WRITE_GRANT_CLOCKS * kUnitsPerCycle;
    f.zx.run(f.host.hostNow);
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(f.mem.ram()[0x030000 + i], 0x10 + i);
    EXPECT_EQ(f.dma.address(NeoGSDma::ZX), 0x030004u);
}

TEST(NeoGSZxDma, BackToBackReadsOnAFastHostWaitExactly)
{
    // Host at 14 MHz (8.57 units per T), card at 12 MHz: a fetch takes
    // 8 clocks = 80 units = 9.33 T. LD HL,(#0000): reads 3 T apart
    Fixture f;
    f.host.unitsPerT = 120e6 / 14e6;
    f.startAt(0x100);
    f.read(21);
    f.read(3);
    // The first fetch began 3 T before its call and ends 80 units later; the
    // second read samples /WAIT 1 T into its cycle (2 T before its call)
    const double upt = f.host.unitsPerT;
    const int64_t firstStart = std::llround(21 * upt) - std::llround(3 * upt);
    const int64_t sample = std::llround(24 * upt) - std::llround(2 * upt);
    const uint32_t expected = static_cast<uint32_t>(std::ceil((firstStart + 80 - sample) / upt));
    ASSERT_EQ(f.host.waits.size(), 1u);
    EXPECT_EQ(f.host.waits[0], expected);
    EXPECT_EQ(expected, 6u) << "the worked example of neogs-zxdma-design.md §5.8";
    EXPECT_EQ(f.zx.waitTStates(), 6u);
}

TEST(NeoGSZxDma, MixedDirectionsDropThePendingByte)
{
    Fixture f;
    f.host.unitsPerT = 120e6 / 14e6; // fast host: the byte is still pending
    f.startAt(0x400);
    f.write(21, 0x55);
    const uint8_t latchBefore = f.zx.readLatch();
    f.read(3); // read while the write waits for its grant
    EXPECT_EQ(f.zx.bytesDropped(), 1u);
    EXPECT_TRUE(f.host.waits.empty());
    f.host.hostNow += 1000;
    f.zx.run(f.host.hostNow);
    EXPECT_NE(f.mem.ram()[0x400], 0x55) << "the write was lost";
    EXPECT_EQ(f.dma.address(NeoGSDma::ZX), 0x401u) << "only the read was granted";
    (void)latchBefore;
}

TEST(NeoGSZxDma, ClearingCstDiscardsThePendingByteAndDiverts)
{
    Fixture f;
    f.host.unitsPerT = 120e6 / 14e6;
    f.startAt(0x800);
    f.read(21);
    ASSERT_EQ(f.zx.pending(), NeoGSZxDma::Pending::Read);
    f.control(0x00);
    EXPECT_EQ(f.zx.pending(), NeoGSZxDma::Pending::None);
    EXPECT_EQ(f.zx.mode(), NeoGSZxDma::Mode::Watch);
    EXPECT_EQ(f.read(3, true, 0xC3), 0xC3) << "ROM again, no wait";
    EXPECT_TRUE(f.host.waits.empty());
    EXPECT_EQ(f.dma.address(NeoGSDma::ZX), 0x800u) << "the discarded fetch never moved it";
}

TEST(NeoGSZxDma, ARunningBurstDelaysTheByteBySlot)
{
    Fixture f;
    f.startAt(0x10);
    f.host.stallUntil = 1'000'000; // an SD burst in progress
    f.read(21);
    const int64_t start = std::llround(21 * f.host.unitsPerT) - std::llround(3 * f.host.unitsPerT);
    // Completion moves by one burst slot
    f.host.hostNow = start + (NeoGSZxDma::READ_DONE_CLOCKS + NeoGSZxDma::BURST_SLOT_CLOCKS) * kUnitsPerCycle - 1;
    f.zx.run(f.host.hostNow);
    EXPECT_EQ(f.zx.pending(), NeoGSZxDma::Pending::Read);
    f.zx.run(f.host.hostNow + 1);
    EXPECT_EQ(f.zx.pending(), NeoGSZxDma::Pending::None);
}

/// endregion

/// region <TTD state>

TEST(NeoGSZxDma, StateSavedMidTransferContinuesIdentically)
{
    // A fast host with a byte in flight: the latch, the pending byte, the
    // watch window and the counters all travel in the snapshot
    Fixture a;
    a.host.unitsPerT = 120e6 / 14e6;
    for (int i = 0; i < 64; i++)
        a.mem.ram()[0x5000 + i] = static_cast<uint8_t>(0x30 + i);
    a.startAt(0x5000);
    a.read(21);
    a.read(3);
    a.write(21, 0x99); // the read before it is done by now; this write is pending
    ASSERT_NE(a.zx.pending(), NeoGSZxDma::Pending::None);

    const size_t waitsBeforeSave = a.host.waits.size();
    std::vector<uint8_t> dmaState(NeoGSDma::STATE_SIZE), zxState(NeoGSZxDma::STATE_SIZE);
    a.dma.saveState(dmaState.data());
    a.zx.saveState(zxState.data());

    Fixture b;
    b.host.unitsPerT = a.host.unitsPerT;
    b.host.hostNow = a.host.hostNow;
    b.host.frame = a.host.frame;
    memcpy(b.mem.ram(), a.mem.ram(), b.mem.ramSize());
    b.dma.loadState(dmaState.data());
    b.zx.loadState(zxState.data());
    EXPECT_EQ(b.zx.mode(), NeoGSZxDma::Mode::Divert) << "the mode follows the restored registers";
    EXPECT_TRUE(b.zx.installed()) << "loading installs the overlay";
    std::vector<uint8_t> again(NeoGSZxDma::STATE_SIZE);
    b.zx.saveState(again.data());
    EXPECT_EQ(again, zxState);

    for (Fixture* f : {&a, &b})
    {
        f->write(3, 0x77); // waits for the pending write's grant
        for (int i = 0; i < 6; i++)
            f->read(i == 0 ? 21 : 4);
        f->host.hostNow += 10000;
        f->zx.run(f->host.hostNow);
    }
    const std::vector<uint32_t> aWaitsAfter(a.host.waits.begin() + static_cast<std::ptrdiff_t>(waitsBeforeSave), a.host.waits.end());
    EXPECT_EQ(b.host.waits, aWaitsAfter);
    EXPECT_FALSE(aWaitsAfter.empty()) << "the continuation must include waits";
    EXPECT_EQ(b.zx.readLatch(), a.zx.readLatch());
    EXPECT_EQ(b.zx.bytesRead(), a.zx.bytesRead());
    EXPECT_EQ(b.zx.bytesWritten(), a.zx.bytesWritten());
    EXPECT_EQ(b.zx.waitTStates(), a.zx.waitTStates());
    EXPECT_EQ(b.dma.address(NeoGSDma::ZX), a.dma.address(NeoGSDma::ZX));
    EXPECT_EQ(0, memcmp(b.mem.ram() + 0x5000, a.mem.ram() + 0x5000, 64));
}

/// endregion
