#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

/// @brief Cross-platform named pipe (FIFO) abstraction, the writer's end
///
/// On Unix: a FIFO made with mkfifo() (0600, the user's alone); put it in a private directory.
/// On Windows: the pipe \\.\pipe\<name> made with CreateNamedPipe(): one instance (a second pipe of the same name is
/// refused, so nobody can take the name first), local clients only, open to this user alone; when the reader's
/// process is known (setReaderProcess) a client of another process is refused.
///
/// Used by FFmpegPipeEncoder and the window recorder to feed several input streams (video, sound) to one ffmpeg.
///
/// Two ways to write:
/// - blocking (openForWriteTimeout, write): the pipe's backpressure throttles the writer; both waits end on a
///   timeout or the abort flag, never hang on a reader that is gone or never came
/// - non-blocking (tryOpenForWrite, tryWrite, flushed): nothing waits; for a writer that feeds two pipes of one
///   reader from one thread (a writer stuck on one pipe while the reader waits for the other would be a deadlock)
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

    /// @brief A name no other pipe has, in this process or another: `tag`, the process id, a counter and 64 random
    /// bits. The Windows pipe's name, or a FIFO's file name (in a private directory)
    static std::string uniqueName(const std::string& tag);

    /// @brief What the reader opens for `path` (ffmpeg's input): \\.\pipe\<path> on Windows, the path on Unix
    static std::string readerPath(const std::string& path);

    /// @brief Make the pipe, before the reader starts. Unix: the FIFO at `path` (an old file there is replaced);
    /// Windows: the pipe named `path` (a name in use is refused)
    bool create(const std::string& path);

    /// @brief Only this process may open the pipe (Windows; the FIFO is guarded by its directory). Before the
    /// reader connects: a client of another process is refused and the pipe fails
    void setReaderProcess(uint64_t processId) { _readerProcess = processId; }

    /// @brief Wait for the reader to open the pipe (blocking mode)
    /// @param timeoutMs Timeout in milliseconds
    /// @param abortFlag Aborts the wait when it becomes true
    /// @return true if the reader is there
    bool openForWriteTimeout(int timeoutMs, const std::atomic<bool>* abortFlag = nullptr);

    /// @brief Write all of `data` (blocks while the pipe is full; returns when the reader has room for all of it)
    /// @return true if all bytes were written; false: the reader is gone, or aborted
    bool write(const void* data, size_t size, const std::atomic<bool>* abortFlag = nullptr);

    /// @brief Non-blocking: true once the reader has opened the pipe. False: not yet, or failed (failed() tells)
    bool tryOpenForWrite();

    /// @brief Non-blocking: hand over what the pipe takes now
    /// @return the bytes taken (0: the pipe is full, try again), -1: the reader is gone (failed())
    int64_t tryWrite(const void* data, size_t size);

    /// @brief Non-blocking: everything handed over is in the pipe (Windows: no write in flight). Close only then,
    /// or the last bytes are dropped
    bool flushed();

    /// @brief Close the writer's end: the reader reads what is in the pipe, then the end. A write still in flight
    /// is cancelled
    void close();

    /// @brief Remove the FIFO from the filesystem (Unix; a Windows pipe goes with its handle)
    static void remove(const std::string& path);

    /// @brief The writer's end is open (the reader has opened the pipe)
    bool isOpen() const;

    /// @brief The pipe failed: see getLastError()
    bool failed() const { return _failed; }

    /// @brief Get the pipe's name or path as given to create()
    const std::string& getPath() const { return _path; }

    /// @brief Get last error message
    std::string getLastError() const { return _lastError; }

private:
    bool fail(const std::string& why);

    std::string _path;
    std::string _lastError;
    bool _failed = false;
    uint64_t _readerProcess = 0;
#ifdef _WIN32
    bool connectDone();
    int pollWrite();
    void cancelWrite();

    void* _handle = nullptr;        ///< HANDLE of the pipe
    void* _connectEvent = nullptr;  ///< HANDLE: the overlapped connect's
    void* _writeEvent = nullptr;    ///< HANDLE: the overlapped writes'
    std::vector<unsigned char> _connectOverlapped;  ///< OVERLAPPED (kept opaque: no windows.h here)
    std::vector<unsigned char> _writeOverlapped;
    std::vector<uint8_t> _pending;  ///< the bytes of the non-blocking write in flight (the system reads them)
    bool _connecting = false;       ///< the overlapped connect is pending
    bool _connected = false;
    bool _writing = false;          ///< a write is in flight
#else
    int _fd = -1;
#endif
};
