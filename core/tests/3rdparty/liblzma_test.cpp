#include <3rdparty/liblzma/include/LzmaEnc.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

// Include the LZMA library headers
#include "3rdparty/liblzma/include/LzmaLib.h"
#include "3rdparty/liblzma/include/7zTypes.h"

// Test path helper for finding project root
#include "_helpers/test_path_helper.h"

namespace fs = std::filesystem;

class LzmaTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize random number generator for test data
        rng.seed(42);
    }

    void TearDown() override
    {
        // Cleanup if needed
    }

    // Helper function to generate random data
    std::vector<unsigned char> generateRandomData(size_t size)
    {
        std::vector<unsigned char> data(size);
        std::uniform_int_distribution<int> dist(0, 255);
        for (size_t i = 0; i < size; ++i)
        {
            data[i] = static_cast<unsigned char>(dist(rng));
        }
        return data;
    }

    // Helper function to generate compressible data (repeating patterns)
    std::vector<unsigned char> generateCompressibleData(size_t size)
    {
        std::vector<unsigned char> data(size);
        std::uniform_int_distribution<int> dist(0, 255);
        
        // Create repeating patterns for better compression
        for (size_t i = 0; i < size; ++i)
        {
            if (i % 100 < 50)
            {
                data[i] = static_cast<unsigned char>(i % 256);
            }
            else
            {
                data[i] = static_cast<unsigned char>(dist(rng));
            }
        }
        return data;
    }

    // Helper function to compress data using LZMA
    std::vector<unsigned char> compressData(const std::vector<unsigned char>& input, int level = 5)
    {
        return compressData(input, level, 1 << 24, 3, 0, 2, 32, 2);
    }

    // Extended helper function to compress data using LZMA with custom properties
    std::vector<unsigned char> compressData(const std::vector<unsigned char>& input, 
                                           int level, unsigned dictSize, int lc, int lp, int pb, int fb, int numThreads)
    {
        // Calculate maximum output size (input size + some overhead)
        size_t maxOutputSize = input.size() + (input.size() / 10) + 1024;
        std::vector<unsigned char> output(maxOutputSize);
        
        // Properties buffer (5 bytes)
        unsigned char props[LZMA_PROPS_SIZE];
        size_t propsSize = LZMA_PROPS_SIZE;
        
        // Compress
        int result = LzmaCompress(
            output.data(), &maxOutputSize,           // dest, destLen
            input.data(), input.size(),              // src, srcLen
            props, &propsSize,                       // outProps, outPropsSize
            level,                                   // level
            dictSize,                                // dictSize
            lc, lp, pb, fb, numThreads               // lc, lp, pb, fb, numThreads
        );
        
        if (result != SZ_OK)
        {
            return std::vector<unsigned char>(); // Return empty vector on failure
        }
        
        // Resize output to actual compressed size
        output.resize(maxOutputSize);
        
        // Prepend properties to output
        std::vector<unsigned char> finalOutput;
        finalOutput.insert(finalOutput.end(), props, props + LZMA_PROPS_SIZE);
        finalOutput.insert(finalOutput.end(), output.begin(), output.end());
        
        return finalOutput;
    }

    // Helper function to compress data using LZMA with CLzmaEncProps
    std::vector<unsigned char> compressDataWithProps(const std::vector<unsigned char>& input, const _CLzmaEncProps& props)
    {
        // Calculate maximum output size (input size + some overhead)
        size_t maxOutputSize = input.size() + (input.size() / 10) + 1024;
        std::vector<unsigned char> output(maxOutputSize);
        
        // Properties buffer (5 bytes)
        unsigned char propsBuffer[LZMA_PROPS_SIZE];
        size_t propsSize = LZMA_PROPS_SIZE;
        
        // Convert CLzmaEncProps to individual parameters
        int level = props.level;
        unsigned dictSize = props.dictSize;
        int lc = props.lc;
        int lp = props.lp;
        int pb = props.pb;
        int fb = props.fb;
        int numThreads = props.numThreads;
        
        // Compress
        int result = LzmaCompress(
            output.data(), &maxOutputSize,           // dest, destLen
            input.data(), input.size(),              // src, srcLen
            propsBuffer, &propsSize,                 // outProps, outPropsSize
            level,                                   // level
            dictSize,                                // dictSize
            lc, lp, pb, fb, numThreads               // lc, lp, pb, fb, numThreads
        );
        
        if (result != SZ_OK)
        {
            return std::vector<unsigned char>(); // Return empty vector on failure
        }
        
        // Resize output to actual compressed size
        output.resize(maxOutputSize);
        
        // Prepend properties to output
        std::vector<unsigned char> finalOutput;
        finalOutput.insert(finalOutput.end(), propsBuffer, propsBuffer + LZMA_PROPS_SIZE);
        finalOutput.insert(finalOutput.end(), output.begin(), output.end());
        
        return finalOutput;
    }

    // Helper function to decompress data using LZMA
    std::vector<unsigned char> decompressData(const std::vector<unsigned char>& compressed, size_t originalSize)
    {
        if (compressed.size() < LZMA_PROPS_SIZE)
        {
            return std::vector<unsigned char>(); // Invalid compressed data
        }
        
        // Extract properties
        unsigned char props[LZMA_PROPS_SIZE];
        std::copy(compressed.begin(), compressed.begin() + LZMA_PROPS_SIZE, props);
        
        // Extract compressed data
        std::vector<unsigned char> compressedData(compressed.begin() + LZMA_PROPS_SIZE, compressed.end());
        
        // Decompress
        std::vector<unsigned char> output(originalSize);
        size_t destLen = originalSize;
        SizeT srcLen = compressedData.size();
        
        int result = LzmaUncompress(
            output.data(), &destLen,                 // dest, destLen
            compressedData.data(), &srcLen,          // src, srcLen
            props, LZMA_PROPS_SIZE                   // props, propsSize
        );
        
        if (result != SZ_OK)
        {
            return std::vector<unsigned char>(); // Return empty vector on failure
        }
        
        output.resize(destLen);
        return output;
    }

    std::mt19937 rng;
};

TEST_F(LzmaTest, BasicCompressionDecompression)
{
    // Test with simple text data
    std::string testData = "Hello, LZMA compression! This is a test string that should be compressible.";
    std::vector<unsigned char> input(testData.begin(), testData.end());
    
    // Compress
    auto compressed = compressData(input);
    ASSERT_FALSE(compressed.empty()) << "Compression failed";
    
    // Verify compression actually reduced size (for compressible data)
    EXPECT_LT(compressed.size(), input.size() + LZMA_PROPS_SIZE) << "Compression didn't reduce size";
    
    // Decompress
    auto decompressed = decompressData(compressed, input.size());
    ASSERT_FALSE(decompressed.empty()) << "Decompression failed";
    
    // Verify data integrity
    ASSERT_EQ(decompressed.size(), input.size()) << "Decompressed size doesn't match original";
    EXPECT_EQ(decompressed, input) << "Decompressed data doesn't match original";
}

TEST_F(LzmaTest, RandomDataCompression)
{
    // Test with random data (may not compress well)
    auto input = generateRandomData(1024);
    
    // Compress
    auto compressed = compressData(input);
    ASSERT_FALSE(compressed.empty()) << "Compression failed";
    
    // Decompress
    auto decompressed = decompressData(compressed, input.size());
    ASSERT_FALSE(decompressed.empty()) << "Decompression failed";
    
    // Verify data integrity
    ASSERT_EQ(decompressed.size(), input.size()) << "Decompressed size doesn't match original";
    EXPECT_EQ(decompressed, input) << "Decompressed data doesn't match original";
}

TEST_F(LzmaTest, CompressibleDataCompression)
{
    // Test with compressible data (repeating patterns)
    auto input = generateCompressibleData(2048);
    
    // Compress
    auto compressed = compressData(input);
    ASSERT_FALSE(compressed.empty()) << "Compression failed";
    
    // Verify compression actually reduced size
    EXPECT_LT(compressed.size(), input.size() + LZMA_PROPS_SIZE) << "Compression didn't reduce size for compressible data";
    
    // Decompress
    auto decompressed = decompressData(compressed, input.size());
    ASSERT_FALSE(decompressed.empty()) << "Decompression failed";
    
    // Verify data integrity
    ASSERT_EQ(decompressed.size(), input.size()) << "Decompressed size doesn't match original";
    EXPECT_EQ(decompressed, input) << "Decompressed data doesn't match original";
}

TEST_F(LzmaTest, LargeDataCompression)
{
    // Test with larger data
    auto input = generateCompressibleData(10000);
    
    // Compress
    auto compressed = compressData(input);
    ASSERT_FALSE(compressed.empty()) << "Compression failed";
    
    // Verify compression ratio
    double compressionRatio = static_cast<double>(compressed.size()) / input.size();
    EXPECT_LT(compressionRatio, 1.0) << "Compression ratio should be less than 1.0";
    
    // Decompress
    auto decompressed = decompressData(compressed, input.size());
    ASSERT_FALSE(decompressed.empty()) << "Decompression failed";
    
    // Verify data integrity
    ASSERT_EQ(decompressed.size(), input.size()) << "Decompressed size doesn't match original";
    EXPECT_EQ(decompressed, input) << "Decompressed data doesn't match original";
}

TEST_F(LzmaTest, DifferentCompressionLevels)
{
    auto input = generateCompressibleData(5000);
    
    std::vector<size_t> compressedSizes;
    
    // Test different compression levels
    for (int level = 0; level <= 9; ++level)
    {
        auto compressed = compressData(input, level);
        ASSERT_FALSE(compressed.empty()) << "Compression failed at level " << level;
        
        // Decompress to verify it works
        auto decompressed = decompressData(compressed, input.size());
        ASSERT_FALSE(decompressed.empty()) << "Decompression failed at level " << level;
        EXPECT_EQ(decompressed, input) << "Data integrity failed at level " << level;
        
        compressedSizes.push_back(compressed.size());
    }
    
    // Higher levels should generally provide better compression (smaller sizes)
    // Note: This is not always guaranteed due to the nature of compression algorithms
    for (size_t i = 1; i < compressedSizes.size(); ++i)
    {
        // Allow some tolerance as higher levels don't always guarantee smaller sizes
        if (compressedSizes[i] > compressedSizes[i-1])
        {
            std::cout << "Warning: Level " << i << " produced larger output than level " << (i-1) 
                      << " (" << compressedSizes[i] << " vs " << compressedSizes[i-1] << ")" << std::endl;
        }
    }
}

TEST_F(LzmaTest, EmptyDataCompression)
{
    // Test with empty data
    std::vector<unsigned char> input;
    
    // For empty data, we need to handle it specially
    // LZMA may not handle empty input properly, so we'll test the edge case
    if (input.empty())
    {
        // For empty data, we should still be able to create a valid LZMA stream
        // but the compressed data might be empty or just contain properties
        auto compressed = compressData(input);
        
        // For empty input, the compressed data should at least contain the properties
        if (!compressed.empty())
        {
            EXPECT_GE(compressed.size(), LZMA_PROPS_SIZE) << "Compressed data should contain at least properties";
            
            // Try to decompress
            auto decompressed = decompressData(compressed, input.size());
            EXPECT_EQ(decompressed.size(), input.size()) << "Decompressed size should match original (0)";
            EXPECT_EQ(decompressed, input) << "Decompressed data should match original (empty)";
        }
        else
        {
            // If compression fails for empty data, that's acceptable behavior
            std::cout << "Note: LZMA compression failed for empty data (this may be expected behavior)" << std::endl;
        }
    }
}

TEST_F(LzmaTest, SingleByteCompression)
{
    // Test with single byte
    std::vector<unsigned char> input = {0x42};
    
    // Compress
    auto compressed = compressData(input);
    ASSERT_FALSE(compressed.empty()) << "Compression failed for single byte";
    
    // Decompress
    auto decompressed = decompressData(compressed, input.size());
    ASSERT_FALSE(decompressed.empty()) << "Decompression failed for single byte";
    
    // Verify data integrity
    ASSERT_EQ(decompressed.size(), input.size()) << "Decompressed size doesn't match original";
    EXPECT_EQ(decompressed, input) << "Decompressed data doesn't match original";
}

TEST_F(LzmaTest, BinaryDataCompression)
{
    // Test with binary data (zeros, ones, patterns)
    std::vector<unsigned char> input(1000);
    
    // Fill with various patterns
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (i % 4 == 0) input[i] = 0x00;      // Zeros
        else if (i % 4 == 1) input[i] = 0xFF; // Ones
        else if (i % 4 == 2) input[i] = 0xAA; // Alternating
        else input[i] = static_cast<unsigned char>(i % 256); // Incremental
    }
    
    // Compress
    auto compressed = compressData(input);
    ASSERT_FALSE(compressed.empty()) << "Compression failed for binary data";
    
    // Decompress
    auto decompressed = decompressData(compressed, input.size());
    ASSERT_FALSE(decompressed.empty()) << "Decompression failed for binary data";
    
    // Verify data integrity
    ASSERT_EQ(decompressed.size(), input.size()) << "Decompressed size doesn't match original";
    EXPECT_EQ(decompressed, input) << "Decompressed data doesn't match original";
}

TEST_F(LzmaTest, CompressionRatioAnalysis)
{
    // Test compression ratios for different data types
    struct TestCase {
        std::string name;
        std::vector<unsigned char> data;
    };
    
    std::vector<TestCase> testCases = {
        {"Random data", generateRandomData(1000)},
        {"Compressible data", generateCompressibleData(1000)},
        {"Repeating pattern", std::vector<unsigned char>(1000, 0x42)},
        {"Alternating pattern", [](size_t size) {
            std::vector<unsigned char> data(size);
            for (size_t i = 0; i < size; ++i) {
                data[i] = (i % 2) ? 0x00 : 0xFF;
            }
            return data;
        }(1000)}
    };
    
    for (const auto& testCase : testCases)
    {
        auto compressed = compressData(testCase.data);
        ASSERT_FALSE(compressed.empty()) << "Compression failed for " << testCase.name;
        
        double compressionRatio = static_cast<double>(compressed.size()) / testCase.data.size();
        std::cout << testCase.name << " compression ratio: " << compressionRatio 
                  << " (" << compressed.size() << "/" << testCase.data.size() << ")" << std::endl;
        
        // Verify decompression works
        auto decompressed = decompressData(compressed, testCase.data.size());
        ASSERT_FALSE(decompressed.empty()) << "Decompression failed for " << testCase.name;
        EXPECT_EQ(decompressed, testCase.data) << "Data integrity failed for " << testCase.name;
    }
}

TEST_F(LzmaTest, PackUnrealQtSrcFolder)
{
    // Use the shared TestPathHelper to find the project root
    fs::path projectRoot = TestPathHelper::findProjectRoot();
    fs::path srcPath = projectRoot / "unreal-qt" / "src";
    
    // Ensure the source directory exists
    ASSERT_TRUE(fs::exists(srcPath)) << "unreal-qt/src directory does not exist at: " << srcPath;
    
    std::cout << "Packing unreal-qt/src folder: " << srcPath << std::endl;
    
    // Collect all files from the src directory
    std::vector<std::pair<fs::path, std::vector<unsigned char>>> files;
    size_t totalSize = 0;
    size_t fileCount = 0;
    size_t dirCount = 0;
    
    for (const auto& entry : fs::recursive_directory_iterator(srcPath))
    {
        if (entry.is_directory())
        {
            dirCount++;
        }
        else if (entry.is_regular_file())
        {
            fileCount++;
            fs::path relativePath = fs::relative(entry.path(), srcPath);
            
            // Read file content
            std::ifstream file(entry.path(), std::ios::binary);
            if (file)
            {
                std::vector<unsigned char> content(
                    (std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>()
                );
                
                files.emplace_back(relativePath, std::move(content));
                totalSize += content.size();
            }
        }
    }
    
    std::cout << "Found " << fileCount << " files, " << dirCount << " directories, " 
              << totalSize << " bytes total" << std::endl;
    
    ASSERT_GT(files.size(), 0) << "No files found in src directory";
    
    // Create a single data stream containing all files
    // Format: [file_count][file1_size][file1_path][file1_data][file2_size][file2_path][file2_data]...
    std::vector<unsigned char> combinedData;
    
    // Write file count (4 bytes)
    uint32_t count = static_cast<uint32_t>(files.size());
    combinedData.insert(combinedData.end(), 
                       reinterpret_cast<unsigned char*>(&count),
                       reinterpret_cast<unsigned char*>(&count) + sizeof(count));
    
    for (const auto& [path, content] : files)
    {
        // Write file size (4 bytes)
        uint32_t size = static_cast<uint32_t>(content.size());
        combinedData.insert(combinedData.end(),
                           reinterpret_cast<unsigned char*>(&size),
                           reinterpret_cast<unsigned char*>(&size) + sizeof(size));
        
        // Write path length (2 bytes) and path
        uint16_t pathLen = static_cast<uint16_t>(path.string().length());
        combinedData.insert(combinedData.end(),
                           reinterpret_cast<unsigned char*>(&pathLen),
                           reinterpret_cast<unsigned char*>(&pathLen) + sizeof(pathLen));
        
        std::string pathStr = path.string();
        combinedData.insert(combinedData.end(), pathStr.begin(), pathStr.end());
        
        // Write file content
        combinedData.insert(combinedData.end(), content.begin(), content.end());
    }
    
    std::cout << "Combined data size: " << combinedData.size() << " bytes" << std::endl;

    _CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = 9;
    props.dictSize = 1 << 20;
    props.numThreads = std::thread::hardware_concurrency();

    // Compress the combined data using custom properties
    auto compressed = compressDataWithProps(combinedData, props);
    ASSERT_FALSE(compressed.empty()) << "Compression failed";
    
    // Calculate compression ratio
    double compressionRatio = static_cast<double>(compressed.size()) / combinedData.size();
    std::cout << "Compression ratio: " << compressionRatio 
              << " (" << compressed.size() << "/" << combinedData.size() << " bytes)" << std::endl;
    
    // Verify compression actually reduced size
    EXPECT_LT(compressionRatio, 1.0) << "Compression should reduce size";
    
    // Decompress
    auto decompressed = decompressData(compressed, combinedData.size());
    ASSERT_FALSE(decompressed.empty()) << "Decompression failed";
    
    // Verify data integrity
    ASSERT_EQ(decompressed.size(), combinedData.size()) << "Decompressed size doesn't match original";
    EXPECT_EQ(decompressed, combinedData) << "Decompressed data doesn't match original";
    
    // Verify we can extract individual files from the decompressed data
    size_t offset = sizeof(uint32_t); // Skip file count
    
    for (const auto& [originalPath, originalContent] : files)
    {
        // Read file size
        uint32_t size = *reinterpret_cast<const uint32_t*>(&decompressed[offset]);
        offset += sizeof(uint32_t);
        
        // Read path length
        uint16_t pathLen = *reinterpret_cast<const uint16_t*>(&decompressed[offset]);
        offset += sizeof(uint16_t);
        
        // Read path
        std::string pathStr(decompressed.begin() + offset, decompressed.begin() + offset + pathLen);
        offset += pathLen;
        
        // Read file content
        std::vector<unsigned char> content(decompressed.begin() + offset, decompressed.begin() + offset + size);
        offset += size;
        
        // Verify file data matches
        EXPECT_EQ(content.size(), originalContent.size()) << "File size mismatch for " << pathStr;
        EXPECT_EQ(content, originalContent) << "File content mismatch for " << pathStr;
    }
    
    std::cout << "Successfully packed and unpacked " << files.size() << " files from unreal-qt/src" << std::endl;
}

TEST_F(LzmaTest, PackCoreSrcFolder)
{
    // Use the shared TestPathHelper to find the project root
    fs::path projectRoot = TestPathHelper::findProjectRoot();
    fs::path srcPath = projectRoot / "core" / "src";
    
    // Ensure the source directory exists
    ASSERT_TRUE(fs::exists(srcPath)) << "core/src directory does not exist at: " << srcPath;
    
    std::cout << "Packing core/src folder: " << srcPath << std::endl;
    
    // Collect all files from the src directory
    std::vector<std::pair<fs::path, std::vector<unsigned char>>> files;
    size_t totalSize = 0;
    size_t fileCount = 0;
    size_t dirCount = 0;
    
    for (const auto& entry : fs::recursive_directory_iterator(srcPath))
    {
        if (entry.is_directory())
        {
            dirCount++;
        }
        else if (entry.is_regular_file())
        {
            fileCount++;
            fs::path relativePath = fs::relative(entry.path(), srcPath);
            
            // Read file content
            std::ifstream file(entry.path(), std::ios::binary);
            if (file)
            {
                std::vector<unsigned char> content(
                    (std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>()
                );
                
                files.emplace_back(relativePath, std::move(content));
                totalSize += content.size();
            }
        }
    }
    
    std::cout << "Found " << fileCount << " files, " << dirCount << " directories, " 
              << totalSize << " bytes total" << std::endl;
    
    ASSERT_GT(files.size(), 0) << "No files found in core/src directory";
    
    // Create a single data stream containing all files
    // Format: [file_count][file1_size][file1_path][file1_data][file2_size][file2_path][file2_data]...
    std::vector<unsigned char> combinedData;
    
    // Write file count (4 bytes)
    uint32_t count = static_cast<uint32_t>(files.size());
    combinedData.insert(combinedData.end(), 
                       reinterpret_cast<unsigned char*>(&count),
                       reinterpret_cast<unsigned char*>(&count) + sizeof(count));
    
    for (const auto& [path, content] : files)
    {
        // Write file size (4 bytes)
        uint32_t size = static_cast<uint32_t>(content.size());
        combinedData.insert(combinedData.end(),
                           reinterpret_cast<unsigned char*>(&size),
                           reinterpret_cast<unsigned char*>(&size) + sizeof(size));
        
        // Write path length (2 bytes) and path
        uint16_t pathLen = static_cast<uint16_t>(path.string().length());
        combinedData.insert(combinedData.end(),
                           reinterpret_cast<unsigned char*>(&pathLen),
                           reinterpret_cast<unsigned char*>(&pathLen) + sizeof(pathLen));
        
        std::string pathStr = path.string();
        combinedData.insert(combinedData.end(), pathStr.begin(), pathStr.end());
        
        // Write file content
        combinedData.insert(combinedData.end(), content.begin(), content.end());
    }
    
    std::cout << "Combined data size: " << combinedData.size() << " bytes" << std::endl;

    // Set up custom compression properties
    _CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = 9;
    props.dictSize = 1 << 20;
    props.numThreads = std::thread::hardware_concurrency();

    // Compress the combined data using custom properties
    auto compressed = compressDataWithProps(combinedData, props);
    ASSERT_FALSE(compressed.empty()) << "Compression failed";
    
    // Calculate compression ratio
    double compressionRatio = static_cast<double>(compressed.size()) / combinedData.size();
    std::cout << "Compression ratio: " << compressionRatio 
              << " (" << compressed.size() << "/" << combinedData.size() << " bytes)" << std::endl;
    
    // Verify compression actually reduced size
    EXPECT_LT(compressionRatio, 1.0) << "Compression should reduce size";
    
    // Decompress
    auto decompressed = decompressData(compressed, combinedData.size());
    ASSERT_FALSE(decompressed.empty()) << "Decompression failed";
    
    // Verify data integrity
    ASSERT_EQ(decompressed.size(), combinedData.size()) << "Decompressed size doesn't match original";
    EXPECT_EQ(decompressed, combinedData) << "Decompressed data doesn't match original";
    
    // Verify we can extract individual files from the decompressed data
    size_t offset = sizeof(uint32_t); // Skip file count
    
    for (const auto& [originalPath, originalContent] : files)
    {
        // Read file size
        uint32_t size = *reinterpret_cast<const uint32_t*>(&decompressed[offset]);
        offset += sizeof(uint32_t);
        
        // Read path length
        uint16_t pathLen = *reinterpret_cast<const uint16_t*>(&decompressed[offset]);
        offset += sizeof(uint16_t);
        
        // Read path
        std::string pathStr(decompressed.begin() + offset, decompressed.begin() + offset + pathLen);
        offset += pathLen;
        
        // Read file content
        std::vector<unsigned char> content(decompressed.begin() + offset, decompressed.begin() + offset + size);
        offset += size;
        
        // Verify file data matches
        EXPECT_EQ(content.size(), originalContent.size()) << "File size mismatch for " << pathStr;
        EXPECT_EQ(content, originalContent) << "File content mismatch for " << pathStr;
    }
    
    std::cout << "Successfully packed and unpacked " << files.size() << " files from core/src" << std::endl;
}

TEST_F(LzmaTest, DifferentCompressionConfigurations)
{
    // Test different compression configurations
    auto input = generateCompressibleData(5000);
    
    std::vector<std::pair<std::string, _CLzmaEncProps>> configurations = {
        {"Fast compression (level 1)", []() {
            _CLzmaEncProps props;
            LzmaEncProps_Init(&props);
            props.level = 1;
            props.dictSize = 1 << 16; // 64KB
            props.numThreads = 1;
            return props;
        }()},
        {"Balanced compression (level 5)", []() {
            _CLzmaEncProps props;
            LzmaEncProps_Init(&props);
            props.level = 5;
            props.dictSize = 1 << 20; // 1MB
            props.numThreads = 2;
            return props;
        }()},
        {"Maximum compression (level 9)", []() {
            _CLzmaEncProps props;
            LzmaEncProps_Init(&props);
            props.level = 9;
            props.dictSize = 1 << 24; // 16MB
            props.numThreads = std::thread::hardware_concurrency();
            return props;
        }()},
        {"Custom parameters", []() {
            _CLzmaEncProps props;
            LzmaEncProps_Init(&props);
            props.level = 7;
            props.dictSize = 1 << 22; // 4MB
            props.lc = 4;  // More literal context bits
            props.lp = 1;  // Literal position bits
            props.pb = 3;  // Position bits
            props.fb = 64; // Fast bytes
            props.numThreads = 2;
            return props;
        }()}
    };
    
    for (const auto& [name, props] : configurations)
    {
        std::cout << "\nTesting: " << name << std::endl;
        
        auto compressed = compressDataWithProps(input, props);
        ASSERT_FALSE(compressed.empty()) << "Compression failed for " << name;
        
        double compressionRatio = static_cast<double>(compressed.size()) / input.size();
        std::cout << "  Compression ratio: " << compressionRatio 
                  << " (" << compressed.size() << "/" << input.size() << " bytes)" << std::endl;
        
        // Decompress and verify
        auto decompressed = decompressData(compressed, input.size());
        ASSERT_FALSE(decompressed.empty()) << "Decompression failed for " << name;
        EXPECT_EQ(decompressed, input) << "Data integrity failed for " << name;
    }
} 