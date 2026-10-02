// ATAPI CD-ROM conformance (IDE design §12.1, ATAPI table): signature, packet
// protocol, the SCSI commands, sense, a disc swap, a mixed channel. The disc is
// a synthetic ISO of 40 blocks in a MemoryDisk: block n holds (n + i) & #FF,
// block 16 starts with the ISO 9660 volume descriptor id "\x01CD001"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

#include "emulator/io/ide/ata/atachannel.h"
#include "emulator/io/ide/ata/atadisk.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/storage/cd/cdimageformats.h"
#include "emulator/io/storage/memorydisk.h"
#include "_helpers/cdtestdisc.h"
#include "_helpers/scratchfolder.h"

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

/// region <CD audio and the MMC commands of a disc with tracks>

namespace
{
    /// The fixture disc (cdtestdisc.h): track 1 data LBA 0-3, track 2 audio (pregap 4-7, INDEX 01 at 8) to 15,
    /// track 3 audio (PREGAP 16-17, INDEX 01 at 18) to 21; lead-out 22. The drive's clock is a variable here
    class AtapiCdromAudio_Test : public AtapiCdrom_Test
    {
    protected:
        std::unique_ptr<ScratchFolder> _folder;
        std::unique_ptr<CdImage> _disc;
        uint32_t _elapsed = 0;

        void SetUp() override
        {
            AtapiCdrom_Test::SetUp();
            _folder = std::make_unique<ScratchFolder>("atapi-audio");
            std::string error;
            _disc = CdImageFormats::Open(cdtest::WriteFixtureDisc(_folder->Path()), &error);
            ASSERT_NE(_disc, nullptr) << error;
            _cd->Audio().SetClock([this] { return _elapsed; });
            _cd->SetDisc(_disc.get());
            _cd->AttachMedium(*_disc, {});
            ClearUnitAttention();
        }

        void Frames(int count)
        {
            for (int i = 0; i < count; i++)
            {
                _elapsed = 0;
                _cd->Audio().FrameEnd(71680);
            }
        }

        std::vector<uint8_t> Command(std::vector<uint8_t> cdb, uint16_t limit = 0xFFFE)
        {
            SendPacket(std::move(cdb), limit);
            std::vector<uint8_t> all;
            while (Reason() == 2 && (Status() & Status::DRQ))
            {
                const std::vector<uint8_t> part = ReadChunk();
                all.insert(all.end(), part.begin(), part.end());
            }
            return all;
        }

        /// READ SUB-CHANNEL, current position (format 1); MSF or LBA addresses
        std::vector<uint8_t> Position(bool msf)
        {
            return Command({0x42, static_cast<uint8_t>(msf ? 0x02 : 0x00), 0x40, 0x01, 0, 0, 0, 0, 16});
        }

        /// Another disc in the drive (the unit attention cleared)
        void UseDisc(std::unique_ptr<CdImage> disc)
        {
            ASSERT_NE(disc, nullptr);
            _cd->DetachMedium();
            _disc = std::move(disc);
            _cd->SetDisc(_disc.get());
            _cd->AttachMedium(*_disc, {});
            ClearUnitAttention();
        }

        /// The whole sense data of the last command
        std::vector<uint8_t> Sense() { return Command({0x03, 0, 0, 0, 18}); }
    };

    uint32_t Be32(const std::vector<uint8_t>& b, size_t at)
    {
        return (static_cast<uint32_t>(b[at]) << 24) | (static_cast<uint32_t>(b[at + 1]) << 16) | (static_cast<uint32_t>(b[at + 2]) << 8) | b[at + 3];
    }
}  // namespace

TEST_F(AtapiCdromAudio_Test, ReadTocListsEveryTrack)
{
    // Format 0, LBA: 3 tracks + the lead-out
    std::vector<uint8_t> toc = Command({0x43, 0, 0, 0, 0, 0, 0, 0x03, 0x24});
    ASSERT_EQ(toc.size(), 4u + 4 * 8);
    EXPECT_EQ((toc[0] << 8) | toc[1], 34);
    EXPECT_EQ(toc[2], 1);
    EXPECT_EQ(toc[3], 3);
    const uint8_t expected[4][3] = {{0x14, 1, 0}, {0x10, 2, 8}, {0x10, 3, 18}, {0x10, 0xAA, 22}};
    for (int i = 0; i < 4; i++)
    {
        EXPECT_EQ(toc[4 + 8 * i + 1], expected[i][0]) << "ADR / control, entry " << i;
        EXPECT_EQ(toc[4 + 8 * i + 2], expected[i][1]) << "track, entry " << i;
        EXPECT_EQ(Be32(toc, 4 + 8 * i + 4), expected[i][2]) << "LBA, entry " << i;
    }

    // MSF, from track 2 (what NedoOS cdplay asks: 43 02 ... 03 24)
    toc = Command({0x43, 0x02, 0, 0, 0, 0, 2, 0x03, 0x24});
    ASSERT_EQ(toc.size(), 4u + 3 * 8);
    EXPECT_EQ(toc[4 + 2], 2);
    EXPECT_EQ(toc[4 + 5], 0);
    EXPECT_EQ(toc[4 + 6], 2) << "LBA 8 = 00:02:08";
    EXPECT_EQ(toc[4 + 7], 8);
    EXPECT_EQ(toc[4 + 16 + 7], 22) << "lead-out 00:02:22";

    toc = Command({0x43, 0, 0, 0, 0, 0, 0xAA, 0, 12});
    ASSERT_EQ(toc.size(), 12u) << "from the lead-out: the lead-out alone";
    EXPECT_EQ(toc[6], 0xAA);
    SendPacket({0x43, 0, 0, 0, 0, 0, 4, 0, 12});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscInvalidField);

    // Format 1 (sessions) and the SFF-8020 format field (byte 9 bits 7-6)
    toc = Command({0x43, 0, 0x01, 0, 0, 0, 0, 0, 12});
    ASSERT_EQ(toc.size(), 12u);
    EXPECT_EQ(toc[6], 1);
    EXPECT_EQ(Command({0x43, 0, 0, 0, 0, 0, 0, 0, 12, 0x40}), toc);

    // Format 2: A0 / A1 / A2 points, then one entry per track (11 bytes, MSF)
    toc = Command({0x43, 0x02, 0x02, 0, 0, 0, 0, 0x01, 0x00});
    ASSERT_EQ(toc.size(), 4u + 6 * 11);
    EXPECT_EQ(toc[4 + 3], 0xA0);
    EXPECT_EQ(toc[4 + 8], 1) << "first track";
    EXPECT_EQ(toc[4 + 11 + 3], 0xA1);
    EXPECT_EQ(toc[4 + 11 + 8], 3) << "last track";
    EXPECT_EQ(toc[4 + 22 + 3], 0xA2);
    EXPECT_EQ(toc[4 + 22 + 10], 22) << "lead-out frame";
    EXPECT_EQ(toc[4 + 44 + 3], 2);
    EXPECT_EQ(toc[4 + 44 + 10], 8);

    SendPacket({0x43, 0, 0x05, 0, 0, 0, 0, 0, 12});  // CD-TEXT: a pressed disc without
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscInvalidField);
}

TEST_F(AtapiCdromAudio_Test, PlayAudioMsfAndTheSubChannelPosition)
{
    std::vector<uint8_t> position = Position(true);
    ASSERT_EQ(position.size(), 16u);
    EXPECT_EQ(position[1], 0x15) << "no play yet";

    // PLAY AUDIO MSF 00:02:08 - 00:02:16 (track 2, as cdplay plays a track: its start to the next one's)
    SendPacket({0x47, 0, 0, 0, 2, 8, 0, 2, 16});
    EXPECT_TRUE(Completed());
    Frames(1);  // 903 samples: still LBA 9
    _elapsed = 35840;  // half a frame later: + 451 samples, LBA 10
    position = Position(true);
    EXPECT_EQ(position[1], 0x11) << "playing";
    EXPECT_EQ((position[2] << 8) | position[3], 12) << "sub-channel data length";
    EXPECT_EQ(position[4], 0x01);
    EXPECT_EQ(position[5], 0x10) << "ADR 1, audio";
    EXPECT_EQ(position[6], 2) << "track";
    EXPECT_EQ(position[7], 1) << "index";
    EXPECT_EQ(position[9], 0);
    EXPECT_EQ(position[10], 2);
    EXPECT_EQ(position[11], 10) << "absolute 00:02:10";
    EXPECT_EQ(position[15], 2) << "relative 00:00:02";

    position = Position(false);
    EXPECT_EQ(Be32(position, 8), 10u);
    EXPECT_EQ(Be32(position, 12), 2u);

    // Without SubQ: the header alone
    const std::vector<uint8_t> header = Command({0x42, 0, 0x00, 0x01, 0, 0, 0, 0, 16});
    ASSERT_EQ(header.size(), 4u);
    EXPECT_EQ(header[1], 0x11);

    // PAUSE, RESUME
    SendPacket({0x4B, 0, 0, 0, 0, 0, 0, 0, 0});
    EXPECT_TRUE(Completed());
    EXPECT_EQ(Position(true)[1], 0x12);
    SendPacket({0x4B, 0, 0, 0, 0, 0, 0, 0, 1});
    EXPECT_TRUE(Completed());
    EXPECT_EQ(Position(true)[1], 0x11);

    // To the end: 13h once, then 15h
    Frames(10);
    position = Position(true);
    EXPECT_EQ(position[1], 0x13);
    EXPECT_EQ(position[11], 15) << "the head stops at the play's last frame (00:02:15)";
    EXPECT_EQ(Position(true)[1], 0x15);

    // PAUSE with no play: command sequence error
    SendPacket({0x4B, 0, 0, 0, 0, 0, 0, 0, 0});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscCommandSequenceError);
}

TEST_F(AtapiCdromAudio_Test, ThePregapCountsDownInIndexZero)
{
    SendPacket({0x45, 0, 0, 0, 0, 5, 0, 0, 3});  // PLAY AUDIO (10): LBA 5, 3 frames (track 2's pregap)
    EXPECT_TRUE(Completed());
    const std::vector<uint8_t> position = Position(true);
    EXPECT_EQ(position[6], 2);
    EXPECT_EQ(position[7], 0) << "index 0: the pregap";
    EXPECT_EQ(position[15], 3) << "3 frames to INDEX 01";
    EXPECT_EQ(static_cast<int32_t>(Be32(Position(false), 12)), -3);
}

TEST_F(AtapiCdromAudio_Test, PlayCommandsAndTheirErrors)
{
    // PLAY AUDIO (12): LBA 18, 4 frames
    SendPacket({0xA5, 0, 0, 0, 0, 18, 0, 0, 0, 4});
    EXPECT_TRUE(Completed());
    EXPECT_EQ(_cd->Audio().State().endLba, 22u);
    // PLAY AUDIO TRACK / INDEX: track 2 index 1 to track 3 index 1 (both tracks)
    SendPacket({0x48, 0, 0, 0, 2, 1, 0, 3, 1});
    EXPECT_TRUE(Completed());
    EXPECT_EQ(_cd->Audio().HeadLba(), 8u);
    EXPECT_EQ(_cd->Audio().State().endLba, 22u);
    // ... track 2 from its pregap (index 0) to track 3 index 0: to track 3's INDEX 01
    SendPacket({0x48, 0, 0, 0, 2, 0, 0, 3, 0});
    EXPECT_EQ(_cd->Audio().HeadLba(), 4u);
    EXPECT_EQ(_cd->Audio().State().endLba, 18u);
    // From the current position (LBA #FFFFFFFF)
    Frames(2);
    const uint32_t head = _cd->Audio().HeadLba();
    SendPacket({0x45, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 2});
    EXPECT_TRUE(Completed());
    EXPECT_EQ(_cd->Audio().HeadLba(), head);
    // A zero length plays nothing and is no error; start == end in MSF neither
    SendPacket({0x45, 0, 0, 0, 0, 8, 0, 0, 0});
    EXPECT_TRUE(Completed());
    SendPacket({0x47, 0, 0, 0, 2, 8, 0, 2, 8});
    EXPECT_TRUE(Completed());

    // The data track: ILLEGAL MODE FOR THIS TRACK; a start past the lead-out: LBA out of range; an end past
    // it plays to the lead-out (MMC-3 checks the start only); end before start
    SendPacket({0x45, 0, 0, 0, 0, 1, 0, 0, 2});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscIllegalModeForTrack);
    SendPacket({0x45, 0, 0, 0, 0, 22, 0, 0, 5});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscLbaOutOfRange);
    SendPacket({0x45, 0, 0, 0, 0, 20, 0, 0, 5});
    EXPECT_TRUE(Completed());
    EXPECT_EQ(_cd->Audio().State().endLba, 22u);
    SendPacket({0x4E});
    SendPacket({0x47, 0, 0, 0, 2, 16, 0, 2, 8});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscInvalidField);
    SendPacket({0x48, 0, 0, 0, 9, 1, 0, 9, 1});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscInvalidField);

    // STOP PLAY / SCAN; a READ of data stops a play too
    SendPacket({0x45, 0, 0, 0, 0, 8, 0, 0, 8});
    SendPacket({0x4E});
    EXPECT_TRUE(Completed());
    EXPECT_EQ(Position(true)[1], 0x15);
    SendPacket({0x45, 0, 0, 0, 0, 8, 0, 0, 8});
    EXPECT_EQ(Command({0x28, 0, 0, 0, 0, 2, 0, 0, 1}).size(), 2048u);
    EXPECT_EQ(_cd->Audio().Status(), CdAudioStatus::Idle);
    // REQUEST SENSE while playing: ASC 00h, ASCQ 11h (audio play in progress)
    SendPacket({0x45, 0, 0, 0, 0, 8, 0, 0, 8});
    const std::vector<uint8_t> sense = Command({0x03, 0, 0, 0, 18});
    EXPECT_EQ(sense[2], 0);
    EXPECT_EQ(sense[13], 0x11);
}

TEST_F(AtapiCdromAudio_Test, ReadOfAnAudioFrameIsIllegal)
{
    SendPacket({0x28, 0, 0, 0, 0, 3, 0, 0, 2});  // LBA 3 (data) and 4 (audio pregap)
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscIllegalModeForTrack);
    SendPacket({0x44, 0, 0, 0, 0, 9, 0, 0, 8});  // READ HEADER of an audio frame: mode 0
    const std::vector<uint8_t> header = ReadChunk();
    ASSERT_EQ(header.size(), 8u);
    EXPECT_EQ(header[0], 0);
}

TEST_F(AtapiCdromAudio_Test, ReadCdRawAudioDataAndSubchannel)
{
    // READ CD, CD-DA, LBA 8, 2 frames, user data: 2352 bytes each, the samples little-endian
    std::vector<uint8_t> raw = Command({0xBE, 0x04, 0, 0, 0, 8, 0, 0, 2, 0x10, 0});
    ASSERT_EQ(raw.size(), 2u * 2352);
    for (uint32_t n = 0; n < 2 * 588; n++)
    {
        const int16_t left = static_cast<int16_t>(raw[4 * n] | (raw[4 * n + 1] << 8));
        const int16_t right = static_cast<int16_t>(raw[4 * n + 2] | (raw[4 * n + 3] << 8));
        ASSERT_EQ(left, cdtest::kRampA.Left(n)) << n;
        ASSERT_EQ(right, cdtest::kRampA.Right(n)) << n;
    }
    // A byte count limit of 1000: the 2352-byte frames go in pieces
    EXPECT_EQ(Command({0xBE, 0x04, 0, 0, 0, 8, 0, 0, 2, 0x10, 0}, 1000), raw);

    // READ CD MSF of the same frames, with the formatted Q subchannel (16 bytes each)
    std::vector<uint8_t> withQ = Command({0xB9, 0, 0, 0, 2, 8, 0, 2, 10, 0xF8, 0x02});
    ASSERT_EQ(withQ.size(), 2u * (2352 + 16));
    EXPECT_TRUE(std::equal(withQ.begin(), withQ.begin() + 2352, raw.begin()));
    const uint8_t* q = withQ.data() + 2352;
    EXPECT_EQ(q[0], 0x01) << "control 0 (audio), ADR 1";
    EXPECT_EQ(q[1], 0x02) << "track 02 (BCD)";
    EXPECT_EQ(q[2], 0x01) << "index 01";
    EXPECT_EQ(q[5], 0x00) << "relative frame 00";
    EXPECT_EQ(q[8], 0x02);
    EXPECT_EQ(q[9], 0x08) << "absolute 00:02:08 (BCD)";

    // Raw P-W: P set in a pregap, Q bit by bit in bit 6
    std::vector<uint8_t> pw = Command({0xBE, 0, 0, 0, 0, 5, 0, 0, 1, 0x00, 0x01});
    ASSERT_EQ(pw.size(), 96u);
    EXPECT_EQ(pw[0] & 0x80, 0x80);
    uint8_t qBits[12] = {};
    for (int i = 0; i < 96; i++)
        qBits[i / 8] = static_cast<uint8_t>(qBits[i / 8] | (((pw[i] >> 6) & 1) << (7 - i % 8)));
    EXPECT_EQ(qBits[1], 0x02);
    EXPECT_EQ(qBits[2], 0x00) << "index 0";

    // A data frame raw (sync, headers, user data, EDC / ECC): the whole frame
    std::vector<uint8_t> frame = Command({0xBE, 0x08, 0, 0, 0, 2, 0, 0, 1, 0xF8, 0});
    const std::string expected = cdtest::DataFrames(2, 1, true);
    ASSERT_EQ(frame.size(), 2352u);
    EXPECT_EQ(0, std::memcmp(frame.data(), expected.data(), 2352));
    // User data only
    frame = Command({0xBE, 0x08, 0, 0, 0, 2, 0, 0, 1, 0x10, 0});
    EXPECT_EQ(frame, cdtest::UserData(2));

    // The expected sector type must match: CD-DA asked of a data frame
    SendPacket({0xBE, 0x04, 0, 0, 0, 2, 0, 0, 1, 0x10, 0});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscIllegalModeForTrack);
}

TEST_F(AtapiCdromAudio_Test, ModePageZeroEhVolumeAndRouting)
{
    // MODE SENSE (10), page 0Eh, current values: port 0 left, port 1 right, both full
    std::vector<uint8_t> page = Command({0x5A, 0, 0x0E, 0, 0, 0, 0, 0, 24});
    ASSERT_EQ(page.size(), 24u);
    EXPECT_EQ(page[2], 0x03) << "medium: mixed audio / data";
    EXPECT_EQ(page[8], 0x0E);
    EXPECT_EQ(page[9], 14);
    EXPECT_EQ(page[10] & 0x04, 0x04) << "IMMED";
    EXPECT_EQ(page[16], 1);
    EXPECT_EQ(page[17], 0xFF);
    EXPECT_EQ(page[18], 2);
    EXPECT_EQ(page[19], 0xFF);

    // MODE SELECT (10): swap the channels, port 1 at half volume, SOTC
    std::vector<uint8_t> parameters(24, 0);
    parameters[8] = 0x0E;
    parameters[9] = 14;
    parameters[10] = 0x06;
    parameters[16] = 2;
    parameters[17] = 0xFF;
    parameters[18] = 1;
    parameters[19] = 0x80;
    SendPacket({0x55, 0x10, 0, 0, 0, 0, 0, 0, 24});
    ASSERT_EQ(Reason(), 0) << "data from the host";
    ASSERT_TRUE(Status() & Status::DRQ);
    EXPECT_EQ(ByteCount(), 24);
    for (size_t i = 0; i < parameters.size(); i += 2)
        _channel.WriteData(static_cast<uint16_t>(parameters[i] | (parameters[i + 1] << 8)));
    EXPECT_TRUE(Completed());
    const CdAudioState& state = _cd->Audio().State();
    EXPECT_EQ(state.portSelect[0], 2);
    EXPECT_EQ(state.portSelect[1], 1);
    EXPECT_EQ(state.portVolume[1], 0x80);
    EXPECT_EQ(state.sotc, 1);

    page = Command({0x1A, 0, 0x0E, 0, 20});  // MODE SENSE (6) shows it
    ASSERT_EQ(page.size(), 20u);
    EXPECT_EQ(page[4 + 2] & 0x02, 0x02);
    EXPECT_EQ(page[4 + 8], 2);
    EXPECT_EQ(page[4 + 11], 0x80);
    // Default and changeable values
    page = Command({0x1A, 0, 0x8E, 0, 20});
    EXPECT_EQ(page[4 + 8], 1) << "default: port 0 left";
    page = Command({0x1A, 0, 0x4E, 0, 20});
    EXPECT_EQ(page[4 + 9], 0xFF) << "changeable: the volume";
    // All pages: 01h, 0Dh, 0Eh, 2Ah
    page = Command({0x5A, 0, 0x3F, 0, 0, 0, 0, 0, 0xFF});
    EXPECT_EQ(page.size(), 8u + 8 + 8 + 16 + 20);
    EXPECT_EQ(page[8 + 32], 0x2A);
    EXPECT_EQ(page[8 + 32 + 4] & 0x01, 0x01) << "audio play";
    EXPECT_EQ((page[8 + 32 + 10] << 8) | page[8 + 32 + 11], 256) << "volume levels";

    // A broken page length: invalid parameter list
    SendPacket({0x15, 0x10, 0, 0, 8});
    const uint8_t broken[8] = {0, 0, 0, 0, 0x0E, 20, 0, 0};
    for (size_t i = 0; i < 8; i += 2)
        _channel.WriteData(static_cast<uint16_t>(broken[i] | (broken[i + 1] << 8)));
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscInvalidParameter);
}

TEST_F(AtapiCdromAudio_Test, SeekStopsAndMovesTheHead)
{
    SendPacket({0x45, 0, 0, 0, 0, 8, 0, 0, 8});
    SendPacket({0x2B, 0, 0, 0, 0, 19});
    EXPECT_TRUE(Completed());
    EXPECT_EQ(_cd->Audio().Status(), CdAudioStatus::Idle);
    EXPECT_EQ(Be32(Position(false), 8), 19u);
    // DEVICE RESET keeps the audio; the reset line stops it
    SendPacket({0x45, 0, 0, 0, 0, 8, 0, 0, 8});
    _channel.WriteRegister(StatusCommand, Command::DeviceReset);
    EXPECT_EQ(_cd->Audio().Status(), CdAudioStatus::Playing);
    _channel.HardReset();
    EXPECT_EQ(_cd->Audio().Status(), CdAudioStatus::Idle);
}

/// endregion </CD audio>

/// region <Data tracks, multisession, the activity LED>

TEST_F(AtapiCdromAudio_Test, PlayStartingInADataTrackFailsAtOnce)
{
    // PLAY AUDIO MSF 00:02:01 - 00:02:20 (the data track 1): CHECK CONDITION, ILLEGAL REQUEST,
    // ILLEGAL MODE FOR THIS TRACK (MMC-3 PLAY AUDIO); the head stays, the audio status stays
    const uint32_t head = _cd->Audio().HeadLba();
    SendPacket({0x47, 0, 0, 0, 2, 1, 0, 2, 20});
    EXPECT_EQ(Status() & Status::ERR, Status::ERR) << "fails at once, no play started";
    EXPECT_EQ(_channel.ReadRegister(ErrorFeatures) >> 4, 0x05);
    const std::vector<uint8_t> sense = Sense();
    ASSERT_EQ(sense.size(), 18u);
    EXPECT_EQ(sense[0], 0x70);
    EXPECT_EQ(sense[2], 0x05) << "ILLEGAL REQUEST";
    EXPECT_EQ(sense[7], 10);
    EXPECT_EQ(sense[12], 0x64) << "ILLEGAL MODE FOR THIS TRACK";
    EXPECT_EQ(sense[13], 0x00);
    EXPECT_EQ(_cd->Audio().HeadLba(), head);
    EXPECT_EQ(Position(true)[1], 0x15) << "no current audio status, as before";
    // PLAY AUDIO (10) and (12) the same
    for (const std::vector<uint8_t>& play : {std::vector<uint8_t>{0x45, 0, 0, 0, 0, 0, 0, 0, 4},
                                             std::vector<uint8_t>{0xA5, 0, 0, 0, 0, 2, 0, 0, 0, 2}})
    {
        SendPacket(play);
        ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscIllegalModeForTrack);
    }

    // During a play a refused PLAY leaves it playing where it was
    SendPacket({0x47, 0, 0, 0, 2, 8, 0, 2, 22});
    ASSERT_TRUE(Completed());
    Frames(1);
    const uint64_t playing = _cd->Audio().HeadSample();
    SendPacket({0x47, 0, 0, 0, 2, 0, 0, 2, 3});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscIllegalModeForTrack);
    EXPECT_EQ(_cd->Audio().Status(), CdAudioStatus::Playing);
    EXPECT_EQ(_cd->Audio().HeadSample(), playing);
    EXPECT_EQ(_cd->Audio().State().endLba, 22u) << "the earlier play's range";
}

TEST_F(AtapiCdromAudio_Test, PlayRunningIntoADataTrackIsRefused)
{
    // One session, audio first and a data track after it: a range from the audio into the data
    // track ends with END OF USER AREA ENCOUNTERED ON THIS TRACK (MMC-3: the sub-channel mode
    // changes within the transfer length); up to the data track it plays
    std::string error;
    cdtest::WriteFile(_folder->Path() / "a-then-d.bin", cdtest::RampPcm(cdtest::kRampA, 10) + cdtest::DataFrames(10, 10, true));
    UseDisc(CdImageFormats::ParseCue("FILE \"a-then-d.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
                                     "  TRACK 02 MODE1/2352\n    INDEX 01 00:00:10\n",
                                     cdtest::Utf8(_folder->Path()), "a-then-d.cue", &error));
    SendPacket({0x45, 0, 0, 0, 0, 2, 0, 0, 10});  // LBA 2-11: crosses into track 2 at LBA 10
    EXPECT_EQ(Status() & Status::ERR, Status::ERR);
    const std::vector<uint8_t> sense = Sense();
    EXPECT_EQ(sense[2], 0x05);
    EXPECT_EQ(sense[12], 0x63) << "END OF USER AREA ENCOUNTERED ON THIS TRACK";
    EXPECT_EQ(sense[13], 0x00);
    EXPECT_EQ(_cd->Audio().Status(), CdAudioStatus::Idle);
    // PLAY AUDIO MSF has no such rule: the contiguous audio before the data track plays (MMC-3 5.13)
    SendPacket({0x47, 0, 0, 0, 2, 2, 0, 2, 21});  // LBA 2-18
    EXPECT_TRUE(Completed());
    EXPECT_EQ(_cd->Audio().State().endLba, 10u);
    SendPacket({0x4E});
    SendPacket({0x45, 0, 0, 0, 0, 2, 0, 0, 8});  // LBA 2-9: audio only
    EXPECT_TRUE(Completed());
    EXPECT_EQ(_cd->Audio().State().endLba, 10u);
    Frames(20);
    EXPECT_EQ(Position(false)[1], 0x13) << "completed";
}

TEST_F(AtapiCdromAudio_Test, EnhancedCdTocSessionsAndPlay)
{
    // Session 1: audio 1-2 (4 s each), session 2: data track 3 (300 frames) after the 11400-frame gap
    std::string error;
    UseDisc(CdImageFormats::Open(cdtest::WriteMusicDisc(_folder->Path(), 2, 4, 300), &error));
    const cdtest::MusicDiscLayout l = cdtest::MusicLayoutOf(2, 4, 300, cdtest::MusicLayout::Enhanced);

    // Format 0: every track of every session, the last session's lead-out (LBA)
    std::vector<uint8_t> toc = Command({0x43, 0, 0, 0, 0, 0, 0, 0x03, 0x24});
    ASSERT_EQ(toc.size(), 4u + 4 * 8);
    EXPECT_EQ(toc[2], 1);
    EXPECT_EQ(toc[3], 3);
    EXPECT_EQ(toc[5], 0x10) << "track 1 audio";
    EXPECT_EQ(Be32(toc, 8), 0u);
    EXPECT_EQ(Be32(toc, 16), l.audioStart[1]);
    EXPECT_EQ(toc[21], 0x14) << "track 3 data";
    EXPECT_EQ(Be32(toc, 24), l.dataStart);
    EXPECT_EQ(toc[30], 0xAA);
    EXPECT_EQ(Be32(toc, 32), l.leadOut);

    // Format 1: first and last complete session, the last session's first track
    toc = Command({0x43, 0, 0x01, 0, 0, 0, 0, 0, 12});
    ASSERT_EQ(toc.size(), 12u);
    EXPECT_EQ(toc[1], 10);
    EXPECT_EQ(toc[2], 1);
    EXPECT_EQ(toc[3], 2);
    EXPECT_EQ(toc[5], 0x14);
    EXPECT_EQ(toc[6], 3);
    EXPECT_EQ(Be32(toc, 8), l.dataStart);

    // Format 2: per session A0 / A1 / A2 and the tracks; B0 and C0 (ADR 5) after session 1
    toc = Command({0x43, 0x02, 0x02, 0, 0, 0, 0, 0x04, 0});
    const size_t entries = (toc.size() - 4) / 11;
    ASSERT_EQ(entries, 11u);
    EXPECT_EQ(toc[2], 1);
    EXPECT_EQ(toc[3], 2);
    auto entry = [&toc](size_t i) { return std::vector<uint8_t>(toc.begin() + 4 + 11 * i, toc.begin() + 4 + 11 * (i + 1)); };
    auto msf = [](uint32_t lba) { const cd::Msf m = cd::LbaToMsf(lba); return std::vector<uint8_t>{m.m, m.s, m.f}; };
    auto p = [](const std::vector<uint8_t>& e) { return std::vector<uint8_t>{e[8], e[9], e[10]}; };
    EXPECT_EQ(entry(0), (std::vector<uint8_t>{1, 0x10, 0, 0xA0, 0, 0, 0, 0, 1, 0x00, 0})) << "session 1: CD-DA";
    EXPECT_EQ(entry(1), (std::vector<uint8_t>{1, 0x10, 0, 0xA1, 0, 0, 0, 0, 2, 0, 0}));
    EXPECT_EQ(entry(2)[3], 0xA2);
    EXPECT_EQ(p(entry(2)), msf(l.audioLeadOut)) << "session 1's lead-out";
    EXPECT_EQ(entry(3)[3], 1);
    EXPECT_EQ(p(entry(3)), msf(0));
    EXPECT_EQ(entry(4)[3], 2);
    EXPECT_EQ(p(entry(4)), msf(l.audioStart[1]));
    EXPECT_EQ(entry(5)[1], 0x50) << "B0: ADR 5";
    EXPECT_EQ(entry(5)[3], 0xB0);
    const std::vector<uint8_t> b0 = entry(5);
    EXPECT_EQ(std::vector<uint8_t>(b0.begin() + 4, b0.begin() + 7), msf(l.dataPregap)) << "the next program area";
    EXPECT_EQ(entry(5)[7], 2);
    EXPECT_EQ(p(entry(5)), (std::vector<uint8_t>{79, 59, 74}));
    EXPECT_EQ(entry(6)[3], 0xC0);
    EXPECT_EQ(entry(7), (std::vector<uint8_t>{2, 0x14, 0, 0xA0, 0, 0, 0, 0, 3, 0x20, 0})) << "session 2: CD-ROM XA";
    EXPECT_EQ(entry(8)[8], 3);
    EXPECT_EQ(p(entry(9)), msf(l.leadOut));
    EXPECT_EQ(entry(10)[0], 2);
    EXPECT_EQ(entry(10)[3], 3);
    EXPECT_EQ(p(entry(10)), msf(l.dataStart));

    // A player's "track 2 to the next track's start": plays to session 1's lead-out, then 13h
    const cd::Msf from = cd::LbaToMsf(l.audioStart[1]);
    const cd::Msf to = cd::LbaToMsf(l.dataStart);
    SendPacket({0x47, 0, 0, from.m, from.s, from.f, to.m, to.s, to.f});
    ASSERT_TRUE(Completed());
    EXPECT_EQ(_cd->Audio().State().endLba, l.audioLeadOut);
    Frames(250);
    EXPECT_EQ(Position(false)[1], 0x13);
    // Track 3 (data) is refused; the gap between the sessions reads nothing
    SendPacket({0x47, 0, 0, to.m, to.s, to.f, to.m, static_cast<uint8_t>(to.s + 1), to.f});
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscIllegalModeForTrack);
    SendPacket({0x28, 0, 0, 0, 0x03, 0x20, 0, 0, 1});  // READ (10) LBA 800: the lead-out of session 1
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscLbaOutOfRange);
    SendPacket({0x2B, 0, 0, 0, 0x03, 0x20});  // SEEK there
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscLbaOutOfRange);
    const std::vector<uint8_t> block = Command({0x28, 0, static_cast<uint8_t>(l.dataStart >> 24), static_cast<uint8_t>(l.dataStart >> 16),
                                                static_cast<uint8_t>(l.dataStart >> 8), static_cast<uint8_t>(l.dataStart), 0, 0, 1});
    EXPECT_EQ(block, cdtest::UserData(l.dataStart)) << "the data session reads as on a computer drive";
}

TEST_F(AtapiCdromAudio_Test, ActivityLedLightsOnlyOnDataTransfers)
{
    std::atomic<uint64_t> activity{0};
    _cd->SetActivityCounter(&activity);
    // A player's polls and audio commands: no LED
    Position(true);
    SendPacket({0x00});
    Command({0x43, 0, 0, 0, 0, 0, 0, 0x03, 0x24});
    Command({0x12, 0, 0, 0, 36});
    SendPacket({0x47, 0, 0, 0, 2, 8, 0, 2, 16});
    Frames(5);
    Position(true);
    Sense();
    EXPECT_EQ(activity.load(), 0u) << "status polls and audio play leave the LED dark";
    // Reading the disc: one count per block through the data register
    EXPECT_EQ(Command({0x28, 0, 0, 0, 0, 1, 0, 0, 2}).size(), 4096u);
    EXPECT_EQ(activity.load(), 2u);
    EXPECT_EQ(Command({0xBE, 0x04, 0, 0, 0, 8, 0, 0, 1, 0x10, 0}).size(), 2352u);  // READ CD of audio: data for the host
    EXPECT_EQ(activity.load(), 4u) << "2352 bytes: two pieces (2048 + 304)";
    _cd->SetActivityCounter(nullptr);
}

/// endregion </Data tracks, multisession, the activity LED>

TEST_F(AtapiCdromAudio_Test, SprinterCdplayerFlxPlaysFromTheFirstTrack)
{
    // The Sprinter's Flex Navigator plugin CDPLAYER.FLX (Shaos, 2002, "1.0 beta1"; disassembly in
    // docs/disasm/software/sprinter/cdplayer-flx/): IDENTIFY DEVICE (aborted: the signature), IDENTIFY
    // PACKET DEVICE, then "Play CD from first track" = PLAY AUDIO MSF 00:02:00 - 80:00:74 (its own
    // "to the end of any disc") without reading the status afterwards. MMC-3 checks only the start:
    // the drive plays from track 1 to the audio session's lead-out. Before the fix the end past the
    // lead-out was refused (LBA OUT OF RANGE) and nothing ever played
    std::string error;
    UseDisc(CdImageFormats::Open(cdtest::WriteMusicDisc(_folder->Path(), 3, 4, 300), &error));
    const cdtest::MusicDiscLayout l = cdtest::MusicLayoutOf(3, 4, 300, cdtest::MusicLayout::Enhanced);
    _channel.WriteRegister(DeviceHead, 0xA0);
    _channel.WriteRegister(StatusCommand, Command::Identify);
    EXPECT_EQ(Status() & Status::ERR, Status::ERR);
    _channel.WriteRegister(StatusCommand, Command::IdentifyPacket);
    for (int i = 0; i < 256; i++)
        _channel.ReadData();
    SendPacket({0x47, 0x00, 0x00, 0x00, 0x02, 0x00, 0x50, 0x00, 0x4A, 0x00, 0x00, 0x00}, 0xEB14);
    EXPECT_TRUE(Completed()) << "plays: no CHECK CONDITION";
    EXPECT_EQ(_cd->Audio().Status(), CdAudioStatus::Playing);
    EXPECT_EQ(_cd->Audio().HeadLba(), 0u) << "track 1";
    EXPECT_EQ(_cd->Audio().State().endLba, l.audioLeadOut) << "through the last audio track";
    // FF:FF:FF as the end (another player's "to the end"): the same
    SendPacket({0x47, 0, 0, 0, 2, 0, 0xFF, 0xFF, 0xFF});
    EXPECT_TRUE(Completed());
    EXPECT_EQ(_cd->Audio().State().endLba, l.audioLeadOut);

    // The older mixed-mode disc (track 1 = data): the start is in the data track - refused per MMC-3
    // (05h / 64h). The plugin assumes an audio track 1 and does not look at the error: nothing plays,
    // as on a real drive
    UseDisc(CdImageFormats::Open(cdtest::WriteMusicDisc(_folder->Path(), 2, 4, 300, cdtest::MusicLayout::Mixed), &error));
    SendPacket({0x47, 0x00, 0x00, 0x00, 0x02, 0x00, 0x50, 0x00, 0x4A, 0x00, 0x00, 0x00}, 0xEB14);
    ExpectCheck(AtapiCdrom::kSenseIllegalRequest, AtapiCdrom::kAscIllegalModeForTrack);
}
