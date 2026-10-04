// ATA hard disk conformance (IDE design §12.1, layer 1): the command set and
// the transfer engine driven through a channel over a MemoryDisk whose sector
// n starts with n (32-bit, little endian) and continues with (n + i) & #FF

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "emulator/io/ide/ata/atachannel.h"
#include "emulator/io/ide/ata/atadisk.h"
#include "emulator/io/storage/memorydisk.h"

using namespace ata;

namespace
{
    class AtaDisk_Test : public ::testing::Test
    {
    protected:
        std::unique_ptr<MemoryDisk> _medium;
        AtaChannel _channel;
        AtaDisk* _disk = nullptr;

        void SetUp() override { Attach(2048); }

        void Attach(uint64_t sectors, DriveConfig config = {})
        {
            _medium = std::make_unique<MemoryDisk>(sectors);
            for (uint64_t n = 0; n < sectors; n++)
            {
                uint8_t* sector = _medium->Data() + n * 512;
                for (size_t i = 0; i < 512; i++)
                    sector[i] = static_cast<uint8_t>(n + i);
                std::memcpy(sector, &n, 4);  // little-endian hosts: the test data is only compared, never decoded
            }
            auto disk = std::make_unique<AtaDisk>();
            _disk = disk.get();
            _channel.SetUnit(0, std::move(disk));
            _disk->AttachMedium(*_medium, config);
        }

        void SelectLba(uint64_t lba, uint8_t count, uint8_t device = DeviceBits::LBA)
        {
            _channel.WriteRegister(DeviceHead, static_cast<uint8_t>(device | ((lba >> 24) & 0x0F)));
            _channel.WriteRegister(SectorCount, count);
            _channel.WriteRegister(SectorNumber, static_cast<uint8_t>(lba));
            _channel.WriteRegister(CylinderLow, static_cast<uint8_t>(lba >> 8));
            _channel.WriteRegister(CylinderHigh, static_cast<uint8_t>(lba >> 16));
        }

        std::vector<uint8_t> ReadBlock()
        {
            std::vector<uint8_t> data(512);
            for (size_t i = 0; i < 512; i += 2)
            {
                const uint16_t word = _channel.ReadData();
                data[i] = static_cast<uint8_t>(word);
                data[i + 1] = static_cast<uint8_t>(word >> 8);
            }
            return data;
        }

        void WriteBlock(uint8_t fill)
        {
            for (size_t i = 0; i < 512; i += 2)
                _channel.WriteData(static_cast<uint16_t>(fill | (fill << 8)));
        }

        std::vector<uint8_t> Expected(uint64_t n) const
        {
            return std::vector<uint8_t>(_medium->Data() + n * 512, _medium->Data() + (n + 1) * 512);
        }

        uint8_t Status() { return _channel.ReadRegister(StatusCommand); }
    };
}  // namespace

TEST_F(AtaDisk_Test, SignatureAfterResetAndDiagnostic)
{
    _channel.HardReset();
    EXPECT_EQ(_channel.ReadRegister(SectorCount), 1);
    EXPECT_EQ(_channel.ReadRegister(SectorNumber), 1);
    EXPECT_EQ(_channel.ReadRegister(CylinderLow), 0);
    EXPECT_EQ(_channel.ReadRegister(CylinderHigh), 0);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), 0x01) << "diagnostic code: no error";
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);

    _channel.WriteRegister(SectorCount, 7);
    _channel.WriteRegister(StatusCommand, Command::ExecuteDiagnostic);
    EXPECT_TRUE(_channel.Intrq());
    EXPECT_EQ(_channel.ReadRegister(SectorCount), 1);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), 0x01);
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);
    EXPECT_FALSE(_channel.Intrq()) << "reading the status acknowledges the interrupt";

    // SRST through the control register, nIEN masks the line
    _channel.WriteRegister(Control, DeviceControl::SRST | DeviceControl::nIEN);
    EXPECT_EQ(Status(), Status::BSY);
    _channel.WriteRegister(Control, DeviceControl::nIEN);
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);
    SelectLba(0, 1);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_FALSE(_channel.Intrq()) << "nIEN";
    _channel.WriteRegister(Control, 0);
    EXPECT_TRUE(_channel.Intrq());
}

TEST_F(AtaDisk_Test, IdentifyDescribesTheDisk)
{
    _channel.WriteRegister(StatusCommand, Command::Identify);
    EXPECT_TRUE(Status() & Status::DRQ);
    const std::vector<uint8_t> id = ReadBlock();
    auto word = [&id](size_t w) { return static_cast<uint16_t>(id[w * 2] | (id[w * 2 + 1] << 8)); };

    EXPECT_EQ(word(0), 0x045A);
    EXPECT_EQ(word(1), 2) << "2048 sectors / (16 x 63)";
    EXPECT_EQ(word(3), 16);
    EXPECT_EQ(word(6), 63);
    EXPECT_EQ(word(27), ('U' << 8) | 'N') << "ATA strings: first character in the high byte";
    EXPECT_EQ(word(49) & 0x0200, 0x0200) << "LBA";
    EXPECT_EQ(word(60) | (word(61) << 16), 2048);
    EXPECT_EQ(id[510], 0xA5);
    uint8_t sum = 0;
    for (uint8_t b : id)
        sum = static_cast<uint8_t>(sum + b);
    EXPECT_EQ(sum, 0) << "word 255 checksum";
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC) << "the block is read, DRQ off";
}

TEST_F(AtaDisk_Test, ReadSectorsInLbaAndChs)
{
    SelectLba(5, 2);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_TRUE(_channel.Intrq());
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC | Status::DRQ);
    EXPECT_EQ(ReadBlock(), Expected(5));
    EXPECT_TRUE(_channel.Intrq()) << "the second block raises the interrupt again";
    EXPECT_EQ(ReadBlock(), Expected(6));
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);
    EXPECT_EQ(_channel.ReadRegister(SectorNumber), 7) << "the address registers point at the next sector";
    EXPECT_EQ(_channel.ReadRegister(SectorCount), 0);
    EXPECT_EQ(_channel.ReadData(), 0xFFFF) << "no DRQ: no data";

    // CHS: cylinder 0, head 1, sector 3 = (0 x 16 + 1) x 63 + 2 = 65
    _channel.WriteRegister(DeviceHead, 0xA1);
    _channel.WriteRegister(SectorCount, 1);
    _channel.WriteRegister(SectorNumber, 3);
    _channel.WriteRegister(CylinderLow, 0);
    _channel.WriteRegister(CylinderHigh, 0);
    _channel.WriteRegister(StatusCommand, Command::ReadSectorsNoRetry);
    EXPECT_EQ(ReadBlock(), Expected(65));
    EXPECT_EQ(_channel.ReadRegister(SectorNumber), 4);

    // Sector 63 wraps to head 2, sector 1
    _channel.WriteRegister(DeviceHead, 0xA1);
    _channel.WriteRegister(SectorNumber, 63);
    _channel.WriteRegister(SectorCount, 1);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(ReadBlock(), Expected(63 + 62));
    EXPECT_EQ(_channel.ReadRegister(SectorNumber), 1);
    EXPECT_EQ(_channel.ReadRegister(DeviceHead) & 0x0F, 2);
}

TEST_F(AtaDisk_Test, CountZeroMeans256)
{
    SelectLba(10, 0);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    for (uint64_t n = 10; n < 10 + 256; n++)
        ASSERT_EQ(ReadBlock(), Expected(n)) << n;
    EXPECT_FALSE(Status() & Status::DRQ);
}

TEST_F(AtaDisk_Test, WritesReachTheMediumAndReportEachSector)
{
    int writes = 0;
    _disk->SetWriteListener([&writes] { writes++; });

    SelectLba(100, 2);
    _channel.WriteRegister(StatusCommand, Command::WriteSectors);
    EXPECT_FALSE(_channel.Intrq()) << "the first block of a write has no interrupt";
    EXPECT_TRUE(Status() & Status::DRQ);
    WriteBlock(0xAB);
    EXPECT_TRUE(_channel.Intrq());
    EXPECT_TRUE(Status() & Status::DRQ);
    WriteBlock(0xCD);
    EXPECT_TRUE(_channel.Intrq());
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);
    EXPECT_EQ(writes, 2);
    EXPECT_EQ(_medium->Data()[100 * 512], 0xAB);
    EXPECT_EQ(_medium->Data()[101 * 512 + 511], 0xCD);

    // The drive's write-protect jumper, and a read-only medium: ABRT, nothing written
    DriveConfig protectedDrive;
    protectedDrive.writeProtect = true;
    _disk->AttachMedium(*_medium, protectedDrive);
    SelectLba(200, 1);
    _channel.WriteRegister(StatusCommand, Command::WriteSectors);
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC | Status::ERR);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::ABRT);

    _disk->AttachMedium(*_medium, {});
    _medium->SetWritable(false);
    SelectLba(200, 1);
    _channel.WriteRegister(StatusCommand, Command::WriteSectorsNoRetry);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::ABRT);
    EXPECT_EQ(writes, 2);
}

TEST_F(AtaDisk_Test, OutOfRangeIsIdnf)
{
    SelectLba(2048, 1);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC | Status::ERR);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::IDNF);

    _channel.WriteRegister(DeviceHead, 0xA0);  // CHS
    _channel.WriteRegister(SectorNumber, 0);   // sector numbers start at 1
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::IDNF);

    // A read that runs past the end stops there
    SelectLba(2047, 2);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(ReadBlock(), Expected(2047));
    EXPECT_EQ(Status() & (Status::ERR | Status::DRQ), Status::ERR);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::IDNF);

    // Verify checks the range without data
    SelectLba(2040, 8);
    _channel.WriteRegister(StatusCommand, Command::ReadVerify);
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);
    SelectLba(2041, 8);
    _channel.WriteRegister(StatusCommand, Command::ReadVerify);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::IDNF);
}

TEST_F(AtaDisk_Test, InitializeDeviceParametersChangesTheTranslation)
{
    _channel.WriteRegister(DeviceHead, 0xA0 | 3);  // 4 heads
    _channel.WriteRegister(SectorCount, 16);       // 16 sectors per track
    _channel.WriteRegister(StatusCommand, Command::InitializeDeviceParameters);
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);

    // Cylinder 1, head 2, sector 5 = (1 x 4 + 2) x 16 + 4 = 100
    _channel.WriteRegister(DeviceHead, 0xA0 | 2);
    _channel.WriteRegister(SectorCount, 1);
    _channel.WriteRegister(SectorNumber, 5);
    _channel.WriteRegister(CylinderLow, 1);
    _channel.WriteRegister(CylinderHigh, 0);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(ReadBlock(), Expected(100));

    _channel.WriteRegister(DeviceHead, 0xA0);  // head 2 of 4 is fine, 5 of 4 is not
    _channel.WriteRegister(DeviceHead, 0xA0 | 5);
    _channel.WriteRegister(SectorNumber, 1);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::IDNF);

    _channel.WriteRegister(Control, DeviceControl::SRST);
    _channel.WriteRegister(Control, 0);
    _channel.WriteRegister(StatusCommand, Command::Identify);
    const std::vector<uint8_t> id = ReadBlock();
    EXPECT_EQ(id[55 * 2], 16) << "a reset restores the default translation";
}

TEST_F(AtaDisk_Test, MultipleModeInterruptsPerBlock)
{
    SelectLba(0, 8);
    _channel.WriteRegister(StatusCommand, Command::ReadMultiple);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::ABRT) << "multiple mode not set";

    _channel.WriteRegister(SectorCount, 3);
    _channel.WriteRegister(StatusCommand, Command::SetMultipleMode);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::ABRT) << "3 is no power of two";

    _channel.WriteRegister(SectorCount, 4);
    _channel.WriteRegister(StatusCommand, Command::SetMultipleMode);
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);

    SelectLba(20, 6);
    _channel.WriteRegister(StatusCommand, Command::ReadMultiple);
    for (uint64_t n = 20; n < 26; n++)
    {
        const bool blockStart = (n - 20) % 4 == 0;
        EXPECT_EQ(_channel.Intrq(), blockStart) << n;
        if (_channel.Intrq())
            Status();
        EXPECT_EQ(ReadBlock(), Expected(n)) << n;
    }
    EXPECT_FALSE(Status() & Status::DRQ);

    SelectLba(300, 4);
    _channel.WriteRegister(StatusCommand, Command::WriteMultiple);
    for (int i = 0; i < 4; i++)
    {
        EXPECT_FALSE(_channel.Intrq()) << "one block of 4: no interrupt inside it";
        WriteBlock(static_cast<uint8_t>(i));
    }
    EXPECT_TRUE(_channel.Intrq());
    EXPECT_EQ(_medium->Data()[303 * 512], 3);
}

TEST_F(AtaDisk_Test, Lba48UsesThePreviousRegisterValues)
{
    // Each register written twice: the first value becomes the high-order one
    _channel.WriteRegister(DeviceHead, DeviceBits::LBA);
    _channel.WriteRegister(SectorCount, 0);
    _channel.WriteRegister(SectorCount, 2);
    _channel.WriteRegister(SectorNumber, 0);
    _channel.WriteRegister(SectorNumber, 0x10);
    _channel.WriteRegister(CylinderLow, 0);
    _channel.WriteRegister(CylinderLow, 0x02);
    _channel.WriteRegister(CylinderHigh, 0);
    _channel.WriteRegister(CylinderHigh, 0);
    _channel.WriteRegister(StatusCommand, Command::ReadSectorsExt);
    EXPECT_EQ(ReadBlock(), Expected(0x210));
    EXPECT_EQ(ReadBlock(), Expected(0x211));
    EXPECT_EQ(_channel.ReadRegister(SectorNumber), 0x12);

    _channel.WriteRegister(Control, DeviceControl::HOB);
    EXPECT_EQ(_channel.ReadRegister(SectorNumber), 0) << "HOB: LBA 31:24";
    _channel.WriteRegister(Control, 0);
}

TEST_F(AtaDisk_Test, OtherCommands)
{
    _channel.WriteRegister(StatusCommand, Command::Packet);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::ABRT) << "a disk is no ATAPI device";
    _channel.WriteRegister(StatusCommand, 0x5A);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::ABRT);

    for (uint8_t command : {uint8_t(0x10), uint8_t(0x1F), uint8_t(0x70), Command::SetFeatures, Command::FlushCache,
                            Command::IdleImmediate})
    {
        _channel.WriteRegister(StatusCommand, command);
        EXPECT_EQ(Status(), Status::DRDY | Status::DSC) << int(command);
    }
    _channel.WriteRegister(StatusCommand, Command::CheckPowerMode);
    EXPECT_EQ(_channel.ReadRegister(SectorCount), 0xFF);

    _channel.WriteRegister(StatusCommand, Command::FormatTrack);
    WriteBlock(0);
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);
}

/// A CompactFlash card on an IDE adapter (CFA): the same command set as a disk, IDENTIFY word 0 = #848A (removable,
/// not magnetic), the CFA feature set in words 83 / 86 bit 2, the model "UNREAL-NG CF" (MAME's ata_cf answers the same
/// word 0, machine/atastorage.cpp cf_device_base)
TEST_F(AtaDisk_Test, CompactFlashIdentifiesAsCfa)
{
    DriveConfig config;
    config.compactFlash = true;
    Attach(2048, config);
    _channel.WriteRegister(StatusCommand, Command::Identify);
    const std::vector<uint8_t> id = ReadBlock();
    auto word = [&id](size_t w) { return static_cast<uint16_t>(id[w * 2] | (id[w * 2 + 1] << 8)); };

    EXPECT_EQ(word(0), 0x848A);
    EXPECT_EQ(word(83) & 0x0004, 0x0004) << "CFA feature set supported";
    EXPECT_EQ(word(86) & 0x0004, 0x0004) << "CFA feature set enabled";
    std::string model;
    for (size_t w = 27; w < 47; w++)
    {
        model.push_back(static_cast<char>(word(w) >> 8));
        model.push_back(static_cast<char>(word(w) & 0xFF));
    }
    EXPECT_EQ(model.substr(0, 12), "UNREAL-NG CF");
    EXPECT_EQ(word(60) | (word(61) << 16), 2048);
    uint8_t sum = 0;
    for (uint8_t b : id)
        sum = static_cast<uint8_t>(sum + b);
    EXPECT_EQ(sum, 0) << "word 255 checksum";

    // The same data path as a disk
    SelectLba(7, 1);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(ReadBlock(), Expected(7));
}

/// CFA 8-bit data transfer: SET FEATURES #01 moves one byte per data register access on D0-D7 (the upper lines read
/// high), #81 goes back to 16 bits, and any reset restores the 16-bit default. A hard disk ignores both codes
TEST_F(AtaDisk_Test, CompactFlashEightBitTransfer)
{
    DriveConfig config;
    config.compactFlash = true;
    Attach(64, config);
    _channel.WriteRegister(ErrorFeatures, 0x01);
    _channel.WriteRegister(StatusCommand, Command::SetFeatures);
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);

    SelectLba(3, 1);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    std::vector<uint8_t> data;
    for (int i = 0; i < 512; i++)
    {
        const uint16_t word = _channel.ReadData();
        EXPECT_EQ(word >> 8, 0xFF) << i;
        data.push_back(static_cast<uint8_t>(word));
    }
    EXPECT_EQ(data, Expected(3));
    EXPECT_FALSE(Status() & Status::DRQ) << "512 byte accesses move the sector";

    SelectLba(9, 1);
    _channel.WriteRegister(StatusCommand, Command::WriteSectors);
    for (int i = 0; i < 512; i++)
        _channel.WriteData(static_cast<uint16_t>(0xEE00 | (i & 0xFF)));
    EXPECT_EQ(Status(), Status::DRDY | Status::DSC);
    for (int i = 0; i < 512; i++)
        ASSERT_EQ(_medium->Data()[9 * 512 + i], static_cast<uint8_t>(i)) << "D0-D7 only, " << i;

    _channel.WriteRegister(ErrorFeatures, 0x81);
    _channel.WriteRegister(StatusCommand, Command::SetFeatures);
    SelectLba(3, 1);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(ReadBlock(), Expected(3)) << "16-bit again";

    _channel.WriteRegister(ErrorFeatures, 0x01);
    _channel.WriteRegister(StatusCommand, Command::SetFeatures);
    _disk->HardReset();
    SelectLba(3, 1);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(ReadBlock(), Expected(3)) << "a reset restores 16 bits";

    Attach(64);  // a hard disk
    _channel.WriteRegister(ErrorFeatures, 0x01);
    _channel.WriteRegister(StatusCommand, Command::SetFeatures);
    SelectLba(3, 1);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    EXPECT_EQ(ReadBlock(), Expected(3)) << "a disk keeps 16 bits";
}

/// The unit's state is plain data: a copy taken in the middle of a transfer
/// continues it exactly (TTD, IDE design §12.1 "state round-trip")
TEST_F(AtaDisk_Test, StateRoundTripMidTransfer)
{
    SelectLba(40, 3);
    _channel.WriteRegister(StatusCommand, Command::ReadSectors);
    for (int i = 0; i < 100; i++)
        _channel.ReadData();
    const AtaDeviceState saved = _disk->State();

    AtaChannel other;
    auto copy = std::make_unique<AtaDisk>();
    AtaDisk* disk = copy.get();
    other.SetUnit(0, std::move(copy));
    disk->AttachMedium(*_medium, {});
    disk->SetState(saved);

    for (int i = 100; i < 256 * 3; i++)
        ASSERT_EQ(other.ReadData(), _channel.ReadData()) << i;
    EXPECT_EQ(other.ReadRegister(StatusCommand), Status());
    EXPECT_EQ(std::memcmp(&disk->State(), &_disk->State(), sizeof(AtaDeviceState)), 0);
}

/// Profi disks (IDE design §8.3): the geometry comes from the ProfiHiDD header
/// at the first sector of cylinder 1; without one, the SYS ROM's 16 x 16
TEST(AtaDiskProfi_Test, GeometryFromTheProfiHiddHeader)
{
    auto header = [](MemoryDisk& disk, uint64_t lba, uint8_t heads, uint8_t sectors) {
        uint8_t* s = disk.Data() + lba * 512;
        s[1] = heads;  // big-endian words
        s[3] = sectors;
        std::memcpy(s + 16, "rPfoHiDD", 8);
    };

    MemoryDisk karabas(4096);
    header(karabas, 1008, 16, 63);
    auto g = AtaDisk::DetectProfiGeometry(karabas);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->heads, 16u);
    EXPECT_EQ(g->sectors, 63u);
    EXPECT_EQ(g->cylinders, 4096u / (16 * 63));

    MemoryDisk sys(4096);
    header(sys, 256, 16, 16);
    g = AtaDisk::DetectProfiGeometry(sys);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->sectors, 16u);

    MemoryDisk blank(4096);
    EXPECT_FALSE(AtaDisk::DetectProfiGeometry(blank).has_value());

    // On the Profi board: detected, or the SYS ROM format; an explicit CHSn wins
    AtaDisk disk;
    DriveConfig profi;
    profi.profiGeometry = true;
    disk.AttachMedium(karabas, profi);
    EXPECT_EQ(disk.DefaultGeometry().sectors, 63u);
    disk.AttachMedium(blank, profi);
    EXPECT_EQ(disk.DefaultGeometry().heads, 16u);
    EXPECT_EQ(disk.DefaultGeometry().sectors, 16u);
    profi.geometry = BlockGeometry{100, 4, 32};
    disk.AttachMedium(karabas, profi);
    EXPECT_EQ(disk.DefaultGeometry().sectors, 32u);
    disk.AttachMedium(karabas, {});
    EXPECT_EQ(disk.DefaultGeometry().sectors, 63u) << "other boards: the standard CHS for the size (16 x 63 here too)";
}
