// IDE channel bus rules (IDE design §6.2): master and slave, absent units,
// the floating bus, writes latched by both units, commands to the selected one

#include <gtest/gtest.h>

#include <memory>

#include "emulator/io/ide/ata/atachannel.h"
#include "emulator/io/ide/ata/atadisk.h"
#include "emulator/io/storage/memorydisk.h"

using namespace ata;

namespace
{
    struct Unit
    {
        std::unique_ptr<MemoryDisk> medium;
        AtaDisk* disk = nullptr;
    };

    Unit AddDisk(AtaChannel& channel, int unit, uint8_t fill, bool attach = true)
    {
        Unit result;
        result.medium = std::make_unique<MemoryDisk>(64);
        for (size_t i = 0; i < 64 * 512; i++)
            result.medium->Data()[i] = fill;
        auto disk = std::make_unique<AtaDisk>();
        result.disk = disk.get();
        channel.SetUnit(unit, std::move(disk));
        if (attach)
            result.disk->AttachMedium(*result.medium, {});
        return result;
    }

    uint16_t ReadFirstWord(AtaChannel& channel, uint8_t device)
    {
        channel.WriteRegister(DeviceHead, static_cast<uint8_t>(device | DeviceBits::LBA));
        channel.WriteRegister(SectorCount, 1);
        channel.WriteRegister(SectorNumber, 0);
        channel.WriteRegister(CylinderLow, 0);
        channel.WriteRegister(CylinderHigh, 0);
        channel.WriteRegister(StatusCommand, Command::ReadSectors);
        return channel.ReadData();
    }
}  // namespace

TEST(AtaChannel_Test, EmptyChannelFloats)
{
    AtaChannel channel;
    for (uint8_t reg = ErrorFeatures; reg <= Control; reg++)
        EXPECT_EQ(channel.ReadRegister(reg), 0xFF) << int(reg);
    EXPECT_EQ(channel.ReadData(), 0xFFFF);
    EXPECT_FALSE(channel.Intrq());

    // A disk unit without a medium is not on the bus either
    Unit master = AddDisk(channel, 0, 0x11, /*attach*/ false);
    EXPECT_EQ(channel.ReadRegister(StatusCommand), 0xFF);
    master.disk->AttachMedium(*master.medium, {});
    EXPECT_EQ(channel.ReadRegister(StatusCommand), Status::DRDY | Status::DSC);
}

// A host with the ATA DD7 pull-down (the Sprinter's AT board): an empty channel reads #7F, BSY = 0, so the
// firmware sees "no device" at once; the master still answers for an absent slave with status #00
TEST(AtaChannel_Test, EmptyChannelWithTheDd7PullDown)
{
    AtaChannel channel;
    channel.SetEmptyBus(AtaChannel::kEmptyBusDd7PullDown);
    for (uint8_t reg = ErrorFeatures; reg <= Control; reg++)
        EXPECT_EQ(channel.ReadRegister(reg), 0x7F) << int(reg);
    EXPECT_EQ(channel.ReadData(), 0xFF7F);
    channel.WriteRegister(SectorCount, 0x05);
    EXPECT_EQ(channel.ReadRegister(SectorCount), 0x7F) << "no register echo: nothing latched the write";
    channel.WriteRegister(DeviceHead, DeviceBits::DEV);
    EXPECT_EQ(channel.ReadRegister(StatusCommand), 0x7F) << "either unit of an empty channel";

    Unit master = AddDisk(channel, 0, 0x11);
    EXPECT_EQ(channel.ReadRegister(StatusCommand), 0x00) << "absent device 1 next to device 0: status 0 (ATA)";
    EXPECT_EQ(channel.ReadRegister(Control), 0x00);
    channel.WriteRegister(DeviceHead, 0);
    EXPECT_EQ(channel.ReadRegister(StatusCommand), Status::DRDY | Status::DSC);
}

TEST(AtaChannel_Test, MasterAnswersForAnAbsentSlave)
{
    AtaChannel channel;
    Unit master = AddDisk(channel, 0, 0x11);

    channel.WriteRegister(DeviceHead, DeviceBits::DEV);  // select the slave
    EXPECT_EQ(channel.Selected(), 1);
    EXPECT_EQ(channel.ReadRegister(StatusCommand), 0x00) << "absent device 1: status 0";
    EXPECT_EQ(channel.ReadRegister(Control), 0x00);
    channel.WriteRegister(SectorCount, 0x42);
    EXPECT_EQ(channel.ReadRegister(SectorCount), 0x42) << "the master latched the write and answers";
    channel.WriteRegister(StatusCommand, Command::Identify);
    EXPECT_EQ(channel.ReadData(), 0xFFFF) << "the command went to nobody";
    EXPECT_FALSE(channel.Intrq());
}

TEST(AtaChannel_Test, CommandsGoToTheSelectedUnit)
{
    AtaChannel channel;
    Unit master = AddDisk(channel, 0, 0x11);
    Unit slave = AddDisk(channel, 1, 0x22);

    EXPECT_EQ(ReadFirstWord(channel, 0), 0x1111);
    EXPECT_EQ(ReadFirstWord(channel, DeviceBits::DEV), 0x2222);

    // Task-file writes reach both units
    channel.WriteRegister(SectorCount, 9);
    channel.WriteRegister(DeviceHead, 0);
    EXPECT_EQ(channel.ReadRegister(SectorCount), 9);
    EXPECT_EQ(slave.disk->State().sectorCount, 9);

    // A diagnostic runs on both and selects the master
    channel.WriteRegister(DeviceHead, DeviceBits::DEV);
    channel.WriteRegister(StatusCommand, Command::ExecuteDiagnostic);
    EXPECT_EQ(channel.Selected(), 0);
    EXPECT_EQ(slave.disk->State().sectorCount, 1);
    EXPECT_TRUE(channel.Intrq());

    // Hard reset: both at the power-on signature
    ReadFirstWord(channel, DeviceBits::DEV);
    channel.HardReset();
    EXPECT_EQ(channel.Selected(), 0);
    EXPECT_EQ(master.disk->State().phase, static_cast<uint8_t>(AtaPhase::Idle));
    EXPECT_EQ(slave.disk->State().phase, static_cast<uint8_t>(AtaPhase::Idle));
    EXPECT_EQ(channel.ReadData(), 0xFFFF);
}
