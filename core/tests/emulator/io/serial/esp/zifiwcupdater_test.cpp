// The ZiFi S3 firmware's Wild Commander updater (zifiwcupdater.h) through the whole module: WCU_START / APPLY /
// STOP / SYNC from the Z80, the GitHub API and raw.githubusercontent.com served by a fake host over the virtual
// network (TLS is the host's: the slots carry plaintext), the SD card in an in-memory Wild Commander plugin
// (fakezifiplugin.h). Check, apply (FILEX MOVE and the RENAME fallback), protected files, the errors the plugin
// shows, the stop that holds a command, and the TTD state. Source: ZiFi-ESP32-S3-Zero 2e5ba83 src/wc_updater.cpp,
// src/wc_update_service.cpp, src/main.cpp, docs/PROTOCOL.md.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "_helpers/fakehostnet.h"
#include "_helpers/fakezifiplugin.h"
#include "common/network/dnsmessage.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/esp/zifigit.h"
#include "emulator/io/serial/esp/zifinativemodule.h"
#include "emulator/io/serial/esp/zifiwcupdater.h"

class ZiFiWcUpdater_Test : public ::testing::Test
{
protected:
    using Bytes = std::vector<uint8_t>;
    static constexpr const char* kCommit = "0123456789abcdef0123456789abcdef01234567";

    struct Frame
    {
        uint8_t cmd = 0;
        Bytes data;
        std::string Text(size_t from) const { return from < data.size() ? std::string(data.begin() + static_cast<std::ptrdiff_t>(from), data.end()) : std::string(); }
    };

    void SetUp() override
    {
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), VirtualNetworkConfig());
        _esp = std::make_unique<ZiFiNativeModule>(_net.get(), ZiFiNativeModule::Variant::S3);
        _esp->SetClock([this]() { return _now; }, 3500000);
        _esp->OnLineSettings(SerialLine());
        _plugin.statMissing = 1;   // Wild Commander's plugin: "stat-1" is "no such name"
        Send(ZiFiNativeModule::kWifiConnect, Bytes({'U', 'n', 'r', 'e', 'a', 'l', 'N', 'G', 0}));
        Run(100);
        _frames.clear();
    }

    void Send(uint8_t cmd, const Bytes& data = {})
    {
        for (uint8_t b : ZiFiNativeModule::Frame(cmd, data))
            _esp->Transmit(b);
    }

    /// The repository on "GitHub": path -> bytes (folders follow from the paths)
    void Remote(const std::string& path, const std::string& data) { _remote[path] = Bytes(data.begin(), data.end()); }

    static std::string BlobSha(const Bytes& data)
    {
        zifigit::Sha1 sha;
        zifigit::GitBlobBegin(sha, static_cast<uint32_t>(data.size()));
        sha.Update(data.data(), data.size());
        uint8_t digest[zifigit::Sha1::kDigestSize];
        sha.Finish(digest);
        return zifigit::FormatSha1Hex(digest);
    }

    std::string TreeJson() const
    {
        std::set<std::string> folders;
        std::string items;
        for (const auto& [path, data] : _remote)
        {
            for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1))
                folders.insert(path.substr(0, slash));
            items += std::string(items.empty() ? "" : ",") + R"({"path":")" + path + R"(","mode":"100644","type":"blob","sha":")" +
                     BlobSha(data) + R"(","size":)" + std::to_string(data.size()) + "}";
        }
        for (const std::string& folder : folders)
            items += R"(,{"path":")" + folder + R"(","mode":"040000","type":"tree","sha":")" + std::string(40, 'f') + R"("})";
        return R"({"sha":"x","url":"u","tree":[)" + items + R"(],"truncated":false})";
    }

    static std::string Decode(const std::string& s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '%' && i + 2 < s.size())
            {
                out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
                i += 2;
            }
            else
                out.push_back(s[i]);
        }
        return out;
    }

    std::string Answer(const std::string& host, const std::string& path)
    {
        _requests.push_back(host + path);
        const auto ok = [](const std::string& body) {
            return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        };
        if (host == "api.github.com")
        {
            if (_apiStatus != 200)
                return "HTTP/1.1 " + std::to_string(_apiStatus) + " Forbidden\r\nContent-Length: 2\r\n\r\n{}";
            if (path == "/repos/zx/wc/git/ref/heads/main")
                return ok(std::string(R"({"ref":"refs/heads/main","object":{"sha":")") + kCommit + R"(","type":"commit"}})");
            if (path == std::string("/repos/zx/wc/git/trees/") + kCommit + ":wc?recursive=1")
                return ok(TreeJson());
        }
        const std::string prefix = std::string("/zx/wc/") + kCommit + "/wc/";
        if (host == "raw.githubusercontent.com" && _holdRaw)
            return {};   // the server keeps the connection silent
        if (host == "raw.githubusercontent.com" && path.rfind(prefix, 0) == 0)
        {
            auto it = _remote.find(Decode(path.substr(prefix.size())));
            if (it != _remote.end())
            {
                std::string body(it->second.begin(), it->second.end());
                if (_corruptDownloads > 0)
                {
                    --_corruptDownloads;
                    body[0] ^= 1;
                }
                return ok(body);
            }
        }
        return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    }

    /// The fake GitHub: DNS answers, TLS connects accepted, one request per connection answered and closed
    void Serve()
    {
        for (size_t i = _served; i < _host->commands.size(); ++i)
        {
            const FakeHostNet::Command c = _host->commands[i];
            if (c.op == "dns")
            {
                dns::Question question;
                ASSERT_TRUE(dns::ParseQuery(c.data.data(), c.data.size(), question));
                _host->Push(NetEventType::Datagram, c.socket, NetEventStatus::Ok, c.endpoint,
                            dns::BuildAnswer(c.data.data(), c.data.size(), question, {NetIp(140, 82, 121, 6)}, dns::kRcodeNoError));
            }
            else if (c.op == "connect-tls")
            {
                _sni[c.socket] = std::string(c.data.begin(), c.data.end());
                _host->Push(NetEventType::Connected, c.socket);
            }
            else if (c.op == "send" && _sni.count(c.socket))
            {
                const std::string request(c.data.begin(), c.data.end());
                const size_t space = request.find(' ');
                const std::string path = request.substr(space + 1, request.find(' ', space + 1) - space - 1);
                const std::string response = Answer(_sni[c.socket], path);
                _sni.erase(c.socket);
                if (response.empty())
                    continue;
                _host->Push(NetEventType::Data, c.socket, NetEventStatus::Ok, {}, Bytes(response.begin(), response.end()));
                _host->Push(NetEventType::PeerClosed, c.socket);
            }
        }
        _served = _host->commands.size();
    }

    /// The module's output: VFS requests to the plugin, the rest kept
    void Drain()
    {
        for (int guard = 0; guard < 100000; ++guard)
        {
            bool moved = false;
            while (_esp->HasByte())
            {
                _pending.push_back(_esp->TakeByte());
                moved = true;
            }
            while (_pending.size() >= 5)
            {
                const size_t length = static_cast<size_t>(_pending[2] | (_pending[3] << 8));
                if (_pending.size() < 5 + length)
                    break;
                const uint8_t cmd = _pending[1];
                Bytes data(_pending.begin() + 4, _pending.begin() + 4 + static_cast<std::ptrdiff_t>(length));
                _pending.erase(_pending.begin(), _pending.begin() + 5 + static_cast<std::ptrdiff_t>(length));
                if (ZiFiVfsBridge::IsVfsCommand(cmd))
                {
                    if (!_z80Asleep)
                    {
                        for (const FakeZiFiPlugin::Frame& f : _plugin.Handle(cmd, data))
                            Send(f.cmd, f.data);
                    }
                    else
                        _asleep.push_back({cmd, data});
                }
                else
                    _frames.push_back({cmd, data});
                moved = true;
            }
            if (!moved)
                return;
        }
    }

    void Run(int frames = 1)
    {
        for (int i = 0; i < frames; ++i)
        {
            _now += 70000;
            _esp->OnFrame();
            _net->Pump();
            Serve();
            _net->Pump();
            Drain();
        }
    }

    /// Frames until a frame `cmd` (with `first` as its first byte when given) came; it is returned
    const Frame* RunUntil(uint8_t cmd, int first = -1, int frames = 3000)
    {
        for (int i = 0; i < frames; ++i)
        {
            for (size_t k = _seen; k < _frames.size(); ++k)
            {
                if (_frames[k].cmd == cmd && (first < 0 || (!_frames[k].data.empty() && _frames[k].data[0] == first)))
                {
                    _seen = k + 1;
                    return &_frames[k];
                }
            }
            _seen = _frames.size();
            Run();
        }
        // The diagnosis: the states and errors the updater sent
        std::string trail;
        for (const Frame& f : _frames)
        {
            if (f.cmd == ZiFiWcUpdater::kEventState)
                trail += "\n  67 " + std::to_string(f.data[0]) + " " + f.Text(6);
            else if (f.cmd == 0xEE)
                trail += "\n  EE " + f.Text(0);
            else if (f.cmd != ZiFiWcUpdater::kEventEntry)
                trail += "\n  " + std::to_string(f.cmd);
        }
        ADD_FAILURE() << "no frame " << static_cast<int>(cmd) << "; the trail:" << trail;
        return nullptr;
    }

    static Bytes StartPayload(const std::vector<std::string>& protectedPaths = {"wc.ini"})
    {
        Bytes p;
        for (const std::string& field : {std::string("zx/wc"), std::string("main"), std::string("wc")})
        {
            p.insert(p.end(), field.begin(), field.end());
            p.push_back(0);
        }
        for (const std::string& field : protectedPaths)
        {
            p.insert(p.end(), field.begin(), field.end());
            p.push_back(0);
        }
        p.push_back(0);
        return p;
    }

    /// The entries (68) seen since the last call: path -> status
    std::map<std::string, uint8_t> Entries()
    {
        std::map<std::string, uint8_t> out;
        for (; _entriesSeen < _frames.size(); ++_entriesSeen)
        {
            const Frame& f = _frames[_entriesSeen];
            if (f.cmd == ZiFiWcUpdater::kEventEntry)
                out[f.Text(9)] = f.data[1];
        }
        return out;
    }

    /// The usual card and repository: one file the same, one different, one new, one only on the SD, wc.ini kept
    void Typical()
    {
        Remote("WC.$C", "the commander");
        Remote("PLUGINS/A.WMF", "plugin A version 2");
        Remote("PLUGINS/B.WMF", "plugin B");
        Remote("wc.ini", "defaults");
        _plugin.Put("/WC.$C", Bytes{'t', 'h', 'e', ' ', 'c', 'o', 'm', 'm', 'a', 'n', 'd', 'e', 'r'});
        _plugin.Mkdir("/PLUGINS");
        _plugin.Put("/PLUGINS/A.WMF", Bytes{'o', 'l', 'd', ' ', 'A'});
        _plugin.Put("/PLUGINS/X.WMF", Bytes{'m', 'i', 'n', 'e'});
        _plugin.Put("/wc.ini", Bytes{'u', 's', 'e', 'r'});
    }

    std::string Sd(const std::string& path) const
    {
        const Bytes* b = _plugin.Get(path);
        return b ? std::string(b->begin(), b->end()) : std::string("<none>");
    }

    /// WCU_START and the check to its Ready state; the index of each path as the list numbers them
    std::map<std::string, uint8_t> StartAndCheck(const Frame** ready = nullptr)
    {
        Send(ZiFiNativeModule::kWcuStart, StartPayload());
        const Frame* answer = RunUntil(0xA5);
        EXPECT_NE(answer, nullptr);
        if (answer)
            EXPECT_EQ(answer->data, Bytes({1}));
        const Frame* r = RunUntil(ZiFiWcUpdater::kEventState, static_cast<int>(ZiFiWcUpdater::Phase::Ready));
        EXPECT_NE(r, nullptr) << "no Ready state";
        if (ready)
            *ready = r;
        std::map<std::string, uint8_t> index;
        for (const Frame& f : _frames)
        {
            if (f.cmd == ZiFiWcUpdater::kEventEntry)
                index[f.Text(9)] = f.data[0];
        }
        return index;
    }

    uint64_t _now = 1000;
    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<ZiFiNativeModule> _esp;
    FakeZiFiPlugin _plugin;
    std::map<std::string, Bytes> _remote;
    std::map<uint16_t, std::string> _sni;
    std::vector<std::string> _requests;
    int _apiStatus = 200;
    int _corruptDownloads = 0;
    bool _holdRaw = false;
    size_t _served = 0;
    Bytes _pending;
    std::vector<Frame> _frames;
    std::vector<Frame> _asleep;
    bool _z80Asleep = false;
    size_t _seen = 0;
    size_t _entriesSeen = 0;
};

TEST_F(ZiFiWcUpdater_Test, CheckComparesTheCardWithGitHub)
{
    Typical();
    const Frame* ready = nullptr;
    StartAndCheck(&ready);
    ASSERT_NE(ready, nullptr);
    EXPECT_EQ(ready->Text(6), "2 to update from GitHub 0123456");
    const std::map<std::string, uint8_t> e = Entries();
    using S = ZiFiWcUpdater::Status;
    EXPECT_EQ(e.at("WC.$C"), static_cast<uint8_t>(S::Same));
    EXPECT_EQ(e.at("PLUGINS/A.WMF"), static_cast<uint8_t>(S::Different));
    EXPECT_EQ(e.at("PLUGINS/B.WMF"), static_cast<uint8_t>(S::New));
    EXPECT_EQ(e.at("PLUGINS/X.WMF"), static_cast<uint8_t>(S::LocalOnly));
    EXPECT_EQ(e.at("wc.ini"), static_cast<uint8_t>(S::KeptDifferent));
    ASSERT_GE(_requests.size(), 2u);
    EXPECT_EQ(_requests[0], "api.github.com/repos/zx/wc/git/ref/heads/main");
    EXPECT_EQ(_requests[1], std::string("api.github.com/repos/zx/wc/git/trees/") + kCommit + ":wc?recursive=1");
    EXPECT_EQ(_esp->WcUpdater().Commit(), kCommit);
    // The first state event follows WCU_START's answer (the plugin's waitFor would drop it)
    size_t answer = 0, first = 0;
    for (size_t i = 0; i < _frames.size(); ++i)
    {
        if (_frames[i].cmd == 0xA5 && !answer)
            answer = i + 1;
        if (_frames[i].cmd == ZiFiWcUpdater::kEventState && !first)
            first = i + 1;
    }
    EXPECT_LT(answer, first);
}

TEST_F(ZiFiWcUpdater_Test, ApplyReplacesTheMarkedFilesThroughAVerifiedCopy)
{
    Typical();
    std::map<std::string, uint8_t> index = StartAndCheck();
    Entries();
    Send(ZiFiNativeModule::kWcuApply, Bytes({index.at("PLUGINS/A.WMF"), index.at("PLUGINS/B.WMF"), index.at("wc.ini")}));
    const Frame* answer = RunUntil(0xA6);
    ASSERT_NE(answer, nullptr);
    EXPECT_EQ(answer->data, Bytes({1}));
    const Frame* ready = RunUntil(ZiFiWcUpdater::kEventState, static_cast<int>(ZiFiWcUpdater::Phase::Ready));
    ASSERT_NE(ready, nullptr);
    EXPECT_EQ(ready->Text(6), "All files match GitHub 0123456");
    EXPECT_EQ(Sd("/PLUGINS/A.WMF"), "plugin A version 2");
    EXPECT_EQ(Sd("/PLUGINS/B.WMF"), "plugin B");
    EXPECT_EQ(Sd("/wc.ini"), "user") << "a protected file on the card stays the user's";
    EXPECT_EQ(Sd("/PLUGINS/X.WMF"), "mine");
    EXPECT_EQ(Sd("/PLUGINS/WCUPD.TMP"), "<none>");
    const std::map<std::string, uint8_t> e = Entries();
    EXPECT_EQ(e.at("PLUGINS/A.WMF"), static_cast<uint8_t>(ZiFiWcUpdater::Status::Updated));
    EXPECT_EQ(e.at("PLUGINS/B.WMF"), static_cast<uint8_t>(ZiFiWcUpdater::Status::Updated));
    // MOVE_RENAME with REPLACE put each copy in place
    EXPECT_GE(std::count(_plugin.commands.begin(), _plugin.commands.end(), 0x5D), 2);

    Send(ZiFiNativeModule::kWcuStop);
    answer = RunUntil(0xA7);
    ASSERT_NE(answer, nullptr);
    EXPECT_EQ(answer->data, Bytes({1}));
    EXPECT_FALSE(_esp->WcUpdater().Running());
}

TEST_F(ZiFiWcUpdater_Test, WithoutFilexMoveTheRenameFallbackReplaces)
{
    Typical();
    _plugin.move = false;
    std::map<std::string, uint8_t> index = StartAndCheck();
    Send(ZiFiNativeModule::kWcuApply, Bytes({index.at("PLUGINS/A.WMF")}));
    ASSERT_NE(RunUntil(ZiFiWcUpdater::kEventState, static_cast<int>(ZiFiWcUpdater::Phase::Ready)), nullptr);
    EXPECT_EQ(Sd("/PLUGINS/A.WMF"), "plugin A version 2");
    EXPECT_EQ(Sd("/PLUGINS/WCUPD.OLD"), "<none>") << "the old file went after the copy took its name";
    EXPECT_GE(std::count(_plugin.commands.begin(), _plugin.commands.end(), 0x59), 2);
}

TEST_F(ZiFiWcUpdater_Test, ABadDownloadIsFetchedAgainAndAnApiLimitIsReported)
{
    Typical();
    std::map<std::string, uint8_t> index = StartAndCheck();
    _corruptDownloads = 2;   // the third attempt has the right SHA
    Send(ZiFiNativeModule::kWcuApply, Bytes({index.at("PLUGINS/B.WMF")}));
    ASSERT_NE(RunUntil(ZiFiWcUpdater::kEventState, static_cast<int>(ZiFiWcUpdater::Phase::Ready)), nullptr);
    EXPECT_EQ(Sd("/PLUGINS/B.WMF"), "plugin B");
    Send(ZiFiNativeModule::kWcuStop);
    ASSERT_NE(RunUntil(0xA7), nullptr);

    _apiStatus = 403;
    Send(ZiFiNativeModule::kWcuStart, StartPayload());
    ASSERT_NE(RunUntil(0xA5), nullptr);
    const Frame* error = RunUntil(ZiFiWcUpdater::kEventState, static_cast<int>(ZiFiWcUpdater::Phase::Error));
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->Text(6), "GitHub API limit, retry later");
}

TEST_F(ZiFiWcUpdater_Test, CommandsTheFirmwareRefuses)
{
    // No session: APPLY and SYNC answer 0, STOP 1; a bad repository is WCU_START's error text
    Send(ZiFiNativeModule::kWcuApply, Bytes({0}));
    ASSERT_NE(RunUntil(0xA6), nullptr);
    EXPECT_EQ(_frames[_seen - 1].data, Bytes({0}));
    Send(ZiFiNativeModule::kWcuSync);
    ASSERT_NE(RunUntil(0xA8), nullptr);
    EXPECT_EQ(_frames[_seen - 1].data, Bytes({0}));
    Send(ZiFiNativeModule::kWcuStop);
    ASSERT_NE(RunUntil(0xA7), nullptr);
    EXPECT_EQ(_frames[_seen - 1].data, Bytes({1}));
    Send(ZiFiNativeModule::kWcuStart, Bytes({'n', 'o', 's', 'l', 'a', 's', 'h', 0, 'm', 0, 'w', 0, 0}));
    const Frame* error = RunUntil(0xEE);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->Text(0), "wcu:bad repository");
    ASSERT_NE(RunUntil(0xA5), nullptr);
    EXPECT_EQ(_frames[_seen - 1].data, Bytes({0}));
}

TEST_F(ZiFiWcUpdater_Test, SyncResendsTheListAndFtpStartStopsTheSession)
{
    Typical();
    std::map<std::string, uint8_t> index = StartAndCheck();
    Entries();
    Send(ZiFiNativeModule::kWcuSync);
    ASSERT_NE(RunUntil(0xA8), nullptr);
    EXPECT_EQ(_frames[_seen - 1].data, Bytes({1}));
    ASSERT_NE(RunUntil(ZiFiWcUpdater::kEventState, static_cast<int>(ZiFiWcUpdater::Phase::Ready)), nullptr);
    EXPECT_EQ(Entries().size(), index.size()) << "the whole list again";

    // FTP_START: the updater uses the VFS bridge, processFtpStart stops it first
    Send(ZiFiNativeModule::kFtpStart);
    const Frame* ftp = RunUntil(0x86);
    ASSERT_NE(ftp, nullptr);
    EXPECT_EQ(ftp->data[0], 1);
    EXPECT_FALSE(_esp->WcUpdater().Running());
}

TEST_F(ZiFiWcUpdater_Test, StopEndsADownloadAtOnce)
{
    // requestStop: the HTTPS receive ends at once (fetcher_->setCancel), the session finishes, STOP answers 1;
    // the card is untouched
    Typical();
    std::map<std::string, uint8_t> index = StartAndCheck();
    _holdRaw = true;
    Send(ZiFiNativeModule::kWcuApply, Bytes({index.at("PLUGINS/A.WMF")}));
    for (int i = 0; i < 100 && _requests.back().rfind("raw.", 0) != 0; ++i)
        Run();
    ASSERT_EQ(_requests.back().rfind("raw.githubusercontent.com/zx/wc/", 0), 0u);
    const size_t closes = _host->Count("close");
    Send(ZiFiNativeModule::kWcuStop);
    const Frame* answer = RunUntil(0xA7, -1, 20);
    ASSERT_NE(answer, nullptr);
    EXPECT_EQ(answer->data, Bytes({1}));
    EXPECT_FALSE(_esp->WcUpdater().Running());
    EXPECT_GT(_host->Count("close"), closes) << "the download's socket closed";
    EXPECT_EQ(Sd("/PLUGINS/A.WMF"), "old A");
    EXPECT_FALSE(_esp->Busy());
}

TEST_F(ZiFiWcUpdater_Test, TtdStateRoundTripsInTheMiddleOfAnApply)
{
    Typical();
    std::map<std::string, uint8_t> index = StartAndCheck();
    _z80Asleep = true;
    Send(ZiFiNativeModule::kWcuApply, Bytes({index.at("PLUGINS/A.WMF"), index.at("PLUGINS/B.WMF")}));
    Run(10);
    std::vector<uint8_t> blob;
    _esp->SaveBridge(blob);
    ASSERT_TRUE(_esp->LoadBridge(blob.data(), blob.size(), nullptr));
    std::vector<uint8_t> again;
    _esp->SaveBridge(again);
    EXPECT_EQ(blob, again) << "save, load, save gives the same bytes";
    EXPECT_TRUE(_esp->WcUpdater().Running());
    EXPECT_GT(_esp->WcUpdater().LogLength(), 0u);

    // The loaded session goes on where it was
    _z80Asleep = false;
    for (const Frame& f : _asleep)
    {
        for (const FakeZiFiPlugin::Frame& r : _plugin.Handle(f.cmd, f.data))
            Send(r.cmd, r.data);
    }
    _asleep.clear();
    ASSERT_NE(RunUntil(ZiFiWcUpdater::kEventState, static_cast<int>(ZiFiWcUpdater::Phase::Ready)), nullptr);
    EXPECT_EQ(Sd("/PLUGINS/A.WMF"), "plugin A version 2");
    EXPECT_EQ(Sd("/PLUGINS/B.WMF"), "plugin B");
}

TEST_F(ZiFiWcUpdater_Test, AFileOfSeveralWindowsIsWrittenAndVerified)
{
    // 16 KiB VFS windows: a 104 339-byte file (WC_History.txt of Wild Commander Improved) takes seven of them to
    // write and seven to read back for the SHA
    std::string big(104339, 0);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = static_cast<char>('A' + (i * 7 + i / 13) % 26);
    Remote("WC_History.txt", big);
    Remote("WC.$C", "the commander");
    _plugin.Put("/WC.$C", Bytes{'t', 'h', 'e', ' ', 'c', 'o', 'm', 'm', 'a', 'n', 'd', 'e', 'r'});
    std::map<std::string, uint8_t> index = StartAndCheck();
    Entries();
    Send(ZiFiNativeModule::kWcuApply, Bytes({index.at("WC_History.txt")}));
    const Frame* ready = RunUntil(ZiFiWcUpdater::kEventState, static_cast<int>(ZiFiWcUpdater::Phase::Ready));
    ASSERT_NE(ready, nullptr);
    EXPECT_EQ(ready->Text(6), "All files match GitHub 0123456");
    EXPECT_EQ(Sd("/WC_History.txt"), big);
    EXPECT_EQ(Entries().at("WC_History.txt"), static_cast<uint8_t>(ZiFiWcUpdater::Status::Updated));
}
