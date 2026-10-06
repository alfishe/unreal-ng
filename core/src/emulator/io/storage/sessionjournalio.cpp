#include "stdafx.h"

#include "sessionjournalio.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <random>
#include <system_error>
#include <thread>

#include "common/filehelper.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/// region <SessionJournalFile>

std::unique_ptr<SessionJournalFile> SessionJournalFile::Create(const std::filesystem::path& path, bool temp)
{
    std::unique_ptr<SessionJournalFile> file(new SessionJournalFile());
    file->_path = path;
    file->_temp = temp;
#if defined(_WIN32)
    const DWORD flags = FILE_ATTRIBUTE_NORMAL | (temp ? FILE_FLAG_DELETE_ON_CLOSE : 0);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, CREATE_ALWAYS, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return nullptr;
    file->_handle = h;
#else
    file->_fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (file->_fd < 0)
        return nullptr;
    if (temp)
    {
        // The open descriptor keeps the data; the name goes now (a crash leaves nothing)
        file->_gone = ::unlink(path.c_str()) == 0;
    }
#endif
    return file;
}

std::unique_ptr<SessionJournalFile> SessionJournalFile::OpenExisting(const std::filesystem::path& path)
{
    std::unique_ptr<SessionJournalFile> file(new SessionJournalFile());
    file->_path = path;
#if defined(_WIN32)
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return nullptr;
    file->_handle = h;
#else
    file->_fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (file->_fd < 0)
        return nullptr;
#endif
    return file;
}

SessionJournalFile::~SessionJournalFile()
{
    Close();
}

bool SessionJournalFile::WriteAt(uint64_t offset, const void* data, size_t size)
{
    const auto* p = static_cast<const uint8_t*>(data);
    while (size)
    {
#if defined(_WIN32)
        if (!_handle)
            return false;
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(offset);
        at.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 1u << 30));
        if (!WriteFile(static_cast<HANDLE>(_handle), p, chunk, &written, &at) || written == 0)
            return false;
#else
        if (_fd < 0)
            return false;
        const ssize_t written = ::pwrite(_fd, p, size, static_cast<off_t>(offset));
        if (written <= 0)
            return false;
#endif
        p += written;
        offset += static_cast<uint64_t>(written);
        size -= static_cast<size_t>(written);
    }
    return true;
}

bool SessionJournalFile::ReadAt(uint64_t offset, void* data, size_t size) const
{
    auto* p = static_cast<uint8_t*>(data);
    while (size)
    {
#if defined(_WIN32)
        if (!_handle)
            return false;
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(offset);
        at.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD read = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 1u << 30));
        if (!ReadFile(static_cast<HANDLE>(_handle), p, chunk, &read, &at) || read == 0)
            return false;
#else
        if (_fd < 0)
            return false;
        const ssize_t read = ::pread(_fd, p, size, static_cast<off_t>(offset));
        if (read <= 0)
            return false;
#endif
        p += read;
        offset += static_cast<uint64_t>(read);
        size -= static_cast<size_t>(read);
    }
    return true;
}

bool SessionJournalFile::Sync()
{
#if defined(_WIN32)
    return _handle && FlushFileBuffers(static_cast<HANDLE>(_handle));
#else
    return _fd >= 0 && ::fsync(_fd) == 0;
#endif
}

uint64_t SessionJournalFile::Size() const
{
#if defined(_WIN32)
    LARGE_INTEGER size{};
    return _handle && GetFileSizeEx(static_cast<HANDLE>(_handle), &size) ? static_cast<uint64_t>(size.QuadPart) : 0;
#else
    struct stat st{};
    return _fd >= 0 && ::fstat(_fd, &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
#endif
}

void SessionJournalFile::Close()
{
#if defined(_WIN32)
    if (_handle)
    {
        CloseHandle(static_cast<HANDLE>(_handle));
        _handle = nullptr;
        if (_temp)
            _gone = true;  // delete-on-close
    }
#else
    if (_fd >= 0)
    {
        ::close(_fd);
        _fd = -1;
        if (_temp && !_gone)
        {
            std::error_code ec;
            _gone = std::filesystem::remove(_path, ec);
        }
    }
#endif
}

void SessionJournalFile::Remove()
{
    Close();
    if (!_gone)
    {
        std::error_code ec;
        std::filesystem::remove(_path, ec);
        _gone = true;
    }
}

std::string SessionJournalFile::Path() const
{
    return _gone || _temp ? "(deleted) " + FileHelper::FromFsPath(_path) : FileHelper::FromFsPath(_path);
}

std::filesystem::path SessionJournalFile::TempPath(const std::string& folder)
{
    std::error_code ec;
    std::filesystem::path dir = folder.empty() ? std::filesystem::temp_directory_path(ec) : FileHelper::ToFsPath(folder);
    if (ec || dir.empty())
        return {};
    std::filesystem::create_directories(dir, ec);
    // Temp journals of earlier runs that ended without removing theirs (Windows: one still open by a running
    // process cannot be removed, so only the stale ones go)
    static std::once_flag once;
    std::call_once(once, [&dir] {
        std::error_code e;
        for (std::filesystem::directory_iterator it(dir, e), end; !e && it != end; it.increment(e))
        {
            const std::string name = it->path().filename().string();
            if (name.rfind("unreal-ng-session-", 0) == 0 && it->path().extension() == ".spill")
            {
                std::error_code ignored;
                std::filesystem::remove(it->path(), ignored);
            }
        }
    });
    static std::atomic<uint64_t> counter{0};
    static const uint64_t process = std::random_device{}() * 0x9E3779B97F4A7C15ULL;
    char name[80];
    std::snprintf(name, sizeof name, "unreal-ng-session-%016llx-%llu.spill", static_cast<unsigned long long>(process),
                  static_cast<unsigned long long>(counter.fetch_add(1)));
    return dir / name;
}

/// endregion </SessionJournalFile>

/// region <JournalStrand>

void JournalStrand::Post(std::unique_ptr<JournalBatch> batch)
{
    bool schedule = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push_back(std::move(batch));
        if (!_scheduled)
        {
            _scheduled = true;
            schedule = true;
        }
    }
    if (schedule)
        JournalIoPool::Instance().Schedule(shared_from_this());
}

std::vector<std::pair<uint64_t, bool>> JournalStrand::TakeDone()
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<std::pair<uint64_t, bool>> done;
    done.swap(_done);
    return done;
}

void JournalStrand::WaitIdle()
{
    std::unique_lock<std::mutex> lock(_mutex);
    _idle.wait(lock, [this] { return _queue.empty() && !_scheduled; });
}

bool JournalStrand::RunOne()
{
    std::unique_ptr<JournalBatch> batch;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty())
        {
            _scheduled = false;
            _idle.notify_all();
            return false;
        }
        batch = std::move(_queue.front());
        _queue.pop_front();
    }
    bool ok = batch->file != nullptr;
    auto write = [&](const std::vector<JournalBatch::Piece>& pieces) {
        for (const JournalBatch::Piece& piece : pieces)
        {
            if (!ok)
                return;
            const uint8_t* data = piece.data ? piece.data : batch->owned.data() + piece.ownedAt;
            ok = batch->file->WriteAt(piece.offset, data, piece.size);
        }
    };
    // The sectors before the headers that name them: a crash in between leaves the old headers
    write(batch->data);
    write(batch->headers);
    if (ok && batch->sync)
        ok = batch->file->Sync();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _done.emplace_back(batch->ticket, ok);
    }
    return true;
}

/// endregion </JournalStrand>

/// region <JournalIoPool>

JournalIoPool& JournalIoPool::Instance()
{
    static JournalIoPool pool;
    return pool;
}

JournalIoPool::JournalIoPool()
{
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const unsigned threads = std::clamp(cores / 4, 1u, 4u);
    for (unsigned i = 0; i < threads; i++)
        _threads.emplace_back([this] { Worker(); });
}

JournalIoPool::~JournalIoPool()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
    }
    _wake.notify_all();
    for (std::thread& t : _threads)
        t.join();
}

void JournalIoPool::Schedule(std::shared_ptr<JournalStrand> strand)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _ready.push_back(std::move(strand));
    }
    _wake.notify_one();
}

void JournalIoPool::Worker()
{
    for (;;)
    {
        std::shared_ptr<JournalStrand> strand;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _wake.wait(lock, [this] { return _stop || !_ready.empty(); });
            if (_ready.empty())
                return;  // stopping, nothing left
            strand = std::move(_ready.front());
            _ready.pop_front();
        }
        // A strand runs on one thread at a time: its batches stay in order. A few batches, then the next strand
        // gets a turn (many emulator instances share the pool)
        bool more = true;
        for (int i = 0; i < 4 && more; i++)
            more = strand->RunOne();
        if (more)
            Schedule(std::move(strand));
    }
}

/// endregion </JournalIoPool>
