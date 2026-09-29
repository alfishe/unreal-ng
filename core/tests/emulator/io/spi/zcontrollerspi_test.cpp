// Z-Controller SD interface (tdd-storage-sd-ide-cd.md §2.2, ZC-2 and ZC-5)

#include <gtest/gtest.h>

#include <deque>
#include <vector>

#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/io/spi/spidevice.h"
#include "emulator/io/spi/zcontrollerspi.h"
#include "emulator/io/storage/memorydisk.h"

namespace
{
    /// Answers a scripted byte per exchange and records what it was sent
    class ScriptedDevice : public SpiDevice
    {
    public:
        std::deque<uint8_t> replies;
        std::vector<uint8_t> sent;
        std::vector<bool> selects;

        void select(bool selected) override { selects.push_back(selected); }
        uint8_t exchange(uint8_t mosi) override
        {
            sent.push_back(mosi);
            if (replies.empty())
                return 0xFF;
            const uint8_t reply = replies.front();
            replies.pop_front();
            return reply;
        }
    };
}  // namespace

/// ZC-2: a read returns the byte of the PREVIOUS exchange and clocks #FF
TEST(ZControllerSpi_Test, ReadReturnsPreviousExchangeAndSendsFF)
{
    ScriptedDevice device;
    ZControllerSpi zc;
    zc.SetDevice(&device);
    zc.Reset();

    device.replies = {0x11, 0x22, 0x33};
    zc.WriteData(0x40);                   // exchange 1: card answers #11
    EXPECT_EQ(zc.ReadData(), 0x11);       // returns #11, exchange 2 sends #FF, card answers #22
    EXPECT_EQ(zc.ReadData(), 0x22);       // exchange 3 answers #33
    EXPECT_EQ(zc.GetState().rxLatch, 0x33);
    EXPECT_EQ(device.sent, (std::vector<uint8_t>{0x40, 0xFF, 0xFF}));
}

TEST(ZControllerSpi_Test, ChipSelectIsBit1ActiveLowAndResetDeselects)
{
    ScriptedDevice device;
    ZControllerSpi zc;
    zc.SetDevice(&device);
    zc.Reset();
    EXPECT_FALSE(zc.IsSelected()) << "spihub.v: /CS resets high";
    device.selects.clear();

    zc.WriteConfig(0x01);  // D1 = 0: select (D0 is not the chip select)
    EXPECT_TRUE(zc.IsSelected());
    zc.WriteConfig(0x00);  // no change: the card is not told again
    zc.WriteConfig(0x03);  // D1 = 1: deselect
    EXPECT_FALSE(zc.IsSelected());
    EXPECT_EQ(device.selects, (std::vector<bool>{true, false}));

    zc.WriteConfig(0x00);
    zc.Reset();
    EXPECT_FALSE(zc.IsSelected());
    EXPECT_EQ(zc.GetState().rxLatch, 0xFF);
}

TEST(ZControllerSpi_Test, EmptySlotReadsFF)
{
    ZControllerSpi zc;
    zc.WriteConfig(0x00);
    zc.WriteData(0x40);
    EXPECT_EQ(zc.ReadData(), 0xFF);
    EXPECT_EQ(zc.ReadData(), 0xFF);
}

/// ZC-5: controller + card state saved in the middle of a multi-block read
/// continue byte for byte on a second machine
TEST(ZControllerSpi_Test, StateRoundTripMidCommand)
{
    auto makeCard = [](SdCardSpi& card) {
        auto disk = std::make_unique<MemoryDisk>(64);
        for (size_t i = 0; i < 64 * 512; i++)
            disk->Data()[i] = static_cast<uint8_t>(i * 13 + 7);
        ASSERT_TRUE(card.insert(std::move(disk), SdCardSpi::WriteMode::Session));
    };
    SdCardSpi cardA;
    SdCardSpi cardB;
    makeCard(cardA);
    makeCard(cardB);
    ZControllerSpi a;
    ZControllerSpi b;
    a.SetDevice(&cardA);
    b.SetDevice(&cardB);
    a.Reset();
    b.Reset();

    auto command = [&a](uint8_t index, uint32_t arg, uint8_t crc) {
        a.WriteData(static_cast<uint8_t>(0x40 | index));
        for (int shift = 24; shift >= 0; shift -= 8)
            a.WriteData(static_cast<uint8_t>(arg >> shift));
        a.WriteData(crc);
        for (int i = 0; i < 16; i++)
        {
            a.ReadData();
            if (a.GetState().rxLatch != 0xFF)
                return a.ReadData();
        }
        return uint8_t{0xFF};
    };

    a.WriteConfig(0x00);  // select
    ASSERT_EQ(command(0, 0, 0x95), 0x01);
    for (int tries = 0; tries < 20; tries++)
    {
        command(55, 0, 0xFF);
        if (command(41, 0x40000000, 0xFF) == 0x00)
            break;
    }
    ASSERT_TRUE(cardA.initialized());
    ASSERT_EQ(command(18, 3 * 512, 0xFF), 0x00);
    for (int i = 0; i < 200; i++)
        a.ReadData();  // into the first block

    std::vector<uint8_t> cardState(SdCardSpi::STATE_SIZE);
    cardA.saveState(cardState.data());
    cardB.loadState(cardState.data());
    b.SetState(a.GetState());
    EXPECT_TRUE(b.IsSelected());

    for (int i = 0; i < 1500; i++)
        ASSERT_EQ(b.ReadData(), a.ReadData()) << "byte " << i;
}
