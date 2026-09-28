// WebAPI Upload Helper - File staging for embedded media uploads

#include "upload_helper.h"

#include <common/filehelper.h>
#include <drogon/HttpTypes.h>
#include <drogon/MultiPart.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <random>
#include <filesystem>

namespace fs = std::filesystem;

namespace api::v1
{

MediaType getMediaTypeFromFilename(const std::string& filename)
{
    if (filename.empty()) return MediaType::Unknown;

    // Find extension
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return MediaType::Unknown;

    std::string ext = filename.substr(dot);
    // Lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Snapshots
    if (ext == ".sna" || ext == ".z80" || ext == ".szx" || ext == ".sp" || ext == ".snp")
        return MediaType::Snapshot;

    // Disks
    if (ext == ".trd" || ext == ".scl" || ext == ".fdi" || ext == ".udi" || ext == ".td0" || ext == ".img" ||
        ext == ".dsk" || ext == ".mgt")
        return MediaType::Disk;

    // Tapes
    if (ext == ".tap" || ext == ".tzx" || ext == ".csw" || ext == ".wav")
        return MediaType::Tape;

    return MediaType::Unknown;
}

size_t getMaxSizeForMediaType(MediaType type)
{
    switch (type)
    {
        case MediaType::Snapshot: return MAX_SNAPSHOT_SIZE;
        case MediaType::Disk:     return MAX_DISK_SIZE;
        case MediaType::Tape:     return MAX_TAPE_SIZE;
        default:                  return MAX_SNAPSHOT_SIZE; // Reasonable default
    }
}

std::string sanitizeFilename(const std::string& filename)
{
    if (filename.empty()) return "upload";

    // Find basename (after last / or \)
    auto lastSlash = filename.find_last_of("/\\");
    std::string basename = (lastSlash == std::string::npos) ? filename : filename.substr(lastSlash + 1);

    if (basename.empty()) return "upload";

    // Remove any remaining path traversal attempts
    std::string result;
    result.reserve(basename.size());
    for (char c : basename)
    {
        // Allow alphanumeric, dot, dash, underscore
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')
            result += c;
        else
            result += '_';
    }

    // Prevent leading dots (hidden files / relative paths)
    while (!result.empty() && result[0] == '.')
        result.erase(0, 1);

    return result.empty() ? "upload" : result;
}

// Generate a short random session ID
static std::string generateSessionId()
{
    static const char chars[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::string id;
    id.reserve(16);
    for (int i = 0; i < 16; ++i)
        id += chars[dis(gen)];
    return id;
}

UploadHelper& UploadHelper::Instance()
{
    static UploadHelper instance;
    return instance;
}

UploadHelper::UploadHelper()
{
    _sessionId = generateSessionId();
}

UploadHelper::~UploadHelper()
{
    cleanup();
}

void UploadHelper::setWritableRoot(const std::string& root)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _writableRoot = root;
    _initialized = false; // Will re-create upload dir on next use
}

void UploadHelper::ensureUploadDir()
{
    if (_initialized) return;

    if (_writableRoot.empty())
    {
        // Fallback to executable path + "uploads"
        _writableRoot = FileHelper::GetExecutablePath();
    }

    _uploadDir = _writableRoot + "/uploads/" + _sessionId + "/";

    std::error_code ec;
    fs::create_directories(_uploadDir, ec);
    // Ignore errors - stageUpload will fail if dir creation failed

    _initialized = true;
}

std::string UploadHelper::stageUpload(const uint8_t* data, size_t size,
                                       const std::string& filename,
                                       MediaType type,
                                       std::string& errorMsg)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Check size limit
    size_t maxSize = getMaxSizeForMediaType(type);
    if (size > maxSize)
    {
        errorMsg = "File too large: " + std::to_string(size) + " bytes (max " +
                   std::to_string(maxSize / (1024 * 1024)) + " MB for this type)";
        return "";
    }

    if (size == 0)
    {
        errorMsg = "Empty file";
        return "";
    }

    ensureUploadDir();

    // Generate unique filename: <seq>_<sanitized_name>
    uint32_t seq = ++_uploadSeq;
    std::string safeName = sanitizeFilename(filename);
    std::string fullPath = _uploadDir + std::to_string(seq) + "_" + safeName;

    // Write file
    FILE* f = fopen(fullPath.c_str(), "wb");
    if (!f)
    {
        errorMsg = "Failed to create staging file: " + fullPath;
        return "";
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size)
    {
        errorMsg = "Failed to write staging file (wrote " + std::to_string(written) + " of " + std::to_string(size) + ")";
        std::remove(fullPath.c_str());
        return "";
    }

    // Staged successfully
    return fullPath;
}

std::string UploadHelper::stageUpload(const std::vector<uint8_t>& data,
                                       const std::string& filename,
                                       MediaType type,
                                       std::string& errorMsg)
{
    return stageUpload(data.data(), data.size(), filename, type, errorMsg);
}

void UploadHelper::cleanup()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_uploadDir.empty() || !_initialized) return;

    std::error_code ec;
    if (fs::exists(_uploadDir, ec))
    {
        fs::remove_all(_uploadDir, ec);
    }

    _initialized = false;
}

MediaContent extractMediaContent(const drogon::HttpRequestPtr& req, MediaType expectedType)
{
    MediaContent result;

    auto contentType = req->contentType();

    // Case 1: Multipart form data
    if (contentType == drogon::CT_MULTIPART_FORM_DATA)
    {
        drogon::MultiPartParser parser;
        if (parser.parse(req) != 0)
        {
            result.errorMsg = "Failed to parse multipart form data";
            return result;
        }

        auto& files = parser.getFiles();
        if (files.empty())
        {
            result.errorMsg = "No file uploaded in multipart request";
            return result;
        }

        // Get the first file
        const auto& file = files[0];
        result.filename = file.getFileName();
        result.data.assign(
            reinterpret_cast<const uint8_t*>(file.fileData()),
            reinterpret_cast<const uint8_t*>(file.fileData()) + file.fileLength()
        );
        result.isEmbedded = true;

        // Validate type from filename
        MediaType actualType = getMediaTypeFromFilename(result.filename);
        if (actualType == MediaType::Unknown)
        {
            result.errorMsg = "Unrecognized file extension: " + result.filename;
            return result;
        }
        if (expectedType != MediaType::Unknown && actualType != expectedType)
        {
            result.errorMsg = "File type mismatch: expected different media type";
            return result;
        }

        // Check size limit
        size_t maxSize = getMaxSizeForMediaType(actualType);
        if (result.data.size() > maxSize)
        {
            result.errorMsg = "File too large: " + std::to_string(result.data.size()) +
                              " bytes (max " + std::to_string(maxSize / (1024 * 1024)) + " MB)";
            return result;
        }

        result.valid = true;
        return result;
    }

    // Case 2: Raw binary with X-Filename header
    if (contentType == drogon::CT_APPLICATION_OCTET_STREAM)
    {
        result.filename = req->getHeader("X-Filename");
        if (result.filename.empty())
        {
            result.errorMsg = "Missing X-Filename header for raw body upload";
            return result;
        }

        auto body = req->body();
        result.data.assign(
            reinterpret_cast<const uint8_t*>(body.data()),
            reinterpret_cast<const uint8_t*>(body.data()) + body.size()
        );
        result.isEmbedded = true;

        // Validate type from filename
        MediaType actualType = getMediaTypeFromFilename(result.filename);
        if (actualType == MediaType::Unknown)
        {
            result.errorMsg = "Unrecognized file extension in X-Filename: " + result.filename;
            return result;
        }
        if (expectedType != MediaType::Unknown && actualType != expectedType)
        {
            result.errorMsg = "File type mismatch: expected different media type";
            return result;
        }

        // Check size limit
        size_t maxSize = getMaxSizeForMediaType(actualType);
        if (result.data.size() > maxSize)
        {
            result.errorMsg = "File too large: " + std::to_string(result.data.size()) +
                              " bytes (max " + std::to_string(maxSize / (1024 * 1024)) + " MB)";
            return result;
        }

        result.valid = true;
        return result;
    }

    // Case 3: JSON with path (existing behavior)
    auto json = req->getJsonObject();
    if (json && json->isMember("path"))
    {
        result.path = (*json)["path"].asString();
        result.isEmbedded = false;
        result.valid = true;
        return result;
    }

    // No valid content found
    result.errorMsg = "Missing content: provide 'path' in JSON, multipart 'file', or raw body with X-Filename header";
    return result;
}

} // namespace api::v1
