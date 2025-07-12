#include "loader_uns.h"

#include <cstring>  // Required for memcmp
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#include "3rdparty/liblzma/include/LzmaLib.h"
#include "3rdparty/rapidyaml/ryml_all.hpp"
#include "loaders/snapshot/uns/serializers/yamlsnapshotserializer.h"


// 7z signature: 37 7A BC AF 27 1C
//static const uint8_t k7zSignature[6] = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C};

// @brief Construct LoaderUNS with emulator context and snapshot file path
// @param context Pointer to EmulatorContext for state restoration
// @param unsFilePath Path to the .uns snapshot file
LoaderUNS::LoaderUNS(EmulatorContext* context, const std::string& unsFilePath)
    : _context(context), _unsFilePath(unsFilePath)
{
    (void)_context; // TODO: Remove this when context is used
}

// @brief Destructor for LoaderUNS
LoaderUNS::~LoaderUNS()
{
    // Cleanup if needed
}

// @brief Load the snapshot from the .uns file and apply to emulator
// @return true on success, false on error (see lastError)
bool LoaderUNS::load()
{
    _lastError.clear();
    std::cout << "[LoaderUNS] Loading snapshot from: " << _unsFilePath << std::endl;

    // 1. Check file exists and non-zero size
    std::error_code ec;
    auto fileSize = std::filesystem::file_size(_unsFilePath, ec);
    if (ec || fileSize == 0)
    {
        _lastError = "Snapshot file is empty or inaccessible: " + _unsFilePath;
        std::cerr << "[LoaderUNS] Error: " << _lastError << std::endl;
        return false;
    }

    // 2. Decompress archive (LZMA stream)
    std::string tempDir = extractArchive(_unsFilePath);
    if (tempDir.empty() || !std::filesystem::exists(tempDir))
    {
        _lastError = "Failed to decompress .uns archive to temp directory.";
        std::cerr << "[LoaderUNS] Error: " << _lastError << std::endl;
        return false;
    }
    std::cout << "[LoaderUNS] Archive decompressed to: " << tempDir << std::endl;

    // 3. Parse YAML manifest using YamlSnapshotSerializer
    std::string manifestPath = tempDir + "/snapshot.yaml";
    YamlSnapshotSerializer yamlSerializer;
    SnapshotDTO snapshot;
    if (!yamlSerializer.load(manifestPath, snapshot)) {
        _lastError = "Failed to parse YAML manifest: " + yamlSerializer.lastError();
        std::cerr << "[LoaderUNS] Error: " << _lastError << std::endl;
        return false;
    }
    std::cout << "[LoaderUNS] YAML manifest loaded successfully." << std::endl;

    // TODO: Apply snapshot DTO to emulator state
    // ...

    return false;  // Not fully implemented yet
}

// @brief Get the last error message (if any)
// @return Last error string, or empty if no error
std::string LoaderUNS::lastError() const
{
    return _lastError;
}

// @brief Decompress the .uns file to a temp folder using LZMA
// @param unsFilePath Path to the .uns file
// @return Path to temp folder with decompressed data, or empty string on error
std::string LoaderUNS::extractArchive(const std::string& unsFilePath)
{
    // Check file size > 0
    std::error_code ec;
    auto fileSize = std::filesystem::file_size(unsFilePath, ec);
    if (ec || fileSize == 0)
    {
        _lastError = "Snapshot file is empty or inaccessible: " + unsFilePath;
        std::cerr << "[LoaderUNS] Error: " << _lastError << std::endl;
        return std::string();
    }

    // Read the entire .uns file into memory
    std::ifstream file(unsFilePath, std::ios::binary);
    if (!file)
    {
        _lastError = "Failed to open .uns file for reading: " + unsFilePath;
        std::cerr << "[LoaderUNS] Error: " << _lastError << std::endl;
        return std::string();
    }
    std::vector<unsigned char> compressedData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // TODO: Determine uncompressed size (from manifest or header, or use a large buffer and resize after)
    size_t estimatedUncompressedSize = 32 * 1024 * 1024;  // 32MB default, adjust as needed
    std::vector<unsigned char> decompressedData(estimatedUncompressedSize);
    size_t destLen = decompressedData.size();

    // LZMA properties are usually the first 5 bytes
    if (compressedData.size() < 5)
    {
        _lastError = "Compressed .uns file too small to contain LZMA properties.";
        std::cerr << "[LoaderUNS] Error: " << _lastError << std::endl;
        return std::string();
    }
    unsigned char* props = compressedData.data();
    size_t propsSize = 5;
    unsigned char* src = compressedData.data() + 5;
    size_t srcDataLen = compressedData.size() - 5;

    // Call LzmaUncompress
    int result = LzmaUncompress(decompressedData.data(), &destLen, src, &srcDataLen, props, propsSize);
    if (result != 0)
    {
        _lastError = "LZMA decompression failed with code: " + std::to_string(result);
        std::cerr << "[LoaderUNS] Error: " << _lastError << std::endl;
        return std::string();
    }
    decompressedData.resize(destLen);
    std::cout << "[LoaderUNS] LZMA decompression successful. Uncompressed size: " << destLen << " bytes." << std::endl;

    // Unpack directory structure from buffer
    size_t offset = 0;
    uint32_t fileCount = 0;
    memcpy(&fileCount, decompressedData.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / ("uns_extract_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(tempDir);

    for (uint32_t i = 0; i < fileCount; ++i)
    {
        uint32_t fileSize = 0;
        memcpy(&fileSize, decompressedData.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        uint16_t pathLen = 0;
        memcpy(&pathLen, decompressedData.data() + offset, sizeof(uint16_t));

        offset += sizeof(uint16_t);
        std::string relPath(reinterpret_cast<const char*>(decompressedData.data() + offset), pathLen);
        offset += pathLen;
        
        std::vector<unsigned char> fileContent(decompressedData.data() + offset, decompressedData.data() + offset + fileSize);
        offset += fileSize;

        // Write file to tempDir/relPath, creating directories as needed
        std::filesystem::path outPath = tempDir / relPath;
        std::filesystem::create_directories(outPath.parent_path());
        std::ofstream outFile(outPath, std::ios::binary);
        outFile.write(reinterpret_cast<const char*>(fileContent.data()), fileContent.size());
        outFile.close();
    }

    return tempDir.string();
}

// @brief Cleanup temp folder (not yet implemented)
// @param folderPath Path to temp folder to remove
void LoaderUNS::cleanupTempFolder(const std::string& folderPath)
{
    std::cout << "[LoaderUNS] Cleaning up temp folder: " << folderPath << std::endl;
    // TODO: Implement cleanup logic
}

bool LoaderUNS::recursiveCleanup(const std::filesystem::path& dir)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return true;

    std::filesystem::remove_all(dir, ec);
    if (ec)
    {
        std::cerr << "[LoaderUNS] Failed to remove directory: " << dir << ". Error: " << ec.message() << std::endl;
        return false;
    }
    
    return true;
}

bool LoaderUNS::compressAndSaveDirectory(const std::filesystem::path& snapshotDir, const std::filesystem::path& unsFilePath)
{
    // Recursively walk the snapshot directory and collect all files
    std::vector<std::pair<std::filesystem::path, std::vector<unsigned char>>> files;
    for (auto& entry : std::filesystem::recursive_directory_iterator(snapshotDir))
    {
        if (entry.is_regular_file())
        {
            std::filesystem::path relPath = std::filesystem::relative(entry.path(), snapshotDir);
            std::ifstream inFile(entry.path(), std::ios::binary);
            std::vector<unsigned char> content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
            files.emplace_back(relPath, std::move(content));
        }
    }

    // Pack file count and all file records into a buffer
    std::vector<unsigned char> combinedData;
    uint32_t count = static_cast<uint32_t>(files.size());
    combinedData.insert(combinedData.end(), reinterpret_cast<unsigned char*>(&count), reinterpret_cast<unsigned char*>(&count) + sizeof(count));
    for (const auto& [relPath, content] : files) {
        uint32_t size = static_cast<uint32_t>(content.size());
        combinedData.insert(combinedData.end(), reinterpret_cast<unsigned char*>(&size), reinterpret_cast<unsigned char*>(&size) + sizeof(size));
        std::string pathStr = relPath.string();
        uint16_t pathLen = static_cast<uint16_t>(pathStr.length());
        combinedData.insert(combinedData.end(), reinterpret_cast<unsigned char*>(&pathLen), reinterpret_cast<unsigned char*>(&pathLen) + sizeof(pathLen));
        combinedData.insert(combinedData.end(), pathStr.begin(), pathStr.end());
        combinedData.insert(combinedData.end(), content.begin(), content.end());
    }

    // LZMA-compress the buffer
    size_t maxOutputSize = combinedData.size() + (combinedData.size() / 10) + 1024;
    std::vector<unsigned char> compressed(maxOutputSize);
    unsigned char props[LZMA_PROPS_SIZE];
    size_t propsSize = LZMA_PROPS_SIZE;
    int level = 5;
    unsigned dictSize = 1 << 24;
    int lc = 3, lp = 0, pb = 2, fb = 32, numThreads = 2;
    int result = LzmaCompress(
        compressed.data(), &maxOutputSize,
        combinedData.data(), combinedData.size(),
        props, &propsSize,
        level, dictSize, lc, lp, pb, fb, numThreads
    );

    if (result != SZ_OK)
    {
        // Handle compression error
        return false;
    }
    compressed.resize(maxOutputSize);
    
    // Write props + compressed data to .uns file
    std::ofstream outFile(unsFilePath, std::ios::binary);
    outFile.write(reinterpret_cast<const char*>(props), LZMA_PROPS_SIZE);
    outFile.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
    outFile.close();
    return true;
}