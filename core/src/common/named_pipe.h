#pragma once

#include <cstdint>
#include <string>
#include <atomic>

/// @brief Cross-platform named pipe (FIFO) abstraction
///
/// On Unix: uses mkfifo() to create a named pipe in the filesystem.
/// On Windows: uses CreateNamedPipe() API.
///
/// Used by FFmpegPipeEncoder to feed multiple input streams (video + audio)
/// to an external ffmpeg process via separate pipes.
///
/// The pipe provides natural backpressure: if the reader (ffmpeg) can't
/// consume data fast enough, write() blocks until the buffer has space.
/// This throttles the emulation loop without explicit signaling.
class NamedPipe
{
public:
    NamedPipe() = default;
    ~NamedPipe();

    // Non-copyable, non-movable (owns OS handle)
    NamedPipe(const NamedPipe&) = delete;
    NamedPipe& operator=(const NamedPipe&) = delete;
    NamedPipe(NamedPipe&&) = delete;
    NamedPipe& operator=(NamedPipe&&) = delete;

    /// @brief Create a named pipe at the given filesystem path
    /// @param path Filesystem path for the pipe
    /// @return true if pipe was created successfully
    static bool create(const std::string& path);

    /// @brief Open an existing pipe for writing
    /// @param path Filesystem path for the pipe
    /// @param timeoutMs Maximum time to wait for a reader (default 5000ms). 0 = non-blocking.
    /// @return true if pipe was opened successfully
    bool openForWrite(const std::string& path, int timeoutMs = 5000);

    /// @brief Open an existing pipe for writing with a timeout and abort condition
    /// @param path Filesystem path for the pipe
    /// @param timeoutMs Timeout in milliseconds
    /// @param abortFlag Pointer to boolean flag that aborts the open if it becomes true
    /// @return true if pipe was opened successfully
    bool openForWriteTimeout(const std::string& path, int timeoutMs, const std::atomic<bool>* abortFlag = nullptr);

    /// @brief Write data to the pipe (blocks if pipe buffer is full)
    /// @param data Pointer to data buffer
    /// @param size Number of bytes to write
    /// @param abortFlag Optional flag to abort blocking write
    /// @return true if all bytes were written
    bool write(const void* data, size_t size, const std::atomic<bool>* abortFlag = nullptr);

    /// @brief Close the pipe handle
    void close();

    /// @brief Remove the named pipe from the filesystem
    /// @param path Filesystem path for the pipe
    static void remove(const std::string& path);

    /// @brief Check if pipe is currently open
    bool isOpen() const;

    /// @brief Get the pipe's filesystem path
    const std::string& getPath() const { return _path; }

    /// @brief Get last error message
    std::string getLastError() const { return _lastError; }

private:
#ifdef _WIN32
    void* _handle = nullptr; // HANDLE
#else
    int _fd = -1;
#endif
    std::string _path;
    std::string _lastError;
};
