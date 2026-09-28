// SD card in SPI mode (neogs-tdd.md §5.5)

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/sessionwritemap.h"

namespace
{
/// A scratch image file, removed when the object goes (keep it alive while a
/// card has it open)
class ImageFile
{
public:
    explicit ImageFile(std::string path) : _path(std::move(path)) {}
    ~ImageFile()
    {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
    }
    ImageFile(const ImageFile&) = delete;
    ImageFile& operator=(const ImageFile&) = delete;
    ImageFile(ImageFile&& other) noexcept : _path(std::move(other._path)) { other._path.clear(); }
    operator const std::string&() const { return _path; }

private:
    std::string _path;
};

/// A scratch image: block b is filled with (b & 0xFF), size in blocks
ImageFile makeImage(size_t blocks, size_t extraBytes = 0)
{
    const auto path = TestPathHelper::GetUniqueTestScratchPath("sdcard.img");
    std::ofstream out(path, std::ios::binary);
    for (size_t b = 0; b < blocks; b++)
    {
        std::vector<char> block(SdCardSpi::BLOCK, static_cast<char>(b & 0xFF));
        out.write(block.data(), static_cast<std::streamsize>(block.size()));
    }
    std::vector<char> tail(extraBytes, 0x77);
    out.write(tail.data(), static_cast<std::streamsize>(tail.size()));
    return ImageFile(path);
}

struct Host
{
    SdCardSpi& card;

    uint8_t xfer(uint8_t b) { return card.exchange(b); }

    /// Send a command (CRC from the table for CMD0/CMD8, #FF otherwise) and
    /// return the first non-#FF byte within 16 polls
    uint8_t command(uint8_t index, uint32_t arg, uint8_t crc = 0xFF)
    {
        xfer(static_cast<uint8_t>(0x40 | index));
        xfer(static_cast<uint8_t>(arg >> 24));
        xfer(static_cast<uint8_t>(arg >> 16));
        xfer(static_cast<uint8_t>(arg >> 8));
        xfer(static_cast<uint8_t>(arg));
        xfer(crc);
        for (int i = 0; i < 16; i++)
        {
            const uint8_t r = xfer(0xFF);
            if (r != 0xFF)
                return r;
        }
        return 0xFF;
    }

    /// The drivers' init sequence (ngs_sd_drv.a80): CMD0, CMD8, ACMD41 loop
    /// with HCS, CMD59 off, CMD16 512
    bool init()
    {
        if (command(0, 0, 0x95) != 0x01)
            return false;
        if (command(8, 0x1AA, 0x87) != 0x01)
            return false;
        for (int i = 0; i < 4; i++)
            xfer(0xFF); // rest of R7
        for (int tries = 0; tries < 100; tries++)
        {
            command(55, 0);
            if (command(41, 0x40000000) == 0x00)
                return command(59, 0) == 0x00 && command(16, 512) == 0x00;
        }
        return false;
    }

    /// Read the data block that follows a read command's R1
    bool readData(std::vector<uint8_t>& out)
    {
        for (int i = 0; i < 64; i++)
        {
            if (xfer(0xFF) == 0xFE)
            {
                out.resize(SdCardSpi::BLOCK);
                for (auto& b : out)
                    b = xfer(0xFF);
                xfer(0xFF); // CRC
                xfer(0xFF);
                return true;
            }
        }
        return false;
    }

    uint8_t writeData(uint8_t token, uint8_t fill)
    {
        xfer(0xFF);
        xfer(token);
        for (size_t i = 0; i < SdCardSpi::BLOCK; i++)
            xfer(fill);
        xfer(0xFF);
        xfer(0xFF); // CRC (ignored)
        uint8_t response = 0xFF;
        for (int i = 0; i < 8 && response == 0xFF; i++)
            response = xfer(0xFF);
        int busy = 0;
        while (xfer(0xFF) != 0xFF && busy < 1000)
            busy++;
        return static_cast<uint8_t>(response & 0x1F);
    }
};
} // namespace

TEST(SdCardSpi_Test, NoCardOrDeselectedReadsFF)
{
    const ImageFile image = makeImage(16);
    SdCardSpi card;
    EXPECT_EQ(card.exchange(0x40), 0xFF);
    ASSERT_TRUE(card.open(image));
    Host host{card};
    EXPECT_EQ(host.command(0, 0, 0x95), 0xFF) << "not selected";
}

TEST(SdCardSpi_Test, InitSequenceSdscAndSdhc)
{
    for (auto type : {SdCardSpi::Type::Auto, SdCardSpi::Type::SDHC})
    {
        const ImageFile image = makeImage(64);
        SdCardSpi card;
        ASSERT_TRUE(card.open(image, SdCardSpi::WriteMode::Session, type));
        card.select(true);
        Host host{card};
        ASSERT_TRUE(host.init());
        EXPECT_TRUE(card.initialized());

        EXPECT_EQ(host.command(58, 0), 0x00);
        const uint8_t ocr0 = host.xfer(0xFF);
        EXPECT_EQ(ocr0 & 0x80, 0x80) << "power-up done";
        EXPECT_EQ((ocr0 & 0x40) != 0, type == SdCardSpi::Type::SDHC) << "CCS";
    }
}

TEST(SdCardSpi_Test, CrcCheckedOnlyForCmd0AndCmd8UntilCmd59)
{
    const ImageFile image = makeImage(16);
    SdCardSpi card;
    ASSERT_TRUE(card.open(image));
    card.select(true);
    Host host{card};
    EXPECT_EQ(host.command(0, 0, 0xFF), 0xFF) << "a bad CRC on CMD0 before SPI mode: no answer";
    EXPECT_EQ(host.command(16, 512, 0xFF), 0xFF) << "nothing but CMD0 before SPI mode";
    EXPECT_EQ(host.command(0, 0, 0x95), 0x01);
    EXPECT_EQ(host.command(8, 0x1AA, 0x00) & 0x08, 0x08) << "CMD8 always checks its CRC";
    EXPECT_EQ(host.command(16, 512, 0x00), 0x01) << "CRC off: any CRC byte";
    EXPECT_EQ(host.command(59, 1, 0x00), 0x01) << "CRC on (CMD59 itself still unchecked)";
    EXPECT_EQ(host.command(16, 512, 0x00) & 0x08, 0x08) << "now every command is checked";
}

TEST(SdCardSpi_Test, UnknownCommandsAreIllegal)
{
    const ImageFile image = makeImage(16);
    SdCardSpi card;
    ASSERT_TRUE(card.open(image));
    card.select(true);
    Host host{card};
    ASSERT_TRUE(host.init());
    EXPECT_EQ(host.command(1, 0) & 0x04, 0x04) << "CMD1 (MMC) is not supported";
}

TEST(SdCardSpi_Test, SingleAndMultiBlockReadWithStop)
{
    for (auto type : {SdCardSpi::Type::SDSC, SdCardSpi::Type::SDHC})
    {
        const ImageFile image = makeImage(64);
        SdCardSpi card;
        ASSERT_TRUE(card.open(image, SdCardSpi::WriteMode::Session, type));
        card.select(true);
        Host host{card};
        ASSERT_TRUE(host.init());
        const uint32_t scale = type == SdCardSpi::Type::SDHC ? 1 : 512; // block vs byte addresses

        std::vector<uint8_t> data;
        ASSERT_EQ(host.command(17, 5 * scale), 0x00);
        ASSERT_TRUE(host.readData(data));
        EXPECT_EQ(data[0], 5);
        EXPECT_EQ(data[511], 5);

        ASSERT_EQ(host.command(18, 10 * scale), 0x00);
        for (uint8_t b = 10; b < 14; b++)
        {
            ASSERT_TRUE(host.readData(data));
            EXPECT_EQ(data[100], b);
        }
        EXPECT_EQ(host.command(12, 0), 0x00) << "CMD12 stops the stream";
        for (int i = 0; i < 8; i++)
            host.xfer(0xFF);
        ASSERT_EQ(host.command(17, 20 * scale), 0x00) << "commands work again after the stop";
        ASSERT_TRUE(host.readData(data));
        EXPECT_EQ(data[0], 20);
    }
}

TEST(SdCardSpi_Test, WritesSessionPersistAndOff)
{
    const ImageFile image = makeImage(32);
    {
        SdCardSpi card;
        ASSERT_TRUE(card.open(image, SdCardSpi::WriteMode::Session));
        card.select(true);
        Host host{card};
        ASSERT_TRUE(host.init());
        ASSERT_EQ(host.command(24, 3 * 512), 0x00);
        EXPECT_EQ(host.writeData(0xFE, 0xAB), 0x05);
        std::vector<uint8_t> data;
        ASSERT_EQ(host.command(17, 3 * 512), 0x00);
        ASSERT_TRUE(host.readData(data));
        EXPECT_EQ(data[7], 0xAB) << "session overlay is read back";
    }
    {
        SdCardSpi card;
        ASSERT_TRUE(card.open(image, SdCardSpi::WriteMode::Session));
        uint8_t block[512];
        ASSERT_TRUE(card.readBlock(3, block));
        EXPECT_EQ(block[7], 3) << "session writes never reach the file";
    }
    {
        SdCardSpi card;
        ASSERT_TRUE(card.open(image, SdCardSpi::WriteMode::Persist));
        card.select(true);
        Host host{card};
        ASSERT_TRUE(host.init());
        ASSERT_EQ(host.command(25, 4 * 512), 0x00);
        EXPECT_EQ(host.writeData(0xFC, 0x11), 0x05);
        EXPECT_EQ(host.writeData(0xFC, 0x22), 0x05);
        host.xfer(0xFD); // stop token
        for (int i = 0; i < 100 && host.xfer(0xFF) != 0xFF; i++)
        {
        }
        EXPECT_EQ(card.blocksWritten(), 2u);
    }
    {
        SdCardSpi card;
        ASSERT_TRUE(card.open(image, SdCardSpi::WriteMode::Session));
        uint8_t block[512];
        card.readBlock(4, block);
        EXPECT_EQ(block[0], 0x11) << "persist wrote through";
        card.readBlock(5, block);
        EXPECT_EQ(block[0], 0x22);
    }
    {
        SdCardSpi card;
        ASSERT_TRUE(card.open(image, SdCardSpi::WriteMode::Off));
        card.select(true);
        Host host{card};
        ASSERT_TRUE(host.init());
        ASSERT_EQ(host.command(24, 6 * 512), 0x00);
        EXPECT_EQ(host.writeData(0xFE, 0x99), 0x0D) << "write error data response";
        EXPECT_EQ(host.command(13, 0), 0x00);
        EXPECT_EQ(host.xfer(0xFF) & 0x20, 0x20) << "WP_VIOLATION in the R2 status";
    }
}

TEST(SdCardSpi_Test, WriteListenerHearsEveryAcceptedBlock)
{
    const ImageFile image = makeImage(32);
    SdCardSpi card;
    ASSERT_TRUE(card.open(image, SdCardSpi::WriteMode::Session));
    std::vector<uint64_t> heard;
    card.setWriteListener([&heard](uint64_t block) { heard.push_back(block); });
    card.select(true);
    Host host{card};
    ASSERT_TRUE(host.init());
    ASSERT_EQ(host.command(24, 3 * 512), 0x00);
    host.writeData(0xFE, 0x01);
    ASSERT_EQ(host.command(25, 7 * 512), 0x00);
    host.writeData(0xFC, 0x02);
    host.writeData(0xFC, 0x03);
    host.xfer(0xFD);
    EXPECT_EQ(heard, (std::vector<uint64_t>{3, 7, 8}));
}

TEST(SdCardSpi_Test, StateRoundTripMidStreamContinuesIdentically)
{
    const ImageFile image = makeImage(32);
    SdCardSpi a;
    SdCardSpi b;
    ASSERT_TRUE(a.open(image));
    ASSERT_TRUE(b.open(image));
    a.select(true);
    Host host{a};
    ASSERT_TRUE(host.init());
    ASSERT_EQ(host.command(18, 5 * 512), 0x00);
    for (int i = 0; i < 300; i++)
        host.xfer(0xFF); // into the first block of a multi-block read

    std::vector<uint8_t> state(SdCardSpi::STATE_SIZE);
    a.saveState(state.data());
    b.loadState(state.data());
    std::vector<uint8_t> again(SdCardSpi::STATE_SIZE);
    b.saveState(again.data());
    EXPECT_EQ(again, state);
    for (int i = 0; i < 2000; i++)
        ASSERT_EQ(b.exchange(0xFF), a.exchange(0xFF)) << "byte " << i;
    for (const uint8_t byte : {0x4C, 0x00, 0x00, 0x00, 0x00, 0xFF}) // CMD12
        ASSERT_EQ(b.exchange(byte), a.exchange(byte));
    for (int i = 0; i < 100; i++)
        ASSERT_EQ(b.exchange(0xFF), a.exchange(0xFF)) << "after CMD12, byte " << i;
    EXPECT_EQ(b.blocksRead(), a.blocksRead());
}

TEST(SdCardSpi_Test, OddSizedImageIsPadded)
{
    const ImageFile image = makeImage(2, 100);
    SdCardSpi card;
    ASSERT_TRUE(card.open(image));
    EXPECT_EQ(card.sizeBytes(), 3u * 512u);
    uint8_t block[512];
    ASSERT_TRUE(card.readBlock(2, block));
    EXPECT_EQ(block[99], 0x77);
    EXPECT_EQ(block[100], 0x00);
    EXPECT_FALSE(card.readBlock(3, block));
}

/// Any medium, not only a file (tdd-storage-sd-ide-cd.md §1 S1)
TEST(SdCardSpi_Test, InsertedMemoryMediumServesReads)
{
    auto disk = std::make_unique<MemoryDisk>(64);
    disk->Data()[9 * 512 + 3] = 0x5C;
    SdCardSpi card;
    ASSERT_TRUE(card.insert(std::move(disk), SdCardSpi::WriteMode::Persist));
    EXPECT_TRUE(card.present());
    EXPECT_EQ(card.path(), "memory disk");
    EXPECT_EQ(card.sizeBytes(), 64u * 512u);
    EXPECT_EQ(card.sessionWrites(), nullptr) << "Persist writes to the medium itself";

    card.select(true);
    Host host{card};
    ASSERT_TRUE(host.init());
    std::vector<uint8_t> data;
    ASSERT_EQ(host.command(17, 9 * 512), 0x00);
    ASSERT_TRUE(host.readData(data));
    EXPECT_EQ(data[3], 0x5C);

    card.close();
    EXPECT_FALSE(card.present());
    EXPECT_EQ(card.exchange(0xFF), 0xFF) << "an empty slot reads #FF";
}

/// Persist onto a read-only medium: the card reports a write error, it does
/// not pretend the data was stored
TEST(SdCardSpi_Test, PersistOnReadOnlyMediumAnswersWriteError)
{
    SdCardSpi card;
    ASSERT_TRUE(card.insert(std::make_unique<MemoryDisk>(16, /*writable*/ false), SdCardSpi::WriteMode::Persist));
    card.select(true);
    Host host{card};
    ASSERT_TRUE(host.init());
    ASSERT_EQ(host.command(24, 2 * 512), 0x00);
    EXPECT_EQ(host.writeData(0xFE, 0x42), 0x0D);
    EXPECT_EQ(card.blocksWritten(), 0u);
}

/// Session writes live in a SessionWriteMap the owner can count and export (S3)
TEST(SdCardSpi_Test, SessionWritesAreExportable)
{
    const ImageFile image = makeImage(8);
    SdCardSpi card;
    ASSERT_TRUE(card.open(image, SdCardSpi::WriteMode::Session));
    ASSERT_NE(card.sessionWrites(), nullptr);
    card.select(true);
    Host host{card};
    ASSERT_TRUE(host.init());
    ASSERT_EQ(host.command(24, 6 * 512), 0x00);
    ASSERT_EQ(host.writeData(0xFE, 0x3C), 0x05);
    EXPECT_EQ(card.sessionWrites()->ChangedSectors(), 1u);

    const ImageFile exported(TestPathHelper::GetUniqueTestScratchPath("sdcard-export.img"));
    ASSERT_TRUE(card.sessionWrites()->ExportTo(exported));
    SdCardSpi check;
    ASSERT_TRUE(check.open(exported));
    uint8_t block[512];
    ASSERT_TRUE(check.readBlock(6, block));
    EXPECT_EQ(block[0], 0x3C);
    ASSERT_TRUE(check.readBlock(5, block));
    EXPECT_EQ(block[0], 5);
}

/// The owner hears every command once SPI mode is on (the TTD storage rule)
TEST(SdCardSpi_Test, CommandListenerHearsCommandsAfterSpiMode)
{
    const ImageFile image = makeImage(16);
    SdCardSpi card;
    ASSERT_TRUE(card.open(image));
    std::vector<uint8_t> heard;
    card.setCommandListener([&heard](uint8_t index) { heard.push_back(index); });
    card.select(true);
    Host host{card};
    host.command(16, 512);  // before CMD0: ignored by the card
    EXPECT_TRUE(heard.empty());
    ASSERT_EQ(host.command(0, 0, 0x95), 0x01);
    host.command(8, 0x1AA, 0x87);
    EXPECT_EQ(heard, (std::vector<uint8_t>{0, 8}));
}
