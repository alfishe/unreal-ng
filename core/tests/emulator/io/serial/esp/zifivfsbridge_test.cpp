// The ZIFI-NATIVE VFS client (zifivfsbridge.h) against an in-memory Z80 plugin (fakezifiplugin.h): every operation
// the FTP / WebDAV servers use, both firmwares' line forms (S3 windows and batches, the old / ESP-01S 512-byte
// exchanges), byte-exact round trips, errors, timeouts and the TTD state. Sources: ZiFi-ESP32-S3-Zero 2e5ba83
// src/vfs_client.cpp, src/vfs_bridge.cpp; ZiFi-ESP-01S-Native-C-Project 90834e4 src/vfs_client.cpp.

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

#include "_helpers/fakezifiplugin.h"
#include "emulator/io/serial/esp/zifistate.h"
#include "emulator/io/serial/esp/zifivfsbridge.h"

class ZiFiVfsBridge_Test : public ::testing::Test
{
protected:
    using Op = ZiFiVfsBridge::Op;
    using Bytes = std::vector<uint8_t>;

    void Make(bool s3)
    {
        _vfs = std::make_unique<ZiFiVfsBridge>(s3);
        _vfs->now = [this]() { return _now; };
        _vfs->micros = [](uint64_t us) { return us; };   // 1 T-state = 1 us here
        _vfs->sendFrame = [this](uint8_t cmd, const Bytes& data) { _requests.push_back({cmd, data}); };
    }
    void SetUp() override { Make(true); }

    /// Deliver the requests to the plugin and its answers back until the operation is done (or nothing moves)
    ZiFiVfsBridge::Result Run()
    {
        ZiFiVfsBridge::Result r;
        for (int guard = 0; guard < 100000; ++guard)
        {
            if (_vfs->TakeResult(r))
                return r;
            if (!_answers.empty())
            {
                const FakeZiFiPlugin::Frame f = _answers.front();
                _answers.pop_front();
                EXPECT_TRUE(_vfs->Waiting());
                EXPECT_EQ(f.cmd, _vfs->AwaitedCommand());
                _vfs->OnResponse(f.data);
                continue;
            }
            if (!_requests.empty())
            {
                const FakeZiFiPlugin::Frame q = _requests.front();
                _requests.pop_front();
                for (FakeZiFiPlugin::Frame& a : _plugin.Handle(q.cmd, q.data))
                    _answers.push_back(std::move(a));
                continue;
            }
            // Nothing to deliver: time passes
            _now += 1000000;
            _vfs->Tick();
        }
        ADD_FAILURE() << "the operation did not finish";
        return r;
    }

    ZiFiVfsBridge::Result Do(Op op, const std::string& path = {}, uint32_t value = 0)
    {
        EXPECT_TRUE(_vfs->Submit(op, path, value));
        return Run();
    }

    static Bytes Pattern(size_t n, uint32_t seed = 7)
    {
        Bytes b(n);
        for (size_t i = 0; i < n; ++i)
        {
            seed = seed * 1103515245u + 12345u;
            b[i] = static_cast<uint8_t>(seed >> 16);
        }
        return b;
    }

    /// Read a whole file through Open / Read / Close (the RETR loop)
    Bytes ReadAll(const std::string& path, uint32_t size, uint32_t chunk)
    {
        Bytes out;
        EXPECT_TRUE(Do(Op::OpenRead, path).success);
        while (out.size() < size)
        {
            const ZiFiVfsBridge::Result r = Do(Op::Read, {}, std::min<uint32_t>(chunk, size - static_cast<uint32_t>(out.size())));
            EXPECT_TRUE(r.success) << r.error;
            if (!r.success || r.transferred == 0)
                break;
            uint8_t buffer[1024];
            size_t n;
            while ((n = _vfs->ReadForNetwork(buffer, sizeof(buffer))) != 0)
                out.insert(out.end(), buffer, buffer + n);
        }
        EXPECT_TRUE(Do(Op::CloseCommit).success);
        return out;
    }

    /// Write a whole file (the STOR loop: S3 16 KiB windows, E01 256-byte slots)
    void WriteAll(const std::string& path, const Bytes& data, uint32_t chunk)
    {
        ASSERT_TRUE(Do(Op::OpenWrite, path).success);
        size_t at = 0;
        while (at < data.size())
        {
            const size_t n = std::min<size_t>(chunk, data.size() - at);
            ASSERT_EQ(_vfs->WriteFromNetwork(data.data() + at, n), n);
            const ZiFiVfsBridge::Result r = Do(Op::Write, {}, static_cast<uint32_t>(n));
            ASSERT_TRUE(r.success) << r.error;
            ASSERT_EQ(r.transferred, n);
            at += n;
        }
        const ZiFiVfsBridge::Result c = Do(Op::CloseCommit);
        ASSERT_TRUE(c.success) << c.error;
    }

    size_t Count(uint8_t cmd) const
    {
        size_t n = 0;
        for (uint8_t c : _plugin.commands)
            n += c == cmd;
        return n;
    }

    uint64_t _now = 1000;
    std::unique_ptr<ZiFiVfsBridge> _vfs;
    FakeZiFiPlugin _plugin;
    std::deque<FakeZiFiPlugin::Frame> _requests;
    std::deque<FakeZiFiPlugin::Frame> _answers;
};

TEST_F(ZiFiVfsBridge_Test, StatFileDirectoryAndMissing)
{
    _plugin.Put("/A.TXT", Pattern(1234));
    _plugin.Mkdir("/GAMES");
    ZiFiVfsBridge::Result r = Do(Op::Stat, "/A.TXT");
    ASSERT_TRUE(r.success);
    EXPECT_FALSE(r.isDirectory);
    EXPECT_EQ(r.size, 1234u);
    EXPECT_TRUE(r.hasMetadata) << "the new plugin's FILEX GET_METADATA after the six bytes";
    EXPECT_EQ(r.writeDate, 0x5944);
    EXPECT_EQ(r.writeTime, 0x6000);
    EXPECT_TRUE(Do(Op::Stat, "/GAMES").isDirectory);
    r = Do(Op::Stat, "/NONE");
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.error, "stat-4");
    EXPECT_FALSE(_vfs->RequestPending());
    EXPECT_FALSE(_vfs->Submit(Op::Stat, std::string(300, 'x'))) << "request.path[256]";
}

TEST_F(ZiFiVfsBridge_Test, DirectoryOneEntryPerRequestAndBatched)
{
    _plugin.Mkdir("/D");
    for (int i = 0; i < 20; ++i)
        _plugin.Put("/D/F" + std::to_string(i) + ".BIN", Pattern(static_cast<size_t>(i)));
    for (bool batch : {false, true})
    {
        _plugin.batch = batch;
        const size_t before = Count(0x42);
        ASSERT_TRUE(Do(Op::OpenDirectory, "/D").success);
        std::vector<std::string> names;
        for (;;)
        {
            const ZiFiVfsBridge::Result r = Do(Op::ReadDirectory);
            ASSERT_TRUE(r.success) << r.error;
            if (r.atEnd)
                break;
            names.push_back(r.name);
            EXPECT_EQ(r.writeDate, 0x5944) << "the date after the name's zero";
        }
        EXPECT_EQ(names.size(), 20u);
        EXPECT_EQ(Count(0x42) - before, batch ? 2u : 21u) << "a batch of 16, then the rest with the end summary";
    }
}

TEST_F(ZiFiVfsBridge_Test, ReadWindowsByteExact)
{
    const Bytes file = Pattern(40000);
    _plugin.Put("/BIG.BIN", file);
    EXPECT_EQ(ReadAll("/BIG.BIN", 40000, 16384), file);
    EXPECT_EQ(Count(0x58), 3u) << "16 KiB windows";
    EXPECT_EQ(Count(0x51), 0u);
}

TEST_F(ZiFiVfsBridge_Test, OldPluginReads512ByteBlocks)
{
    _plugin.windows = false;
    const Bytes file = Pattern(5000);
    _plugin.Put("/OLD.BIN", file);
    EXPECT_EQ(ReadAll("/OLD.BIN", 5000, 16384), file);
    EXPECT_EQ(Count(0x51), 10u) << "the window in READs of 512";
}

TEST_F(ZiFiVfsBridge_Test, WriteWindowsByteExact)
{
    const Bytes file = Pattern(50000, 3);
    WriteAll("/UP.BIN", file, 16384);
    ASSERT_NE(_plugin.Get("/UP.BIN"), nullptr);
    EXPECT_EQ(*_plugin.Get("/UP.BIN"), file);
    EXPECT_EQ(Count(0x56), 0u);
}

TEST_F(ZiFiVfsBridge_Test, OldPluginWritesBlocksAndTheTailAtClose)
{
    _plugin.windows = false;
    const Bytes file = Pattern(1300, 5);
    WriteAll("/UP.BIN", file, 16384);
    EXPECT_EQ(*_plugin.Get("/UP.BIN"), file);
    // 2 full blocks (each 248 + 252 + 12 in three fragments) at Write, the 276-byte tail (248 + 28) at CLOSE
    EXPECT_EQ(Count(0x56), 8u);
}

TEST_F(ZiFiVfsBridge_Test, Esp01sReadWriteDeleteMkdir)
{
    Make(false);
    _plugin.windows = false;
    _plugin.filex = false;
    _plugin.dates = false;
    const Bytes file = Pattern(3000, 11);
    WriteAll("/E.BIN", file, 256);   // the four 256-byte slots of the E01 STOR
    EXPECT_EQ(*_plugin.Get("/E.BIN"), file);
    EXPECT_EQ(ReadAll("/E.BIN", 3000, 512), file);
    ZiFiVfsBridge::Result st = Do(Op::Stat, "/E.BIN");
    EXPECT_EQ(st.size, 3000u);
    EXPECT_FALSE(st.hasMetadata);
    EXPECT_TRUE(Do(Op::Mkdir, "/NEW").success);
    EXPECT_FALSE(Do(Op::Mkdir, "/NEW").success);
    EXPECT_TRUE(Do(Op::Delete, "/E.BIN").success);
    st = Do(Op::Delete, "/E.BIN");
    EXPECT_FALSE(st.success);
    EXPECT_EQ(st.error, "delete-4");
    // E01 READDIR: a short or non-zero answer is the end
    ASSERT_TRUE(Do(Op::OpenDirectory, "/").success);
    EXPECT_EQ(Do(Op::ReadDirectory).name, "NEW");
    EXPECT_TRUE(Do(Op::ReadDirectory).atEnd);
}

TEST_F(ZiFiVfsBridge_Test, ErrorsAndTimeouts)
{
    _plugin.Put("/A.BIN", Pattern(100));
    // OPEN refused by the plugin
    _plugin.failCmd = 0x50;
    _plugin.failStatus = 0x15;
    ZiFiVfsBridge::Result r = Do(Op::OpenRead, "/A.BIN");
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.error, "open-21");
    EXPECT_EQ(r.status, 0x15);
    _plugin.failCmd = 0;
    // A lost answer: the client's 5 s timeout
    _plugin.swallowCmd = 0x40;
    const uint64_t start = _now;
    r = Do(Op::Stat, "/A.BIN");
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.error, "timeout-40");
    EXPECT_GE(_now - start, 5000000u);
    EXPECT_EQ(_vfs->Timeouts(), 1u);
    _plugin.swallowCmd = 0;
    // A write window the Z80 rejects: the write fails, the commit becomes an abort
    ASSERT_TRUE(Do(Op::OpenWrite, "/W.BIN").success);
    const Bytes data = Pattern(2000);
    _vfs->WriteFromNetwork(data.data(), data.size());
    _plugin.failCmd = 0x57;
    _plugin.failStatus = 0x22;
    r = Do(Op::Write, {}, 2000);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.error, "block-status-34");
    _plugin.failCmd = 0;
    r = Do(Op::CloseAbort);
    EXPECT_TRUE(r.success);
    EXPECT_EQ(_vfs->FromNetAvailable(), 0u) << "the abort drops the unwritten ring";
    // CLOSE with bytes still in the ring is refused (ingress-pending)
    ASSERT_TRUE(Do(Op::OpenWrite, "/W2.BIN").success);
    _vfs->WriteFromNetwork(data.data(), 10);
    r = Do(Op::CloseCommit);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.error, "ingress-pending");
}

TEST_F(ZiFiVfsBridge_Test, RandomAccessMetadata)
{
    _plugin.Put("/M.TXT", Pattern(10));
    ASSERT_TRUE(Do(Op::OpenRandom, "/M.TXT").success);
    ZiFiVfsBridge::Metadata m;
    m.timeMask = 0x04;
    m.writeDate = 0x1234;
    m.writeTime = 0x5678;
    ASSERT_TRUE(_vfs->SubmitMetadata(m));
    const ZiFiVfsBridge::Result r = Run();
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.appliedAttributes, 0x20);
    EXPECT_TRUE(Do(Op::CloseCommit).success);
    EXPECT_EQ(_plugin.files["/M.TXT"].date, 0x1234);
    EXPECT_EQ(_plugin.files["/M.TXT"].time, 0x5678);
    // Without FILEX the random open is refused before any metadata goes out
    _plugin.filex = false;
    const ZiFiVfsBridge::Result o = Do(Op::OpenRandom, "/M.TXT");
    EXPECT_FALSE(o.success);
    EXPECT_EQ(o.error, "open-filex-unsupported");
}

TEST_F(ZiFiVfsBridge_Test, StateSurvivesASaveInTheMiddleOfAWindow)
{
    const Bytes file = Pattern(20000, 9);
    _plugin.Put("/T.BIN", file);
    ASSERT_TRUE(Do(Op::OpenRead, "/T.BIN").success);
    ASSERT_TRUE(_vfs->Submit(Op::Read, {}, 16384));
    // The request went out; half of the answer frames arrive, then a checkpoint
    const FakeZiFiPlugin::Frame q = _requests.front();
    _requests.pop_front();
    std::vector<FakeZiFiPlugin::Frame> frames = _plugin.Handle(q.cmd, q.data);
    ASSERT_GT(frames.size(), 4u);
    for (size_t i = 0; i < 4; ++i)
        _vfs->OnResponse(frames[i].data);
    std::vector<uint8_t> blob;
    ZiFiStateWriter w(blob);
    _vfs->Save(w);
    // A fresh client restored from it takes the rest
    Make(true);
    ZiFiStateReader r(blob.data(), blob.size());
    ASSERT_TRUE(_vfs->Load(r));
    EXPECT_TRUE(_vfs->Waiting());
    for (size_t i = 4; i < frames.size(); ++i)
        _vfs->OnResponse(frames[i].data);
    ZiFiVfsBridge::Result res;
    ASSERT_TRUE(_vfs->TakeResult(res));
    ASSERT_TRUE(res.success) << res.error;
    EXPECT_EQ(res.transferred, 16384u);
    Bytes got(16384);
    EXPECT_EQ(_vfs->ReadForNetwork(got.data(), got.size()), 16384u);
    EXPECT_EQ(got, Bytes(file.begin(), file.begin() + 16384));
    // A foreign blob loads as nothing
    const uint8_t junk[3] = {9, 9, 9};
    ZiFiStateReader bad(junk, sizeof(junk));
    EXPECT_FALSE(_vfs->Load(bad));
    EXPECT_FALSE(_vfs->RequestPending());
}
