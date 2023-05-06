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

TEST_F(LoaderTAP_Test, loadTAP)
{
    static std::string testTapePath = "../../../tests/loaders/tap/action.tap";
    std::string absoluteSnapshotPath = FileHelper::AbsolutePath(testTapePath);
    constexpr size_t referenceBlockNumber = 6;
    const std::vector<size_t> referenceBlockSizes = { 19, 167, 19, 4338, 19, 27082 };

    LoaderTAPCUT loader(_context);
    vector<TapeBlock> result = loader.loadTAP(testTapePath);

    // Check block count
    EXPECT_EQ(result.size(), referenceBlockNumber);

    // Check all block data sizes
    for (int i = 0; i < result.size(); i++)
    {
        size_t blockSize = result[i].data.size();
        size_t referenceBlockSize = referenceBlockSizes[i];
        EXPECT_EQ(blockSize, referenceBlockSize) << "Invalid data content length for block #: " << i;
    }
}

TEST_F(LoaderTAP_Test, getBlockChecksum)
{
    LoaderTAPCUT loader(_context);

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
    LoaderTAPCUT loader(_context);

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

    LoaderTAPCUT loader(_context);
    std::vector<TapeBlock> allBlocks;

    FILE* file = FileHelper::OpenFile(testTapePath);
    EXPECT_NE(file, nullptr);

    size_t blockCount = 0;
    while (true)
    {
        TapeBlock block = loader.readNextBlock(file);

        if (block.data.size() == 0)
            break;

        allBlocks.push_back(block);

        blockCount++;
    }

    std::cout << loader.dumpBlocks(allBlocks);

    EXPECT_EQ(blockCount, referenceBlockCount);

    FileHelper::CloseFile(file);
}
