#include "loader_z80_fuzzing_test.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "_helpers/test_path_helper.h"

#include <random>
#include <fstream>
#include <cstdio>  // for std::remove

/// region <SetUp / TearDown>

void LoaderZ80_Fuzzing_Test::SetUp()
{
    // Instantiate emulator with minimal logging to avoid flooding output during fuzzing
    _context = new EmulatorContext(LoggerLevel::LogError);

    _cpu = new Core(_context);
    if (_cpu->Init())
    {
        _cpu->GetMemory()->DefaultBanksFor48k();
    }
    else
    {
        throw std::runtime_error("Unable to SetUp LoaderZ80_Fuzzing_Test");
    }
}

void LoaderZ80_Fuzzing_Test::TearDown()
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

/// region <Helper Methods>

void LoaderZ80_Fuzzing_Test::createRandomFile(const std::string& path, size_t size)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    std::ofstream file(path, std::ios::binary);
    for (size_t i = 0; i < size; ++i)
    {
        uint8_t byte = static_cast<uint8_t>(dis(gen));
        file.write(reinterpret_cast<const char*>(&byte), 1);
    }
    file.close();
}

void LoaderZ80_Fuzzing_Test::createCorruptedFile(const std::string& path, const std::string& sourceFile, size_t corruptionCount)
{
    // Copy source file
    std::ifstream src(sourceFile, std::ios::binary);
    std::ofstream dst(path, std::ios::binary);
    dst << src.rdbuf();
    src.close();
    dst.close();

    // Get file size
    size_t fileSize = FileHelper::GetFileSize(path);
    if (fileSize == 0) return;

    // Corrupt random bytes
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> posDis(0, static_cast<int>(fileSize - 1));
    std::uniform_int_distribution<> byteDis(0, 255);

    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    for (size_t i = 0; i < corruptionCount && i < fileSize; ++i)
    {
        size_t pos = posDis(gen);
        uint8_t corruptByte = static_cast<uint8_t>(byteDis(gen));
        file.seekp(pos);
        file.write(reinterpret_cast<const char*>(&corruptByte), 1);
    }
    file.close();
}

/// endregion </Helper Methods>

/// region <Fuzzing Tests>

TEST_F(LoaderZ80_Fuzzing_Test, randomData_SmallSizes)
{
    // Test random data at various small sizes
    std::vector<size_t> testSizes = {0, 1, 5, 10, 15, 29, 30, 31, 50, 54, 55, 85, 86, 87, 100};

    for (size_t size : testSizes)
    {
        std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_random_" + std::to_string(size) + ".z80");
        createRandomFile(testPath, size);

        LoaderZ80CUT loader(_context, testPath);
        
        // Should not crash - validate may pass or fail depending on random data
        bool validateResult = loader.validate();
        
        if (validateResult)
        {
            // If validation passes, load should also not crash
            bool loadResult = loader.load();
            (void)loadResult;  // Don't care about result, just that it doesn't crash
        }

        // Clean up
        std::remove(testPath.c_str());
    }
}

TEST_F(LoaderZ80_Fuzzing_Test, randomData_MediumSizes)
{
    // Test random data at medium sizes (around typical snapshot sizes)
    std::vector<size_t> testSizes = {500, 1000, 2000, 5000, 10000, 20000, 49182};

    for (size_t size : testSizes)
    {
        std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_medium_" + std::to_string(size) + ".z80");
        createRandomFile(testPath, size);

        LoaderZ80CUT loader(_context, testPath);
        
        bool validateResult = loader.validate();
        if (validateResult)
        {
            bool loadResult = loader.load();
            (void)loadResult;
        }

        std::remove(testPath.c_str());
    }
}

TEST_F(LoaderZ80_Fuzzing_Test, randomData_LargeSizes)
{
    // Test random data at large sizes (beyond typical snapshots)
    std::vector<size_t> testSizes = {100000, 500000, 1000000};

    for (size_t size : testSizes)
    {
        std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_large_" + std::to_string(size) + ".z80");
        createRandomFile(testPath, size);

        LoaderZ80CUT loader(_context, testPath);
        
        bool validateResult = loader.validate();
        if (validateResult)
        {
            bool loadResult = loader.load();
            (void)loadResult;
        }

        std::remove(testPath.c_str());
    }
}

TEST_F(LoaderZ80_Fuzzing_Test, corruptedValidFiles_LightCorruption)
{
    // Take valid files and corrupt a few bytes
    std::vector<std::string> validFiles = {
        "loaders/z80/newbench.z80",
        "loaders/z80/dizzyx.z80"
    };

    for (const auto& validFile : validFiles)
    {
        std::string sourcePath = TestPathHelper::GetTestDataPath(validFile);
        std::string corruptPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_corrupt_light.z80");

        // Corrupt 1-5 random bytes
        for (size_t corruptCount = 1; corruptCount <= 5; ++corruptCount)
        {
            createCorruptedFile(corruptPath, sourcePath, corruptCount);

            LoaderZ80CUT loader(_context, corruptPath);
            
            bool validateResult = loader.validate();
            if (validateResult)
            {
                bool loadResult = loader.load();
                (void)loadResult;
            }

            std::remove(corruptPath.c_str());
        }
    }
}

TEST_F(LoaderZ80_Fuzzing_Test, corruptedValidFiles_HeavyCorruption)
{
    // Take valid files and corrupt many bytes
    std::vector<std::string> validFiles = {
        "loaders/z80/newbench.z80"
    };

    for (const auto& validFile : validFiles)
    {
        std::string sourcePath = TestPathHelper::GetTestDataPath(validFile);
        std::string corruptPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_corrupt_heavy.z80");

        // Corrupt 10%, 25%, 50% of bytes
        size_t fileSize = FileHelper::GetFileSize(sourcePath);
        std::vector<size_t> corruptCounts = {fileSize / 10, fileSize / 4, fileSize / 2};

        for (size_t corruptCount : corruptCounts)
        {
            createCorruptedFile(corruptPath, sourcePath, corruptCount);

            LoaderZ80CUT loader(_context, corruptPath);
            
            bool validateResult = loader.validate();
            if (validateResult)
            {
                bool loadResult = loader.load();
                (void)loadResult;
            }

            std::remove(corruptPath.c_str());
        }
    }
}

TEST_F(LoaderZ80_Fuzzing_Test, malformedHeaders_AllZeros)
{
    // File with all zeros
    std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_zeros.z80");
    
    std::ofstream file(testPath, std::ios::binary);
    for (size_t i = 0; i < 5000; ++i)
    {
        uint8_t zero = 0;
        file.write(reinterpret_cast<const char*>(&zero), 1);
    }
    file.close();

    LoaderZ80CUT loader(_context, testPath);
    bool validateResult = loader.validate();
    if (validateResult)
    {
        bool loadResult = loader.load();
        (void)loadResult;
    }

    std::remove(testPath.c_str());
}

TEST_F(LoaderZ80_Fuzzing_Test, malformedHeaders_AllOnes)
{
    // File with all 0xFF
    std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_ones.z80");
    
    std::ofstream file(testPath, std::ios::binary);
    for (size_t i = 0; i < 5000; ++i)
    {
        uint8_t ones = 0xFF;
        file.write(reinterpret_cast<const char*>(&ones), 1);
    }
    file.close();

    LoaderZ80CUT loader(_context, testPath);
    bool validateResult = loader.validate();
    if (validateResult)
    {
        bool loadResult = loader.load();
        (void)loadResult;
    }

    std::remove(testPath.c_str());
}

TEST_F(LoaderZ80_Fuzzing_Test, malformedHeaders_RepeatingPatterns)
{
    // Test various repeating patterns
    std::vector<uint8_t> patterns = {0xAA, 0x55, 0xDE, 0xAD, 0xBE, 0xEF};

    for (uint8_t pattern : patterns)
    {
        std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_pattern.z80");
        
        std::ofstream file(testPath, std::ios::binary);
        for (size_t i = 0; i < 5000; ++i)
        {
            file.write(reinterpret_cast<const char*>(&pattern), 1);
        }
        file.close();

        LoaderZ80CUT loader(_context, testPath);
        bool validateResult = loader.validate();
        if (validateResult)
        {
            bool loadResult = loader.load();
            (void)loadResult;
        }

        std::remove(testPath.c_str());
    }
}

TEST_F(LoaderZ80_Fuzzing_Test, extremeValues_MaxExtendedHeaderLen)
{
    // Create file with max extended header length (65535)
    std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_maxext.z80");
    
    std::ofstream file(testPath, std::ios::binary);
    
    // Write v1 header with PC=0 (indicates v2+)
    for (size_t i = 0; i < 6; ++i) file.put(0x00);
    file.put(0x00); file.put(0x00);  // PC = 0
    for (size_t i = 8; i < 30; ++i) file.put(0x00);
    
    // Extended header length = 0xFFFF
    file.put(0xFF); file.put(0xFF);
    
    // Some random data
    for (size_t i = 0; i < 100; ++i) file.put(0xAA);
    file.close();

    LoaderZ80CUT loader(_context, testPath);
    bool validateResult = loader.validate();
    EXPECT_FALSE(validateResult) << "Max extended header length should be rejected";

    std::remove(testPath.c_str());
}

TEST_F(LoaderZ80_Fuzzing_Test, extremeValues_HugeMemoryBlockSizes)
{
    // Create valid v2 header but with huge memory block sizes
    std::string testPath = TestPathHelper::GetTestDataPath("loaders/z80/fuzz_hugeblock.z80");
    
    std::ofstream file(testPath, std::ios::binary);
    
    // Write v1 header with PC=0
    for (size_t i = 0; i < 6; ++i) file.put(0x01);
    file.put(0x00); file.put(0x00);  // PC = 0
    for (size_t i = 8; i < 30; ++i) file.put(0x01);
    
    // Extended header length = 23 (v2)
    file.put(0x17); file.put(0x00);
    
    // newPC, model, etc (23 bytes)
    for (size_t i = 0; i < 23; ++i) file.put(0x01);
    
    // Memory block descriptor with huge size
    file.put(0xFF); file.put(0xFE);  // compressedSize = 0xFEFF (65279)
    file.put(0x04);                  // page = 4
    
    // Only provide small amount of data (should trigger bounds check)
    for (size_t i = 0; i < 100; ++i) file.put(0xED);
    file.close();

    LoaderZ80CUT loader(_context, testPath);
    loader.validate();
    bool loadResult = loader.load();
    // Should handle gracefully (may pass or fail, but shouldn't crash)
    (void)loadResult;

    std::remove(testPath.c_str());
}

/// endregion </Fuzzing Tests>
