// NeoGS SPI masters (neogs-tdd.md §3.7; FPGA common/spi.v, ports.v:577-623)

#include <gtest/gtest.h>

#include <vector>

#include "emulator/io/spi/spidevice.h"
#include "emulator/sound/chips/neogs/neogsspi.h"

namespace
{
class EchoDevice : public SpiDevice
{
public:
    std::vector<uint8_t> received;
    int truncated = 0;
    bool selected = false;
    uint8_t reply = 0x10;

    void select(bool s) override { selected = s; }
    uint8_t exchange(uint8_t mosi) override
    {
        received.push_back(mosi);
        return reply++;
    }
    void truncatedByte() override { truncated++; }
};

constexpr int64_t kTicks = 6; // 20 MHz card clock
} // namespace

TEST(NeoGSSpi, ResetValueAndWriteRule)
{
    NeoGSSpi spi;
    EXPECT_EQ(spi.readSctrl(), 0x0B);
    spi.writeSctrl(0x80 | NeoGSSpi::SCTRL_MC_XRESET | NeoGSSpi::SCTRL_MDHLF, 0);
    EXPECT_EQ(spi.readSctrl(), 0x1F);
    spi.writeSctrl(NeoGSSpi::SCTRL_SD_NCS | NeoGSSpi::SCTRL_MC_NCS, 0); // d7 = 0 clears
    EXPECT_EQ(spi.readSctrl(), 0x1C);
}

TEST(NeoGSSpi, ChipSelectFollowsSctrl)
{
    NeoGSSpi spi;
    EchoDevice sd;
    spi.attach(NeoGSSpi::SD, &sd);
    spi.writeSctrl(NeoGSSpi::SCTRL_SD_NCS, 0);
    EXPECT_TRUE(sd.selected);
    spi.writeSctrl(0x80 | NeoGSSpi::SCTRL_SD_NCS, 0);
    EXPECT_FALSE(sd.selected);
}

TEST(NeoGSSpi, ByteTimesPerDivider)
{
    NeoGSSpi spi;
    EXPECT_EQ(spi.byteClocks(NeoGSSpi::SD), 16);
    EXPECT_EQ(spi.byteClocks(NeoGSSpi::MC), 34) << "reset MCSPD = 01: /4";
    EXPECT_EQ(spi.byteClocks(NeoGSSpi::MD), 16);
    spi.writeSctrl(0x80 | NeoGSSpi::SCTRL_MCSPD1, 0);
    EXPECT_EQ(spi.byteClocks(NeoGSSpi::MC), 130) << "MCSPD = 11: /16";
    spi.writeSctrl(0x80 | NeoGSSpi::SCTRL_MDHLF, 0);
    EXPECT_EQ(spi.byteClocks(NeoGSSpi::MD), 34);
}

TEST(NeoGSSpi, EarlyReadReturnsPreviousByteBoundaryIsInclusive)
{
    NeoGSSpi spi;
    EchoDevice sd;
    spi.attach(NeoGSSpi::SD, &sd);
    spi.start(NeoGSSpi::SD, 0x40, 0, kTicks);
    spi.sync(15 * kTicks);
    EXPECT_EQ(spi.received(NeoGSSpi::SD), 0xFF) << "one clock early: still the old byte";
    EXPECT_TRUE(sd.received.empty());
    spi.sync(16 * kTicks);
    EXPECT_EQ(spi.received(NeoGSSpi::SD), 0x10) << "exactly 16 clocks later: done";
    ASSERT_EQ(sd.received.size(), 1u);
    EXPECT_EQ(sd.received[0], 0x40);
}

TEST(NeoGSSpi, LoaderSixteenClockPairsSeeEveryByte)
{
    // OUT (C),A / NOP / OUT (C),A: starts exactly 16 clocks apart
    NeoGSSpi spi;
    EchoDevice sd;
    spi.attach(NeoGSSpi::SD, &sd);
    for (int i = 0; i < 6; i++)
        spi.start(NeoGSSpi::SD, static_cast<uint8_t>(i), i * 16 * kTicks, kTicks);
    spi.sync(6 * 16 * kTicks);
    EXPECT_EQ(sd.received.size(), 6u);
    EXPECT_EQ(sd.truncated, 0);
}

TEST(NeoGSSpi, StartDuringExchangeRestarts)
{
    NeoGSSpi spi;
    EchoDevice sd;
    spi.attach(NeoGSSpi::SD, &sd);
    spi.start(NeoGSSpi::SD, 0x01, 0, kTicks);
    spi.start(NeoGSSpi::SD, 0x02, 10 * kTicks, kTicks); // mid-byte
    EXPECT_EQ(sd.truncated, 1);
    spi.sync(25 * kTicks);
    EXPECT_TRUE(sd.received.empty()) << "the restarted byte completes 16 clocks after the restart";
    spi.sync(26 * kTicks);
    ASSERT_EQ(sd.received.size(), 1u);
    EXPECT_EQ(sd.received[0], 0x02);
}

TEST(NeoGSSpi, AbsentDeviceReadsFF)
{
    NeoGSSpi spi;
    spi.start(NeoGSSpi::SD, 0x40, 0, kTicks);
    spi.sync(1000);
    EXPECT_EQ(spi.received(NeoGSSpi::SD), 0xFF);
}
