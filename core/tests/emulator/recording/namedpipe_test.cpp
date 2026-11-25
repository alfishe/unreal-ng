// NamedPipe: the writer's end of a FIFO / Windows named pipe (core/recording/src/common/named_pipe.h). Nothing waits
// on a reader that is gone or never came, the bytes reach the reader whole (also after the writer closed), and pipes
// of several recordings (several emulators) never meet

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "common/named_pipe.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
/// The reader's end, as ffmpeg opens it (a plain file open of the reader path, blocking reads)
class Reader
{
public:
    ~Reader() { close(); }

    /// Unix: blocks until the writer opens (as ffmpeg's open does); Windows: the pipe is there
    bool open(const std::string& path)
    {
#ifdef _WIN32
        _handle = CreateFileA(NamedPipe::readerPath(path).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        return _handle != INVALID_HANDLE_VALUE;
#else
        do
            _fd = ::open(NamedPipe::readerPath(path).c_str(), O_RDONLY);
        while (_fd < 0 && errno == EINTR);
        return _fd >= 0;
#endif
    }

    /// Everything up to the end (the writer closed)
    std::vector<uint8_t> readAll()
    {
        std::vector<uint8_t> all;
        uint8_t buffer[65536];
        for (;;)
        {
#ifdef _WIN32
            DWORD got = 0;
            if (!ReadFile(_handle, buffer, sizeof(buffer), &got, nullptr) || got == 0)
                return all;  // ERROR_BROKEN_PIPE: the end
#else
            const ssize_t got = ::read(_fd, buffer, sizeof(buffer));
            if (got < 0 && errno == EINTR)
                continue;
            if (got <= 0)
                return all;
#endif
            all.insert(all.end(), buffer, buffer + got);
        }
    }

    void close()
    {
#ifdef _WIN32
        if (_handle != INVALID_HANDLE_VALUE)
            CloseHandle(_handle);
        _handle = INVALID_HANDLE_VALUE;
#else
        if (_fd >= 0)
            ::close(_fd);
        _fd = -1;
#endif
    }

private:
#ifdef _WIN32
    HANDLE _handle = INVALID_HANDLE_VALUE;
#else
    int _fd = -1;
#endif
};

std::vector<uint8_t> Pattern(size_t size, uint32_t seed)
{
    std::vector<uint8_t> bytes(size);
    uint32_t value = seed * 2654435761u + 1;
    for (uint8_t& byte : bytes)
    {
        value = value * 1664525u + 1013904223u;
        byte = static_cast<uint8_t>(value >> 24);
    }
    return bytes;
}

class NamedPipe_Test : public ::testing::Test
{
protected:
    /// A name for a new pipe: a Windows pipe name, or a FIFO in the test process's scratch directory
    static std::string Name(const std::string& tag)
    {
#ifdef _WIN32
        return NamedPipe::uniqueName(tag);
#else
        return TestPathHelper::GetUniqueTestScratchPath("namedpipe-" + tag + ".fifo");
#endif
    }
};
}  // namespace

TEST_F(NamedPipe_Test, NamesAreUnique)
{
    std::set<std::string> names;
    for (int i = 0; i < 1000; i++)
        names.insert(NamedPipe::uniqueName("sound"));
    EXPECT_EQ(names.size(), 1000u);
    EXPECT_EQ(NamedPipe::uniqueName("sound").rfind("unreal-ng-sound-", 0), 0u);
}

TEST_F(NamedPipe_Test, NoReaderNeverWaits)
{
    NamedPipe pipe;
    ASSERT_TRUE(pipe.create(Name("noreader"))) << pipe.getLastError();
    EXPECT_FALSE(pipe.tryOpenForWrite());
    EXPECT_FALSE(pipe.failed()) << "no reader yet is not a failure";
    EXPECT_EQ(pipe.tryWrite("x", 1), 0) << "nothing taken before the reader is there";

    EXPECT_FALSE(pipe.openForWriteTimeout(30));
    EXPECT_TRUE(pipe.failed());
    EXPECT_NE(pipe.getLastError().find("Timed out"), std::string::npos) << pipe.getLastError();

    NamedPipe aborted;
    ASSERT_TRUE(aborted.create(Name("aborted"))) << aborted.getLastError();
    const std::atomic<bool> abort{true};
    EXPECT_FALSE(aborted.openForWriteTimeout(60000, &abort)) << "the abort flag ends the wait";
    EXPECT_NE(aborted.getLastError().find("aborted"), std::string::npos) << aborted.getLastError();
}

TEST_F(NamedPipe_Test, ClosedPipeKeepsWhatTheReaderHasNotRead)
{
    NamedPipe pipe;
    const std::string name = Name("closed");
    ASSERT_TRUE(pipe.create(name)) << pipe.getLastError();
    Reader reader;
    std::atomic<bool> opened{false}, opening{true};
    std::thread thread([&] {
        opened = reader.open(name);
        opening = false;
    });
    ASSERT_TRUE(TestWait::For([&] { return pipe.tryOpenForWrite() || pipe.failed(); })) << "the reader came";
    ASSERT_TRUE(pipe.isOpen()) << pipe.getLastError();
    ASSERT_TRUE(TestWait::For([&] { return !opening.load(); }));
    thread.join();
    ASSERT_TRUE(opened);

    // Less than any pipe's buffer: all of it is taken while the reader reads nothing
    const std::vector<uint8_t> sent = Pattern(1000, 1);
    size_t done = 0;
    ASSERT_TRUE(TestWait::For([&] {
        const int64_t n = pipe.tryWrite(sent.data() + done, sent.size() - done);
        if (n > 0)
            done += static_cast<size_t>(n);
        return n < 0 || done == sent.size();
    }));
    ASSERT_EQ(done, sent.size()) << pipe.getLastError();
    ASSERT_TRUE(TestWait::For([&] { return pipe.flushed(); }));
    pipe.close();

    EXPECT_EQ(reader.readAll(), sent) << "the bytes in the pipe outlive the writer's end, then the end";
}

TEST_F(NamedPipe_Test, FullPipeNeverBlocksAndAbortEndsAWrite)
{
    NamedPipe pipe;
    const std::string name = Name("full");
    ASSERT_TRUE(pipe.create(name)) << pipe.getLastError();
    Reader reader;  // opens, never reads
    std::thread thread([&] { reader.open(name); });
    ASSERT_TRUE(TestWait::For([&] { return pipe.tryOpenForWrite() || pipe.failed(); }));
    thread.join();
    ASSERT_TRUE(pipe.isOpen()) << pipe.getLastError();

    const std::vector<uint8_t> chunk = Pattern(64 * 1024, 2);
    uint64_t taken = 0;
    bool full = false;
    for (int i = 0; i < 4096 && !full; i++)  // 256 MB at most: far more than a pipe holds
    {
        const int64_t n = pipe.tryWrite(chunk.data(), chunk.size());
        ASSERT_GE(n, 0) << pipe.getLastError();
        full = n == 0;
        taken += static_cast<uint64_t>(n);
    }
    EXPECT_TRUE(full) << "a pipe nobody reads fills, and the next write returns at once (" << taken << " bytes in)";
    EXPECT_GT(taken, 0u);
#ifdef _WIN32
    EXPECT_FALSE(pipe.flushed()) << "the last write is in flight: closing now would drop it";
#endif
}

TEST_F(NamedPipe_Test, BlockingWriteEndsOnAbort)
{
    NamedPipe pipe;
    const std::string name = Name("abortwrite");
    ASSERT_TRUE(pipe.create(name)) << pipe.getLastError();
    Reader reader;  // opens, never reads
    std::thread thread([&] { reader.open(name); });
    ASSERT_TRUE(pipe.openForWriteTimeout(5000)) << pipe.getLastError();
    thread.join();

    // More than the pipe holds: the write waits for room until the flag is raised
    std::atomic<bool> abort{false};
    std::atomic<bool> writing{false}, returned{false}, result{true};
    const std::vector<uint8_t> big = Pattern(64 * 1024 * 1024, 3);
    std::thread writer([&] {
        writing = true;
        result = pipe.write(big.data(), big.size(), &abort);
        returned = true;
    });
    ASSERT_TRUE(TestWait::For([&] { return writing.load(); }));
    EXPECT_FALSE(returned) << "the pipe cannot hold 64 MB: the write waits";
    abort = true;
    const bool ended = TestWait::For([&] { return returned.load(); });
    if (!ended)
        reader.close();  // ends the write (the reader is gone), so the test fails instead of hanging
    writer.join();
    EXPECT_TRUE(ended) << "the abort flag ends a blocked write";
    EXPECT_FALSE(result);
}

TEST_F(NamedPipe_Test, ReaderGoneIsAFailureNotAWait)
{
    NamedPipe pipe;
    const std::string name = Name("gone");
    ASSERT_TRUE(pipe.create(name)) << pipe.getLastError();
    Reader reader;
    std::thread thread([&] { reader.open(name); });
    ASSERT_TRUE(TestWait::For([&] { return pipe.tryOpenForWrite() || pipe.failed(); }));
    thread.join();
    reader.close();

    const std::vector<uint8_t> chunk = Pattern(4096, 4);
    int64_t n = 0;
    for (int i = 0; i < 16 && n >= 0; i++)  // the first write may still be taken (Windows: in flight)
    {
        n = pipe.tryWrite(chunk.data(), chunk.size());
        if (n >= 0)
            pipe.flushed();
    }
    EXPECT_LT(n, 0) << "the reader is gone";
    EXPECT_TRUE(pipe.failed());
    EXPECT_NE(pipe.getLastError().find("reader has exited"), std::string::npos) << pipe.getLastError();
    EXPECT_FALSE(pipe.write(chunk.data(), chunk.size())) << "a blocking write fails too, it does not wait";
}

/// Several recordings at once (several emulators): one writer thread feeds every pipe without waiting on any, every
/// reader gets its own bytes and only those
TEST_F(NamedPipe_Test, ManyPipesAtOnce)
{
    constexpr int kPipes = 6;
    constexpr size_t kBytes = 3 * 1024 * 1024 / 2;  // more than any pipe's buffer: the writer meets full pipes
    NamedPipe pipes[kPipes];
    Reader readers[kPipes];
    std::vector<uint8_t> sent[kPipes], received[kPipes];
    std::string names[kPipes];
    for (int i = 0; i < kPipes; i++)
    {
        names[i] = Name("many" + std::to_string(i));
        ASSERT_TRUE(pipes[i].create(names[i])) << pipes[i].getLastError();
        sent[i] = Pattern(kBytes, 10 + static_cast<uint32_t>(i));
    }
    std::atomic<int> readersDone{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < kPipes; i++)
        threads.emplace_back([&, i] {
            if (readers[i].open(names[i]))
                received[i] = readers[i].readAll();
            readersDone++;
        });

    size_t done[kPipes] = {};
    bool closed[kPipes] = {};
    const bool finished = TestWait::For(
        [&] {
            bool all = true;
            for (int i = 0; i < kPipes; i++)
            {
                if (closed[i] || pipes[i].failed() || !pipes[i].tryOpenForWrite())
                {
                    all = all && (closed[i] || pipes[i].failed());
                    continue;
                }
                while (done[i] < kBytes)
                {
                    const int64_t n = pipes[i].tryWrite(sent[i].data() + done[i], kBytes - done[i]);
                    if (n <= 0)
                        break;
                    done[i] += static_cast<size_t>(n);
                }
                if (done[i] == kBytes && pipes[i].flushed())
                {
                    pipes[i].close();
                    closed[i] = true;
                }
                all = all && closed[i];
            }
            return all;
        },
        std::chrono::milliseconds(20000));
    if (!finished)
        for (NamedPipe& pipe : pipes)
            pipe.close();  // the readers end, so the test fails instead of hanging
    for (std::thread& thread : threads)
        thread.join();
    ASSERT_TRUE(finished);
    EXPECT_EQ(readersDone.load(), kPipes);
    for (int i = 0; i < kPipes; i++)
    {
        EXPECT_FALSE(pipes[i].failed()) << i << ": " << pipes[i].getLastError();
        EXPECT_TRUE(received[i] == sent[i]) << "pipe " << i << ": " << received[i].size() << " bytes of " << kBytes;
    }
}

#ifdef _WIN32
TEST_F(NamedPipe_Test, NameInUseIsRefused)
{
    const std::string name = Name("taken");
    NamedPipe first, second;
    ASSERT_TRUE(first.create(name)) << first.getLastError();
    EXPECT_FALSE(second.create(name)) << "a name another pipe has is not shared";
    EXPECT_TRUE(second.failed());
}

TEST_F(NamedPipe_Test, OnlyTheReaderProcessMayOpen)
{
    const std::string name = Name("foreign");
    NamedPipe pipe;
    ASSERT_TRUE(pipe.create(name)) << pipe.getLastError();
    pipe.setReaderProcess(GetCurrentProcessId() + 4);  // not us: our own open is the foreign one
    Reader reader;
    ASSERT_TRUE(reader.open(name));
    EXPECT_TRUE(TestWait::For([&] { return pipe.tryOpenForWrite() || pipe.failed(); }));
    EXPECT_FALSE(pipe.isOpen());
    EXPECT_TRUE(pipe.failed());
    EXPECT_NE(pipe.getLastError().find("Another process"), std::string::npos) << pipe.getLastError();
}
#endif
