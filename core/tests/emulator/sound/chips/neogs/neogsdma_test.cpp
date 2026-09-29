// NeoGS DMA modules (neogs-tdd.md §3.9, §5.7; FPGA dma/dma_sd.v, dma_mp3.v)

#include <gtest/gtest.h>

#include <deque>
#include <fstream>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/io/flash/flash29f040b.h"
#include "emulator/io/spi/spidevice.h"
#include "emulator/sound/chips/neogs/neogsdma.h"
#include "emulator/sound/chips/neogs/neogsinterrupts.h"
#include "emulator/sound/chips/neogs/neogsmemory.h"
#include "emulator/sound/chips/neogs/vs10xx.h"

namespace
{
constexpr int64_t kUnitsPerCycle = 6; // 20 MHz in 120 MHz base ticks

/// An SD card that answers a scripted byte stream, then #FF
class ScriptedSd : public SpiDevice
{
public:
    std::deque<uint8_t> script;
    int exchanges = 0;
    void select(bool) override {}
    uint8_t exchange(uint8_t mosi) override
    {
        EXPECT_EQ(mosi, 0xFF) << "SD DMA drives MOSI high";
        exchanges++;
        if (script.empty())
            return 0xFF;
        const uint8_t b = script.front();
        script.pop_front();
        return b;
    }
};

struct Host : NeoGSDma::Host
{
    int64_t stalled = 0;
    uint8_t lastSd = 0;
    void dmaStall(int64_t units) override { stalled += units; }
    int64_t dmaUnitsPerCycle() const override { return kUnitsPerCycle; }
    void dmaSdByteDone(uint8_t received) override { lastSd = received; }
};

struct Fixture
{
    Flash29F040B flash{120e6};
    NeoGSMemory mem{4096, &flash};
    NeoGSInterrupts irq;
    Host host;
    NeoGSDma dma{host, mem, irq};
    ScriptedSd sd;

    Fixture()
    {
        irq.writeEnable(0x87);
        dma.attachSd(&sd);
    }

    void setAddress(uint8_t module, uint32_t address)
    {
        dma.writeModuleSelect(module);
        dma.writeRegister(0, static_cast<uint8_t>(address >> 16), 0);
        dma.writeRegister(1, static_cast<uint8_t>(address >> 8), 0);
        dma.writeRegister(2, static_cast<uint8_t>(address), 0);
    }

    /// Run all events up to `until`
    void runTo(int64_t until)
    {
        while (dma.nextEvent() <= until)
            dma.run(dma.nextEvent());
    }
};

std::deque<uint8_t> tokenBlock(int leadingFF, uint8_t token, uint8_t fill)
{
    std::deque<uint8_t> s(static_cast<size_t>(leadingFF), 0xFF);
    s.push_back(token);
    for (int i = 0; i < 512; i++)
        s.push_back(static_cast<uint8_t>(fill + i));
    s.push_back(0x12); // CRC, not checked
    s.push_back(0x34);
    return s;
}
} // namespace

TEST(NeoGSDma, RegistersReadBackWithHadSixBitsAndCstBit7)
{
    Fixture f;
    f.dma.writeModuleSelect(2);
    f.dma.writeRegister(0, 0xFF, 0);
    f.dma.writeRegister(1, 0x12, 0);
    f.dma.writeRegister(2, 0x34, 0);
    EXPECT_EQ(f.dma.readRegister(0), 0xFF) << "6 bits stored, 7:6 read as 1";
    EXPECT_EQ(f.dma.readRegister(3), 0x7F) << "not running";
    EXPECT_EQ(f.dma.address(NeoGSDma::SD), 0x1F1234u) << "HAD bit 5 has no effect: 21 address bits";
    f.dma.writeModuleSelect(5);
    EXPECT_EQ(f.dma.readRegister(1), 0xFF) << "no module selected";
}

TEST(NeoGSDma, SdBlockToRamWithTokenWaitStallAndInterrupt)
{
    Fixture f;
    f.sd.script = tokenBlock(8, 0xFE, 0x40);
    f.setAddress(2, 0x012000);
    f.dma.writeModuleSelect(2);
    f.dma.writeRegister(3, 0x80, 1000);
    EXPECT_TRUE(f.dma.running(NeoGSDma::SD));
    EXPECT_EQ(f.dma.readRegister(3) & 0x80, 0x80);

    f.runTo(1'000'000);
    EXPECT_FALSE(f.dma.running(NeoGSDma::SD));
    EXPECT_EQ(f.irq.readRequest() & NeoGSInterrupts::REQ_SD_DMA, NeoGSInterrupts::REQ_SD_DMA);
    for (int i = 0; i < 512; i++)
        ASSERT_EQ(f.mem.ram()[0x012000 + i], static_cast<uint8_t>(0x40 + i)) << i;
    EXPECT_EQ(f.dma.address(NeoGSDma::SD), 0x012200u) << "the address moved past the block";
    EXPECT_EQ(f.host.stalled, (512 * 2 + 4) * kUnitsPerCycle) << "2 clocks a byte plus the grant";
    EXPECT_EQ(f.sd.exchanges, 8 + 1 + 512 + 2);
}

TEST(NeoGSDma, SdErrorTokenStopsWithoutWritingButStillInterrupts)
{
    Fixture f;
    f.sd.script = {0xFF, 0xFF, 0x05};
    f.setAddress(2, 0x000100);
    f.mem.ram()[0x100] = 0xAA;
    f.dma.writeRegister(3, 0x80, 0);
    f.runTo(1'000'000);
    EXPECT_FALSE(f.dma.running(NeoGSDma::SD));
    EXPECT_NE(f.irq.readRequest() & NeoGSInterrupts::REQ_SD_DMA, 0);
    EXPECT_EQ(f.mem.ram()[0x100], 0xAA);
    EXPECT_EQ(f.dma.address(NeoGSDma::SD), 0x000100u) << "the only sign of the error: no advance";
    EXPECT_EQ(f.host.stalled, 0);
}

TEST(NeoGSDma, SdWaitsForeverAndClearingCstAborts)
{
    Fixture f; // the script is empty: #FF forever
    f.dma.writeModuleSelect(2);
    f.dma.writeRegister(3, 0x80, 0);
    f.runTo(10'000'000);
    EXPECT_TRUE(f.dma.running(NeoGSDma::SD)) << "no timeout";
    f.dma.writeRegister(3, 0x00, 10'000'001);
    EXPECT_FALSE(f.dma.running(NeoGSDma::SD));
    EXPECT_EQ(f.irq.readRequest() & NeoGSInterrupts::REQ_SD_DMA, 0) << "an abort raises nothing";
    EXPECT_EQ(f.dma.nextEvent(), NeoGSDma::kNever);
}

TEST(NeoGSDma, SdByteTimingIs18Clocks)
{
    Fixture f;
    f.sd.script = tokenBlock(0, 0xFE, 0);
    f.dma.writeModuleSelect(2);
    f.dma.writeRegister(3, 0x80, 0);
    f.dma.run(0); // token byte and the data are pulled at once
    // The burst starts while the second CRC byte clocks: token + 512 data + 1 CRC
    EXPECT_EQ(f.dma.nextEvent(), static_cast<int64_t>(1 + 512 + 1) * 18 * kUnitsPerCycle);
}

TEST(NeoGSDma, Mp3ModuleBurstsThenFeedsTheDecoderWhileDreq)
{
    Fixture f;
    Vs10xxDecoder mp3(Vs10xxDecoder::Chip::VS1001, Vs10xxDecoder::Level::Software, 120e6);
    mp3.setReset(true, 0);
    f.dma.attachMp3(&mp3);
    for (int i = 0; i < 512; i++)
        f.mem.ram()[0x3000 + i] = 0x00; // junk: the decoder keeps it, the FIFO fills
    f.setAddress(3, 0x3000);
    const int64_t start = 1'000'000; // past the decoder's reset latency
    f.dma.writeRegister(3, 0x80, start);
    EXPECT_EQ(f.host.stalled, (512 * 2 + 4) * kUnitsPerCycle) << "the burst from RAM";
    EXPECT_EQ(f.dma.address(NeoGSDma::MP3), 0x3200u);
    f.runTo(start + 100'000'000);
    EXPECT_FALSE(f.dma.running(NeoGSDma::MP3));
    EXPECT_EQ(mp3.bytesReceived(), 512u);
    EXPECT_NE(f.irq.readRequest() & NeoGSInterrupts::REQ_MP3_DMA, 0);
}

TEST(NeoGSDma, Mp3ModuleIsPacedByDreq)
{
    // A 128 kbit/s stream drains the decoder's FIFO at 16 bytes per ms: with
    // the FIFO full, the module can only send as fast as DREQ allows
    const auto path = TestPathHelper::FindProjectRoot() / "testdata/sound/neogs/mp3/eyeache1-44k-128k-cbr.mp3";
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> stream((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_GT(stream.size(), 10000u);

    Fixture f;
    Vs10xxDecoder mp3(Vs10xxDecoder::Chip::VS1001, Vs10xxDecoder::Level::Software, 120e6);
    mp3.setReset(true, 0);
    f.dma.attachMp3(&mp3);
    const int64_t t0 = 1'000'000; // past the reset latency
    size_t at = 0;
    while (mp3.dreq(t0))
        mp3.sdi()->exchange(stream[at++]);
    for (int i = 0; i < 512; i++)
        f.mem.ram()[0x3000 + i] = stream[at + static_cast<size_t>(i)];
    const uint64_t before = mp3.bytesReceived();

    f.setAddress(3, 0x3000);
    f.dma.writeRegister(3, 0x80, t0);
    f.runTo(t0 + 5 * 120'000); // 5 ms
    EXPECT_TRUE(f.dma.running(NeoGSDma::MP3)) << "DREQ holds it back";
    EXPECT_LT(mp3.bytesReceived() - before, 512u);
    f.runTo(t0 + 100 * 120'000); // 100 ms
    EXPECT_FALSE(f.dma.running(NeoGSDma::MP3));
    EXPECT_EQ(mp3.bytesReceived() - before, 512u);
}

TEST(NeoGSDma, ResetClearsOnlyTheRunBits)
{
    Fixture f;
    f.setAddress(2, 0x123456);
    f.dma.writeRegister(3, 0x80, 0);
    f.dma.reset();
    EXPECT_FALSE(f.dma.running(NeoGSDma::SD));
    EXPECT_EQ(f.dma.address(NeoGSDma::SD), 0x123456u & 0x1FFFFFu);
}
