// ATAPI CD-ROM conformance (IDE design §12.1, ATAPI table): signature, packet
// protocol, the SCSI commands, sense, a disc swap, a mixed channel. The disc is
// a synthetic ISO of 40 blocks in a MemoryDisk: block n holds (n + i) & #FF,
// block 16 starts with the ISO 9660 volume descriptor id "\x01CD001"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "emulator/io/ide/ata/atachannel.h"
#include "emulator/io/ide/ata/atadisk.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/storage/memorydisk.h"

using namespace ata;

namespace
{
    constexpr uint32_t kBlocks = 40;

    std::unique_ptr<MemoryDisk> MakeIso(uint8_t salt = 0)
    {
        auto iso = std::make_unique<MemoryDisk>(kBlocks * 4, /*writable*/ false);
        for (uint32_t n = 0; n < kBlocks; n++)
        {
            for (size_t i = 0; i < 2048; i++)
                iso->Data()[n * 2048 + i] = static_cast<uint8_t>(n + i + salt);
        }
        std::memcpy(iso->Data() + 16 * 2048, "\x01" "CD001", 6);
        return iso;
    }

    class AtapiCdrom_Test : public ::testing::Test
    {
    protected:
        AtaChannel _channel;
        AtapiCdrom* _cd = nullptr;
        std::unique_ptr<MemoryDisk> _iso;

        void SetUp() override
        {
            auto cd = std::make_unique<AtapiCdrom>();
            _cd = cd.get();
            _channel.SetUnit(0, std::move(cd));
        }

        void InsertDisc(uint8_t salt = 0)
        {
            _iso = MakeIso(salt);
            _cd->AttachMedium(*_iso, {});
        }

        uint8_t Status() { return _channel.ReadRegister(StatusCommand); }
        uint8_t Reason() { return _channel.ReadRegister(SectorCount); }
        uint16_t ByteCount()
        {
            return static_cast<uint16_t>(_channel.ReadRegister(CylinderLow) | (_channel.ReadRegister(CylinderHigh) << 8));
        }

        void SendPacket(std::vector<uint8_t> cdb, uint16_t limit = 2048)
        {
            cdb.resize(12, 0);
            _channel.WriteRegister(ErrorFeatures, 0);
            _channel.WriteRegister(CylinderLow, static_cast<uint8_t>(limit));
            _channel.WriteRegister(CylinderHigh, static_cast<uint8_t>(limit >> 8));
            _channel.WriteRegister(StatusCommand, Command::Packet);
            ASSERT_EQ(Status() & (Status::DRQ | Status::BSY), Status::DRQ) << "the drive asks for the packet";
            ASSERT_EQ(Reason(), 1) << "interrupt reason: command";
            for (size_t i = 0; i < 12; i += 2)
                _channel.WriteData(static_cast<uint16_t>(cdb[i] | (cdb[i + 1] << 8)));
        }

        /// One DRQ block of the reply
        std::vector<uint8_t> ReadChunk()
        {
            const uint16_t count = ByteCount();
            std::vector<uint8_t> data;
            for (uint16_t i = 0; i < count; i += 2)
            {
                const uint16_t word = _channel.ReadData();
                data.push_back(static_cast<uint8_t>(word));
                if (i + 1 < count)
                    data.push_back(static_cast<uint8_t>(word >> 8));
            }
            return data;
        }

        std::vector<uint8_t> Block(uint32_t n) const
        {
            return std::vector<uint8_t>(_iso->Data() + n * 2048, _iso->Data() + (n + 1) * 2048);
        }

        /// Completed without error: DRDY, interrupt reason "status"
        bool Completed() { return Reason() == 3 && Status() == Status::DRDY; }

        /// CHECK CONDITION: ERR and the sense key in the error register; then REQUEST SENSE gives key / ASC
        void ExpectCheck(uint8_t key, uint8_t asc)
        {
            EXPECT_EQ(Status() & Status::ERR, Status::ERR);
            EXPECT_EQ(_channel.ReadRegister(ErrorFeatures) >> 4, key);
            SendPacket({0x03, 0, 0, 0, 18});
            const std::vector<uint8_t> sense = ReadChunk();
            ASSERT_EQ(sense.size(), 18u);
            EXPECT_EQ(sense[2], key);
            EXPECT_EQ(sense[12], asc);
            EXPECT_TRUE(Completed());
        }

        void ClearUnitAttention()
        {
            SendPacket({0x00});
            SendPacket({0x03, 0, 0, 0, 18});
            ReadChunk();
        }
    };
}  // namespace

TEST_F(AtapiCdrom_Test, SignatureAndIdentify)
{
    EXPECT_EQ(_channel.ReadRegister(CylinderLow), kAtapiSignatureLow);
    EXPECT_EQ(_channel.ReadRegister(CylinderHigh), kAtapiSignatureHigh);
    EXPECT_EQ(Status(), 0) << "ATAPI after a reset: status 0";

    // IDENTIFY DEVICE aborts and leaves the signature: "this is a CD drive"
    _channel.WriteRegister(CylinderLow, 0);
    _channel.WriteRegister(StatusCommand, Command::Identify);
    EXPECT_EQ(Status() & Status::ERR, Status::ERR);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::ABRT);
    EXPECT_EQ(_channel.ReadRegister(CylinderLow), kAtapiSignatureLow);
    EXPECT_EQ(_channel.ReadRegister(CylinderHigh), kAtapiSignatureHigh);

    _channel.WriteRegister(StatusCommand, Command::IdentifyPacket);
    EXPECT_TRUE(Status() & Status::DRQ);
    const uint16_t word0 = _channel.ReadData();
    EXPECT_EQ(word0, 0x85C0);
    for (int i = 1; i < 256; i++)
        _channel.ReadData();
    EXPECT_EQ(Status(), Status::DRDY);

    _channel.WriteRegister(StatusCommand, Command::WriteSectors);
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures), Error::ABRT) << "a CD takes no ATA writes";

    _channel.WriteRegister(StatusCommand, Command::DeviceReset);
    EXPECT_EQ(Status(), 0);
    EXPECT_EQ(_channel.ReadRegister(CylinderHigh), kAtapiSignatureHigh);
}

TEST_F(AtapiCdrom_Test, ReadABlockThroughThePacketProtocol)
{
    InsertDisc();

    // The first command after the disc arrived reports it (unit attention)
    SendPacket({0x00});
    ExpectCheck(AtapiCdrom::kSenseUnitAttention, AtapiCdrom::kAscMediumChanged);
    SendPacket({0x00});
    EXPECT_TRUE(_channel.Intrq());
    EXPECT_TRUE(Completed()) << "then the drive is ready (the status read acknowledges the interrupt)";

    // READ (10), LBA 16, one block
    SendPacket({0x28, 0, 0, 0, 0, 16, 0, 0, 1});
    EXPECT_EQ(Status(), Status::DRDY | Status::DRQ);
    EXPECT_EQ(Reason(), 2) << "data to the host";
    EXPECT_EQ(ByteCount(), 2048);
    const std::vector<uint8_t> block = ReadChunk();
    EXPECT_EQ(block, Block(16));
    EXPECT_EQ(std::memcmp(block.data(), "\x01" "CD001", 6), 0);
    EXPECT_TRUE(_channel.Intrq());
    EXPECT_TRUE(Completed());

    // READ (12), three blocks: one DRQ block each
    SendPacket({0xA8, 0, 0, 0, 0, 20, 0, 0, 0, 3});
    for (uint32_t n = 20; n < 23; n++)
        EXPECT_EQ(ReadChunk(), Block(n)) << n;
    EXPECT_TRUE(Completed());
}

TEST_F(AtapiCdrom_Test, SmallByteCountLimitSplitsBlocks)
{
    InsertDisc();
    ClearUnitAttention();

    SendPacket({0x28, 0, 0, 0, 0, 5, 0, 0, 2}, /*limit*/ 512);
    std::vector<uint8_t> all;
    for (int chunk = 0; chunk < 8; chunk++)
    {
        EXPECT_EQ(ByteCount(), 512) << chunk;
        const std::vector<uint8_t> part = ReadChunk();
        all.insert(all.end(), part.begin(), part.end());
    }
    std::vector<uint8_t> expected = Block(5);
    const std::vector<uint8_t> six = Block(6);
    expected.insert(expected.end(), six.begin(), six.end());
    EXPECT_EQ(all, expected);
    EXPECT_TRUE(Completed());
}

TEST_F(AtapiCdrom_Test, CapacityTocInquiryModeSense)
{
    InsertDisc();
    ClearUnitAttention();

    SendPacket({0x25});
    std::vector<uint8_t> capacity = ReadChunk();
    ASSERT_EQ(capacity.size(), 8u);
    EXPECT_EQ(capacity[3], kBlocks - 1) << "last LBA";
    EXPECT_EQ((capacity[6] << 8) | capacity[7], 2048);

    SendPacket({0x43, 0, 0, 0, 0, 0, 0, 0x08, 0x00});  // READ TOC, format 0, allocation 2048
    std::vector<uint8_t> toc = ReadChunk();
    ASSERT_EQ(toc.size(), 20u);
    EXPECT_EQ(toc[1], 18) << "TOC data length";
    EXPECT_EQ(toc[2], 1);
    EXPECT_EQ(toc[6], 1) << "track 1";
    EXPECT_EQ(toc[5], 0x14) << "data track";
    EXPECT_EQ(toc[14], 0xAA) << "lead-out";
    EXPECT_EQ(toc[19], kBlocks);

    SendPacket({0x12, 0, 0, 0, 36});
    std::vector<uint8_t> inquiry = ReadChunk();
    ASSERT_EQ(inquiry.size(), 36u);
    EXPECT_EQ(inquiry[0], 0x05) << "CD-ROM";
    EXPECT_EQ(inquiry[1], 0x80) << "removable";

    SendPacket({0x5A, 0, 0, 0, 0, 0, 0, 0, 8});
    EXPECT_EQ(ReadChunk().size(), 8u);
    for (uint8_t op : {uint8_t(0x1B), uint8_t(0x1E), uint8_t(0x2B), uint8_t(0x35), uint8_t(0xBB)})
    {
        SendPacket({op});
        EXPECT_TRUE(Completed()) << int(op);
    }
}

TEST_F(AtapiCdrom_Test, ErrorsReportSense)
{
    SendPacket({0x00});
    ExpectCheck(AtapiCdrom::kSenseNotReady, AtapiCdrom::kAscMediumNotPresent);

    InsertDisc();
    ClearUnitAttention();
    SendPacket({0x28, 0, 0, 0, 0, kBlocks, 0, 0, 1});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscLbaOutOfRange);
    SendPacket({0x2A, 0, 0, 0, 0, 1, 0, 0, 1});  // WRITE (10)
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscInvalidCommand);
}

TEST_F(AtapiCdrom_Test, DiscSwapRaisesUnitAttentionOnce)
{
    InsertDisc(0);
    ClearUnitAttention();
    SendPacket({0x00});
    EXPECT_TRUE(Completed());

    InsertDisc(0x55);
    SendPacket({0x12, 0, 0, 0, 36});  // INQUIRY does not consume it
    ReadChunk();
    SendPacket({0x28, 0, 0, 0, 0, 1, 0, 0, 1});
    ExpectCheck(AtapiCdrom::kSenseUnitAttention, AtapiCdrom::kAscMediumChanged);
    SendPacket({0x28, 0, 0, 0, 0, 1, 0, 0, 1});
    EXPECT_EQ(ReadChunk(), Block(1)) << "the new disc";

    _cd->DetachMedium();
    SendPacket({0x00});
    ExpectCheck(AtapiCdrom::kSenseUnitAttention, AtapiCdrom::kAscMediumChanged);
    SendPacket({0x00});
    ExpectCheck(AtapiCdrom::kSenseNotReady, AtapiCdrom::kAscMediumNotPresent);
}

TEST_F(AtapiCdrom_Test, DiskAndCdOnOneChannel)
{
    // Master: a hard disk; slave: this CD drive
    AtaChannel channel;
    MemoryDisk disk(1024);
    auto master = std::make_unique<AtaDisk>();
    master->AttachMedium(disk, {});
    channel.SetUnit(0, std::move(master));
    channel.SetUnit(1, std::make_unique<AtapiCdrom>());

    channel.WriteRegister(DeviceHead, 0xA0);
    channel.WriteRegister(StatusCommand, Command::Identify);
    EXPECT_TRUE(channel.ReadRegister(StatusCommand) & Status::DRQ) << "the disk answers IDENTIFY";
    for (int i = 0; i < 256; i++)
        channel.ReadData();

    channel.WriteRegister(DeviceHead, 0xB0);
    channel.WriteRegister(StatusCommand, Command::Identify);
    EXPECT_EQ(channel.ReadRegister(ErrorFeatures), Error::ABRT);
    EXPECT_EQ(channel.ReadRegister(CylinderLow), kAtapiSignatureLow);
    EXPECT_EQ(channel.ReadRegister(CylinderHigh), kAtapiSignatureHigh);
}

TEST_F(AtapiCdrom_Test, StateRoundTripMidRead)
{
    InsertDisc();
    ClearUnitAttention();
    SendPacket({0x28, 0, 0, 0, 0, 7, 0, 0, 3});
    for (int i = 0; i < 700; i++)
        _channel.ReadData();
    const AtaDeviceState saved = _cd->State();

    AtaChannel other;
    auto copy = std::make_unique<AtapiCdrom>();
    AtapiCdrom* cd = copy.get();
    other.SetUnit(0, std::move(copy));
    cd->AttachMedium(*_iso, {});
    cd->SetState(saved);

    for (int i = 700; i < 1024 * 3; i++)
        ASSERT_EQ(other.ReadData(), _channel.ReadData()) << i;
    EXPECT_EQ(std::memcmp(&cd->State(), &_cd->State(), sizeof(AtaDeviceState)), 0);
}

/// SPC: REQUEST SENSE reports a pending unit attention and clears it;
/// GET EVENT STATUS NOTIFICATION passes without consuming it (MMC)
TEST_F(AtapiCdrom_Test, RequestSenseReportsUnitAttention)
{
    InsertDisc();
    SendPacket({0x4A, 0x01, 0, 0, 0x10, 0, 0, 0, 8});
    const std::vector<uint8_t> event = ReadChunk();
    ASSERT_EQ(event.size(), 4u);
    EXPECT_EQ(event[1], 2) << "no event: the header alone";
    EXPECT_TRUE(Completed());

    SendPacket({0x03, 0, 0, 0, 18});
    const std::vector<uint8_t> sense = ReadChunk();
    ASSERT_EQ(sense.size(), 18u);
    EXPECT_EQ(sense[2], AtapiCdrom::kSenseUnitAttention);
    EXPECT_EQ(sense[12], AtapiCdrom::kAscMediumChanged);
    SendPacket({0x00});
    EXPECT_TRUE(Completed()) << "reported: the drive is ready";

    // DEVICE RESET keeps the host's device control (nIEN)
    _channel.WriteRegister(Control, DeviceControl::nIEN);
    _channel.WriteRegister(StatusCommand, Command::DeviceReset);
    EXPECT_EQ(_cd->State().control, DeviceControl::nIEN);
}
