// WebAPI Upload Helper - File staging for embedded media uploads
// Implements webapi-media-upload-tdd.md

#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <mutex>

#include <drogon/HttpRequest.h>

namespace api::v1
{

// Size limits (bytes) per webapi-media-upload-tdd.md §2.5
constexpr size_t MAX_SNAPSHOT_SIZE = 4 * 1024 * 1024;   // 4 MB - ZX Evolution full state
constexpr size_t MAX_DISK_SIZE     = 1 * 1024 * 1024;   // 1 MB - single TRD/SCL/FDI
constexpr size_t MAX_TAPE_SIZE     = 1 * 1024 * 1024;   // 1 MB - TAP/TZX
constexpr size_t MAX_UPLOAD_BODY_SIZE = 5 * 1024 * 1024; // 5 MB - drogon body buffer limit

enum class MediaType
{
    Snapshot,
    Disk,
    Tape,
    Unknown
};

// Result of extracting media content from a request
struct MediaContent
{
    std::vector<uint8_t> data;
    std::string filename;
    std::string path;      // For JSON path-based requests (data empty)
    std::string errorMsg;
    bool isEmbedded = false;  // true if data was embedded, false if path reference
    bool valid = false;
};

// Extract media content from request (JSON path, multipart, or raw body)
MediaContent extractMediaContent(const drogon::HttpRequestPtr& req, MediaType expectedType);

// Determine media type from filename extension
MediaType getMediaTypeFromFilename(const std::string& filename);

// Get size limit for media type
size_t getMaxSizeForMediaType(MediaType type);

// Sanitize filename (remove path traversal, extract basename)
std::string sanitizeFilename(const std::string& filename);

class UploadHelper
{
public:
    static UploadHelper& Instance();

    // Set the writable root directory (called from app_init)
    void setWritableRoot(const std::string& root);

    // Stage uploaded content to a temp file
    // Returns the full path to the staged file, or empty string on error
    // Sets errorMsg on failure
    std::string stageUpload(const uint8_t* data, size_t size,
                            const std::string& filename,
                            MediaType type,
                            std::string& errorMsg);

    // Alternative: stage from vector
    std::string stageUpload(const std::vector<uint8_t>& data,
                            const std::string& filename,
                            MediaType type,
                            std::string& errorMsg);

    // Clean up all staged files (called on shutdown)
    void cleanup();

    // Get session upload directory
    const std::string& getUploadDir() const { return _uploadDir; }

private:
    UploadHelper();
    ~UploadHelper();

    void ensureUploadDir();

    std::string _writableRoot;
    std::string _uploadDir;      // <writable_root>/uploads/<session_uuid>/
    std::string _sessionId;
    uint32_t _uploadSeq = 0;
    std::mutex _mutex;
    bool _initialized = false;
};

} // namespace api::v1
