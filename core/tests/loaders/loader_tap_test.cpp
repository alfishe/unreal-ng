#include "pch.h"

#include "loader_tap_test.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void LoaderTAP_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _context = new EmulatorContext(LoggerLevel::LogError);

    _cpu = new Core(_context);
    if (_cpu->Init())
    {

        // Use Spectrum48K / Pentagon memory layout
        _cpu->GetMemory()->DefaultBanksFor48k();
    }
    else
    {
        throw std::runtime_error("Unable to SetUp LoaderSNA test(s)");
    }
}

void LoaderTAP_Test::TearDown()
{
    if (_cpu != nullptr)
    {
        delete _cpu;
        _cpu = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(LoaderTAP_Test, read)
{
    static std::string testTapePath = "../../../tests/loaders/tap/action.tap";
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testTapePath);

    LoaderTAPCUT loader(_context, testTapePath);
    bool result = loader.read();
    EXPECT_EQ(result, true);
}

TEST_F(LoaderTAP_Test, getBlockChecksum)
{
    LoaderTAPCUT loader(_context, "");

    /// region <Positive cases>

    // Valid TAP blocks
    std::vector<std::vector<uint8_t>> testDataBlocks =
    {
        { 0x00, 0x03, 0x52, 0x4F, 0x4D, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0xF1 },
        { 0xFF, 0xF3, 0xAF, 0xA3 }
    };

    // Traverse across all test data blocks
    size_t counter = 0;
    std::for_each(testDataBlocks.begin(), testDataBlocks.end(), [&counter, &loader](std::vector<uint8_t>& dataBlock)
    {
        uint8_t reference = dataBlock.back();
        uint8_t checksum = loader.getBlockChecksum(dataBlock);

        if (checksum != reference)
        {
            FAIL() << "For the testDataBlocks[" << counter << "] expected checksum: " << reference << " but found: " << checksum;
        }

        counter++;
    });

    /// endregion </Positive cases>

    /// region <Negative cases>

    // Valid TAP blocks with incorrect checksums (last byte)
    testDataBlocks =
    {
        { 0x00, 0x03, 0x52, 0x4F, 0x4D, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0xB8 },
        { 0xFF, 0xF3, 0xAF, 0xDE }
    };

    // Traverse across all test data blocks
    counter = 0;
    std::for_each(testDataBlocks.begin(), testDataBlocks.end(), [&counter, &loader](std::vector<uint8_t>& dataBlock)
    {
        uint8_t reference = dataBlock.back();
        uint8_t checksum = loader.getBlockChecksum(dataBlock);

        if (checksum == reference)
        {
            FAIL() << "For the testDataBlocks[" << counter << "] falsely positive result for checksum: " << reference << " It must not match";
        }

        counter++;
    });

    /// endregion </Negative cases>
}

TEST_F(LoaderTAP_Test, isBlockValid)
{
    LoaderTAPCUT loader(_context, "");

    /// region <Positive cases>

    // Valid TAP blocks
    std::vector<std::vector<uint8_t>> testDataBlocks =
    {
        { 0x00, 0x03, 0x52, 0x4F, 0x4D, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0xF1 },
        { 0xFF, 0xF3, 0xAF, 0xA3 }
    };

    // Traverse across all test data blocks
    size_t counter = 0;
    std::for_each(testDataBlocks.begin(), testDataBlocks.end(), [&counter, &loader](std::vector<uint8_t>& dataBlock)
    {
        bool reference = true;
        bool result = loader.isBlockValid(dataBlock);

        if (result != reference)
        {
            FAIL() << "For the testDataBlocks[" << counter << "] expected checksum: " << reference << " but found: " << result;
        }

        counter++;
    });

    /// endregion </Positive cases>

    /// region <Negative cases>

    // Valid TAP blocks with incorrect checksums (last byte)
    testDataBlocks =
    {
        { 0x00, 0x03, 0x52, 0x4F, 0x4D, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x80, 0xB8 },
        { 0xFF, 0xF3, 0xAF, 0xDE }
    };

    // Traverse across all test data blocks
    counter = 0;
    std::for_each(testDataBlocks.begin(), testDataBlocks.end(), [&counter, &loader](std::vector<uint8_t>& dataBlock)
    {
        bool reference = true;
        bool result = loader.isBlockValid(dataBlock);

        if (result == reference)
        {
            FAIL() << "For the testDataBlocks[" << counter << "] falsely positive result for block validity: " << reference << " It must not match";
        }

        counter++;
    });

    /// endregion </Negative cases>
}

TEST_F(LoaderTAP_Test, readNextBlock)
{
    static std::string testTapePath = "../../../tests/loaders/tap/action.tap";
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testTapePath);
    const size_t referenceBlockCount = 6;

    LoaderTAPCUT loader(_context, testTapePath);
    std::vector<std::vector<uint8_t>> allBlocks;

    FILE* file = FileHelper::OpenFile(testTapePath);
    EXPECT_NE(file, nullptr);

    size_t blockCount = 0;
    while (true)
    {
        std::vector<uint8_t> block = loader.readNextBlock(file);

        if (block.size() == 0)
            break;

        allBlocks.push_back(block);

        blockCount++;
    }

    std::cout << loader.dumpBlocks(allBlocks);

    EXPECT_EQ(blockCount, referenceBlockCount);

    FileHelper::CloseFile(file);
}

TEST_F(LoaderTAP_Test, makeBlock)
{
    LoaderTAPCUT loader(_context, "");

    uint8_t data[] = { 0x00, 0x01, 0x02, 0xFF };
    std::vector<uint32_t> referenceResult =
    {
        // Pilot
        2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168,

        // Synchronization
       667, 735,

       // [0] - 0x00
       855, 855, 855, 855, 855, 855, 855, 855,
       855, 855, 855, 855, 855, 855, 855, 855,

       // [1] - 0x01
       855, 855, 855, 855, 855, 855, 855, 855,
       855, 855, 855, 855, 855, 855, 1710, 1710,

       // [2] - 0x02
       855, 855, 855, 855, 855, 855, 855, 855,
       855, 855, 855, 855, 1710, 1710, 855, 855,

       // [3] - 0xFF
       1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710,
       1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710,

       // Pause
       3500000
    };

    std::vector<uint32_t> result = loader.makeStandardBlock(data, sizeof(data), 2168, 667, 735, 855, 1710, 10, 1000);

    // Standard data block has long pilot (8064 or 3220 periods) but reference vector will be too long for that
    //std::vector<uint32_t> result = loader.makeStandardBlock(data, sizeof(data), 2168, 667, 735, 855, 1710, true ? 8064 : 3220, 1000);

    EXPECT_EQ(result, referenceResult);

    // region <Debug print>

    /*
    std::stringstream ss;

    ss << "Vector len: " << result.size() << std::endl;
    std::for_each(result.begin(), result.end(), [&ss](uint32_t value)
    {
        ss << value << ", ";
    });

    ss << std::endl;
    std::cout << ss.str();
    */

    // endregion </Debug print>
}