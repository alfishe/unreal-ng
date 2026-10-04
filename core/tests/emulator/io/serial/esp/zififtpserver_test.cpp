// The ZIFI-NATIVE FTP server (zififtpserver.h) through the whole module: a host FTP client on the virtual network
// (FakeHostNet), the module's UART, the VFS frames answered by an in-memory Wild Commander plugin
// (fakezifiplugin.h). Login, listings, RETR / STOR byte-exact in passive and active mode, errors, the S3 sessions
// and its queued file commands, the ESP-01S single session, the UART rules while the VFS waits, the deferred
// network command, the events and the TTD state. Sources: ZiFi-ESP32-S3-Zero 2e5ba83 src/ftp_server.cpp,
// src/main.cpp; ZiFi-ESP-01S-Native-C-Project 90834e4 src/ftp_server.cpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fakehostnet.h"
#include "_helpers/fakezifiplugin.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/esp/zififtpserver.h"
#include "emulator/io/serial/esp/zifinativemodule.h"

class ZiFiFtpServer_Test : public ::testing::Test
{
protected:
    using Variant = ZiFiNativeModule::Variant;
    using Bytes = std::vector<uint8_t>;
    static constexpr uint16_t kPort = 2121;

    void SetUp() override { Make(Variant::S3); }

    void Make(Variant variant)
    {
        _esp.reset();
        _net.reset();
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), VirtualNetworkConfig());
        _esp = std::make_unique<ZiFiNativeModule>(_net.get(), variant);
        _esp->SetClock([this]() { return _now; }, 3500000);
        _esp->OnLineSettings(SerialLine());
        _plugin = FakeZiFiPlugin();
        if (variant == Variant::Esp01s)
        {
            _plugin.windows = false;
            _plugin.filex = false;
            _plugin.dates = false;
        }
        _frames.clear();
        _pending.clear();
        _nextId = 0x8000;
        Send(ZiFiNativeModule::kWifiConnect, Bytes({'U', 'n', 'r', 'e', 'a', 'l', 'N', 'G', 0}));
        Run(100);
        _frames.clear();
    }

    void Send(uint8_t cmd, const Bytes& data = {})
    {
        for (uint8_t b : ZiFiNativeModule::Frame(cmd, data))
            _esp->Transmit(b);
    }

    /// Drain the module's output: VFS requests go to the plugin (its answers back over the UART), the rest is kept
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
                if (ZiFiVfsBridge::IsVfsCommand(cmd) && !_z80Asleep)
                {
                    for (const FakeZiFiPlugin::Frame& f : _plugin.Handle(cmd, data))
                        Send(f.cmd, f.data);
                }
                else
                    _frames.push_back({cmd, data});
                moved = true;
            }
            if (!moved)
                return;
        }
    }

    /// `frames` emulated frames (20 ms each): module, network, UART
    void Run(int frames = 1)
    {
        for (int i = 0; i < frames; ++i)
        {
            _now += 70000;
            _esp->OnFrame();
            _net->Pump();
            Drain();
        }
    }

    void StartFtp(const std::string& user = "zx", const std::string& password = "zx")
    {
        Bytes p = {static_cast<uint8_t>(kPort), static_cast<uint8_t>(kPort >> 8)};
        p.insert(p.end(), user.begin(), user.end());
        p.push_back(0);
        p.insert(p.end(), password.begin(), password.end());
        p.push_back(0);
        Send(ZiFiNativeModule::kFtpStart, p);
        Run(2);
    }

    const FakeHostNet::Command* Listener(uint16_t hostPort) const
    {
        for (auto it = _host->commands.rbegin(); it != _host->commands.rend(); ++it)
        {
            if (it->op == "listen" && it->endpoint.port == hostPort)
                return &*it;
        }
        return nullptr;
    }

    /// A client connects to the guest port's host listener; its host socket id
    uint16_t Connect(uint16_t hostPort)
    {
        const FakeHostNet::Command* l = Listener(hostPort);
        EXPECT_NE(l, nullptr) << "no listener on " << hostPort;
        if (!l)
            return 0;
        const uint16_t id = _nextId++;
        _host->Push(NetEventType::Accepted, l->socket, NetEventStatus::Ok, {NetIp(127, 0, 0, 1), static_cast<uint16_t>(50000 + id)},
                    {static_cast<uint8_t>(id), static_cast<uint8_t>(id >> 8)});
        Run(3);
        return id;
    }

    void Data(uint16_t id, const std::string& text) { Data(id, Bytes(text.begin(), text.end())); }
    void Data(uint16_t id, const Bytes& bytes)
    {
        _host->Push(NetEventType::Data, id, NetEventStatus::Ok, {}, bytes);
    }
    void Close(uint16_t id) { _host->Push(NetEventType::PeerClosed, id); }

    /// Everything the module sent on a host socket since the last call
    std::string Text(uint16_t id)
    {
        const Bytes b = Received(id);
        return std::string(b.begin(), b.end());
    }
    Bytes Received(uint16_t id)
    {
        Bytes out;
        size_t& seen = _seen[id];
        size_t n = 0;
        for (const FakeHostNet::Command& c : _host->commands)
        {
            if (c.op != "send" || c.socket != id)
                continue;
            if (n++ < seen)
                continue;
            out.insert(out.end(), c.data.begin(), c.data.end());
        }
        seen = n;
        return out;
    }
    bool Closed(uint16_t id) const
    {
        for (const FakeHostNet::Command& c : _host->commands)
        {
            if ((c.op == "close" || c.op == "shutdown") && c.socket == id)
                return true;
        }
        return false;
    }

    /// One command, its reply (the frames it takes to run)
    std::string Cmd(uint16_t id, const std::string& line, int frames = 20)
    {
        Data(id, line + "\r\n");
        Run(frames);
        return Text(id);
    }

    uint16_t Login(const std::string& user = "zx", const std::string& password = "zx")
    {
        const uint16_t id = Connect(kPort);
        Text(id);
        Cmd(id, "USER " + user, 3);
        Cmd(id, "PASS " + password, 3);
        return id;
    }

    /// PASV, then the data client
    uint16_t Passive(uint16_t control, uint16_t passivePort = 2122)
    {
        const std::string r = Cmd(control, "EPSV", 3);
        EXPECT_EQ(r, "229 Entering Extended Passive Mode (|||" + std::to_string(passivePort) + "|)\r\n");
        return Connect(passivePort);
    }

    std::string Events(uint8_t cmd) const
    {
        std::string s;
        for (const FakeZiFiPlugin::Frame& f : _frames)
        {
            if (f.cmd == cmd)
                s += (s.empty() ? "" : "|") + std::string(f.data.begin(), f.data.end());
        }
        return s;
    }
    std::vector<uint8_t> ClientStates() const
    {
        std::vector<uint8_t> s;
        for (const FakeZiFiPlugin::Frame& f : _frames)
        {
            if (f.cmd == 0x60 && !f.data.empty())
                s.push_back(f.data[0]);
        }
        return s;
    }

    static Bytes Pattern(size_t n, uint32_t seed = 1)
    {
        Bytes b(n);
        for (size_t i = 0; i < n; ++i)
        {
            seed = seed * 1103515245u + 12345u;
            b[i] = static_cast<uint8_t>(seed >> 16);
        }
        return b;
    }

    uint64_t _now = 1000;
    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<ZiFiNativeModule> _esp;
    FakeZiFiPlugin _plugin;
    std::vector<FakeZiFiPlugin::Frame> _frames;
    Bytes _pending;
    uint16_t _nextId = 0x8000;
    std::map<uint16_t, size_t> _seen;
    bool _z80Asleep = false;
};

TEST_F(ZiFiFtpServer_Test, StartLoginAndTheEvents)
{
    StartFtp();
    ASSERT_GE(_frames.size(), 2u);
    EXPECT_EQ(_frames[0].cmd, 0xFE);
    EXPECT_EQ(_frames[1].cmd, 0x86);
    EXPECT_EQ(_frames[1].data, Bytes({1, kPort & 0xFF, kPort >> 8}));
    const uint16_t c = Connect(kPort);
    EXPECT_EQ(Text(c).substr(0, 37), "220 ZiFi ESP32-S3 FTP ready s3-native");
    EXPECT_EQ(Cmd(c, "SYST", 3), "530 Please login first\r\n");
    EXPECT_EQ(Cmd(c, "USER zx", 3), "331 Please specify the password\r\n");
    EXPECT_EQ(Cmd(c, "PASS wrong", 3), "530 Login incorrect\r\n");
    Cmd(c, "USER zx", 3);
    EXPECT_EQ(Cmd(c, "PASS zx", 3), "230 Login successful\r\n");
    EXPECT_EQ(Cmd(c, "PWD", 3), "257 \"/\"\r\n");
    EXPECT_EQ(Cmd(c, "FEAT", 3), "211-Features:\r\n EPSV\r\n UTF8\r\n SIZE\r\n MDTM\r\n MFMT\r\n MLST type*;size*;modify*;\r\n211 End\r\n");
    EXPECT_EQ(Cmd(c, "XYZ", 3), "502 Command not implemented\r\n");
    // 60: connected, then logged in; 61: the commands, USER / PASS without their argument
    const std::vector<uint8_t> states = ClientStates();
    ASSERT_GE(states.size(), 2u);
    EXPECT_EQ(states.front(), 1);
    EXPECT_EQ(states.back(), 2);
    EXPECT_EQ(Events(0x61), "SYST|USER|PASS|USER|PASS|PWD|FEAT|XYZ");
    // 66: the Wi-Fi bar every 2 s while FTP runs
    Run(110);
    EXPECT_NE(Events(0x66).find("Wi-Fi [################] 100%"), std::string::npos);
    EXPECT_EQ(Cmd(c, "QUIT", 3), "221 Goodbye\r\n");
    EXPECT_TRUE(Closed(c));
    EXPECT_EQ(ClientStates().back(), 0);
}

TEST_F(ZiFiFtpServer_Test, ListingsInPassiveMode)
{
    _plugin.Mkdir("/GAMES");
    _plugin.Put("/README.TXT", Pattern(123));
    StartFtp();
    const uint16_t c = Login();
    uint16_t d = Passive(c);
    EXPECT_EQ(Cmd(c, "LIST -la"), "150 Here comes the directory listing\r\n226 Directory send OK\r\n");
    // The ESP's clock is not set (no SNTP answer): the year form
    EXPECT_EQ(Text(d), "drwxr-xr-x 1 zx zx 0 Oct  4  2024 GAMES\r\n-rwxr-xr-x 1 zx zx 123 Oct  4  2024 README.TXT\r\n");
    EXPECT_TRUE(Closed(d));
    d = Passive(c);
    Cmd(c, "MLSD");
    EXPECT_EQ(Text(d), "type=dir;modify=20241004120000; GAMES\r\ntype=file;size=123;modify=20241004120000; README.TXT\r\n");
    d = Passive(c);
    Cmd(c, "NLST");
    EXPECT_EQ(Text(d), "GAMES\r\nREADME.TXT\r\n");
    EXPECT_EQ(Cmd(c, "CWD GAMES"), "250 Directory changed\r\n");
    EXPECT_EQ(Cmd(c, "PWD", 3), "257 \"/GAMES\"\r\n");
    EXPECT_EQ(Cmd(c, "CDUP"), "250 Directory changed\r\n");
    EXPECT_EQ(Cmd(c, "CWD NOPE"), "550 Failed to change directory\r\n");
    EXPECT_EQ(Cmd(c, "SIZE README.TXT"), "213 123\r\n");
    EXPECT_EQ(Cmd(c, "MDTM /README.TXT"), "213 20241004120000\r\n");
    EXPECT_EQ(Cmd(c, "MLST README.TXT"),
              "250-Listing /README.TXT\r\n type=file;size=123;modify=20241004120000; /README.TXT\r\n250 End\r\n");
    // PASV reports the module's address on the virtual network
    const std::string pasv = Cmd(c, "PASV", 3);
    EXPECT_EQ(pasv.substr(0, 26), "227 Entering Passive Mode ");
    EXPECT_NE(pasv.find(",8,74)"), std::string::npos) << "port 2122 = 8 * 256 + 74";
}

TEST_F(ZiFiFtpServer_Test, RetrAndStorByteExact)
{
    const Bytes file = Pattern(40000, 9);
    _plugin.Put("/DATA.BIN", file);
    StartFtp();
    const uint16_t c = Login();
    uint16_t d = Passive(c);
    EXPECT_EQ(Cmd(c, "RETR DATA.BIN", 60), "150 Opening data connection\r\n226 Transfer complete\r\n");
    EXPECT_EQ(Received(d), file) << "downloaded byte-exact";
    EXPECT_TRUE(Closed(d));

    const Bytes upload = Pattern(50000, 21);
    d = Passive(c);
    Data(d, Bytes(upload.begin(), upload.begin() + 30000));
    Data(c, "STOR UP.BIN\r\n");
    Run(5);
    Data(d, Bytes(upload.begin() + 30000, upload.end()));
    Close(d);
    Run(60);
    EXPECT_EQ(Text(c), "150 Opening data connection\r\n226 Transfer complete\r\n");
    ASSERT_NE(_plugin.Get("/UP.BIN"), nullptr);
    EXPECT_EQ(*_plugin.Get("/UP.BIN"), upload) << "uploaded byte-exact";
    // And back
    d = Passive(c);
    Cmd(c, "RETR UP.BIN", 60);
    EXPECT_EQ(Received(d), upload);
    EXPECT_EQ(_esp->Ftp().FilesSent(), 2u);
    EXPECT_EQ(_esp->Ftp().FilesReceived(), 1u);
    // FTP_RAM_STATS after a STOR: two points, [0, heap] and [received, heap]
    Send(0x0A);
    Run(2);
    EXPECT_EQ(_frames.back().cmd, 0x8A);
    EXPECT_EQ(_frames.back().data.size(), 17u);
    EXPECT_EQ(_frames.back().data[0], 2);
}

TEST_F(ZiFiFtpServer_Test, ActiveModeConnectsToTheClient)
{
    _plugin.Put("/A.BIN", Pattern(3000));
    StartFtp();
    const uint16_t c = Login();
    EXPECT_EQ(Cmd(c, "PORT 127,0,0,1,195,80", 3), "200 PORT command successful\r\n");
    Data(c, "RETR A.BIN\r\n");
    Run(5);
    ASSERT_NE(_host->Last("connect"), nullptr);
    const FakeHostNet::Command connect = *_host->Last("connect");
    EXPECT_EQ(connect.endpoint.addr, NetIp(127, 0, 0, 1));
    EXPECT_EQ(connect.endpoint.port, 195 * 256 + 80);
    _host->Push(NetEventType::Connected, connect.socket);
    Run(20);
    EXPECT_EQ(Text(c), "150 Opening data connection\r\n226 Transfer complete\r\n");
    EXPECT_EQ(Received(connect.socket), Pattern(3000));
    // EPRT and a refused connection
    EXPECT_EQ(Cmd(c, "EPRT |1|127.0.0.1|50123|", 3), "200 PORT command successful\r\n");
    EXPECT_EQ(Cmd(c, "EPRT |2|::1|50123|", 3), "501 Bad PORT argument\r\n");
    Cmd(c, "PORT 127,0,0,1,195,81", 3);
    Data(c, "RETR A.BIN\r\n");
    Run(5);
    _host->Push(NetEventType::ConnectFailed, _host->Last("connect")->socket, NetEventStatus::Refused);
    Run(10);
    EXPECT_EQ(Text(c), "150 Opening data connection\r\n425 Cannot open data connection\r\n");
}

TEST_F(ZiFiFtpServer_Test, ErrorsMissingFilesTimeoutsAndDrops)
{
    _plugin.Put("/A.BIN", Pattern(10));
    StartFtp();
    const uint16_t c = Login();
    EXPECT_EQ(Cmd(c, "RETR NOPE.BIN"), "550 File not found\r\n");
    EXPECT_EQ(Cmd(c, "SIZE NOPE"), "550 Could not get file size\r\n");
    EXPECT_EQ(Cmd(c, "RETR A.BIN"), "150 Opening data connection\r\n425 Use PASV/EPSV or PORT/EPRT first\r\n");
    // Nobody connects to the passive port: 10 s
    Cmd(c, "EPSV", 3);
    EXPECT_EQ(Cmd(c, "LIST", 600), "150 Here comes the directory listing\r\n425 Passive data connection timed out\r\n");
    // DELE, MKD
    EXPECT_EQ(Cmd(c, "MKD NEW"), "257 \"/NEW\" created\r\n");
    EXPECT_EQ(Cmd(c, "MKD NEW"), "550 Cannot create directory\r\n");
    EXPECT_EQ(Cmd(c, "DELE A.BIN"), "250 File deleted\r\n");
    EXPECT_EQ(Cmd(c, "DELE A.BIN"), "550 Delete failed\r\n");
    EXPECT_EQ(_plugin.Get("/A.BIN"), nullptr);
    // The data connection closes before any byte: no empty file is left, the session drops
    const uint16_t d = Passive(c);
    Close(d);
    EXPECT_EQ(Cmd(c, "STOR EMPTY.BIN", 30), "150 Opening data connection\r\n426 Transfer aborted (data-empty bytes=0)\r\n");
    EXPECT_EQ(_plugin.files.count("/EMPTY.BIN"), 0u) << "the created entry is deleted";
    EXPECT_TRUE(Closed(c));
}

TEST_F(ZiFiFtpServer_Test, TheZ80FailsAWrite)
{
    StartFtp();
    const uint16_t c = Login();
    const uint16_t d = Passive(c);
    _plugin.failCmd = 0x57;
    _plugin.failStatus = 0x22;
    Data(d, Pattern(20000));
    Close(d);
    EXPECT_EQ(Cmd(c, "STOR FULL.BIN", 40),
              "150 Opening data connection\r\n426 Transfer aborted (block-status-34 bytes=20000)\r\n");
    EXPECT_EQ(_plugin.files.count("/FULL.BIN"), 0u);
}

TEST_F(ZiFiFtpServer_Test, ThreeSessionsQueuedCommandsAndTheFourthRefused)
{
    _plugin.Put("/BIG.BIN", Pattern(30000));
    StartFtp();
    const uint16_t a = Login();
    const uint16_t b = Login();
    const uint16_t c = Login();
    const uint16_t d = Connect(kPort);
    EXPECT_EQ(Text(d), "421 Too many FTP sessions (maximum 3)\r\n");
    EXPECT_TRUE(Closed(d));
    // Session b's passive port is 2123; its LIST waits while a's RETR holds the VFS path
    const uint16_t da = Passive(a, 2122);
    const uint16_t db = Passive(b, 2123);
    _z80Asleep = true;   // the Z80 holds its answers: a's command stays in progress
    Data(a, "RETR BIG.BIN\r\n");
    Run(3);
    EXPECT_EQ(Cmd(b, "LIST", 3), "") << "queued behind a's command";
    EXPECT_EQ(Cmd(c, "NOOP", 3), "200 OK\r\n") << "a command without the VFS runs at once";
    // While b has a queued command its control socket is not read (receiveControl): SIZE waits there
    EXPECT_EQ(Cmd(b, "SIZE BIG.BIN", 3), "");
    _z80Asleep = false;
    // The VFS request that went out while asleep was lost: the client times out after 5 s, RETR fails
    Run(400);
    EXPECT_NE(Text(a).find("550 File not found"), std::string::npos);
    EXPECT_EQ(Text(b), "150 Here comes the directory listing\r\n226 Directory send OK\r\n213 30000\r\n")
        << "the queued LIST ran after it, then the SIZE that waited in the socket";
    EXPECT_EQ(Text(db), "-rwxr-xr-x 1 zx zx 30000 Oct  4  2024 BIG.BIN\r\n");
    (void)da;
}

TEST_F(ZiFiFtpServer_Test, TheUartWhileTheVfsWaitsAndADeferredNetworkCommand)
{
    _plugin.Put("/A.BIN", Pattern(100));
    StartFtp();
    const uint16_t c = Login();
    _z80Asleep = true;
    Data(c, "SIZE A.BIN\r\n");
    Run(2);
    // The client waits in waitFor: PING is answered, ECHO is lost
    Send(ZiFiNativeModule::kPing);
    Send(ZiFiNativeModule::kEcho, Bytes({'x'}));
    Run(1);
    EXPECT_EQ(_frames.back().cmd, 0xF0);
    EXPECT_EQ(_esp->DroppedWhileWaiting(), 1u);
    // A network command that comes while the client waits is lost too (waitFor drops it)
    _frames.clear();
    Send(ZiFiNativeModule::kNetIpConfig);
    Run(2);
    EXPECT_TRUE(_frames.empty());
    _z80Asleep = false;
    Run(600);
    EXPECT_EQ(Text(c), "550 Could not get file size\r\n") << "the lost STAT: 5 s client timeout";
    // While the FTP command waits for its data connection (not for the Z80) a network command gets its ACK and
    // runs after the command: the network core is inside the FTP server
    Cmd(c, "EPSV", 3);
    Data(c, "RETR A.BIN\r\n");
    Run(5);
    _frames.clear();
    Send(ZiFiNativeModule::kNetIpConfig);
    Run(2);
    ASSERT_FALSE(_frames.empty());
    EXPECT_EQ(_frames[0].cmd, 0xFE);
    const auto answered = [this]() {
        for (const auto& f : _frames)
        {
            if (f.cmd == 0xA0)
                return true;
        }
        return false;
    };
    EXPECT_FALSE(answered());
    Run(600);
    EXPECT_TRUE(answered()) << "the deferred NET_IP_CONFIG ran after the FTP command";
    EXPECT_EQ(Text(c), "150 Opening data connection\r\n425 Passive data connection timed out\r\n");
}

TEST_F(ZiFiFtpServer_Test, NetOpenAndFtpStopEndTheServer)
{
    StartFtp();
    const uint16_t c = Login();
    Send(ZiFiNativeModule::kFtpStop);
    Run(2);
    EXPECT_TRUE(Closed(c));
    EXPECT_FALSE(_esp->Ftp().Running());
    StartFtp();
    EXPECT_TRUE(_esp->Ftp().Running());
    Bytes open = {'1', '.', '2', '.', '3', '.', '4', 0, 80, 0};
    Send(ZiFiNativeModule::kNetOpen, open);
    Run(2);
    EXPECT_FALSE(_esp->Ftp().Running()) << "NetClient::open stops the FTP server";
}

TEST_F(ZiFiFtpServer_Test, MfmtSetsTheWriteTime)
{
    _plugin.Put("/T.TXT", Pattern(5));
    StartFtp();
    const uint16_t c = Login();
    EXPECT_EQ(Cmd(c, "MFMT 20260102030405 T.TXT"), "213 Modify=20260102030404; T.TXT\r\n") << "FAT: two-second steps";
    EXPECT_EQ(_plugin.files["/T.TXT"].date, ((2026 - 1980) << 9) | (1 << 5) | 2);
    EXPECT_EQ(Cmd(c, "MFMT 2026 T.TXT"), "501 Usage: MFMT YYYYMMDDhhmmss path\r\n");
    _plugin.filex = false;
    EXPECT_EQ(Cmd(c, "MFMT 20260102030405 T.TXT"), "550 Setting dates needs newer ZIFIFTP.WMF and WC Improved\r\n");
}

TEST_F(ZiFiFtpServer_Test, Esp01sOneSessionLegacyTransfers)
{
    Make(Variant::Esp01s);
    _plugin.Put("/E.BIN", Pattern(2000, 4));
    StartFtp();
    const uint16_t c = Connect(kPort);
    EXPECT_EQ(Text(c).substr(0, 33), "220 ZiFi native FTP ready native-");
    const uint16_t x = Connect(kPort);
    EXPECT_EQ(Text(x), "421 Only one FTP session is allowed\r\n");
    Cmd(c, "USER zx", 3);
    EXPECT_EQ(Cmd(c, "PASS zx", 3), "230 Login successful\r\n");
    EXPECT_EQ(Cmd(c, "FEAT", 3), "211-Features:\r\n EPSV\r\n UTF8\r\n SIZE\r\n211 End\r\n");
    EXPECT_EQ(Cmd(c, "MLSD", 3), "502 Command not implemented\r\n");
    uint16_t d = Passive(c);
    Cmd(c, "LIST");
    EXPECT_EQ(Text(d), "-rwxr-xr-x 1 zx zx 2000 Jan 01 00:00 E.BIN\r\n");
    d = Passive(c);
    EXPECT_EQ(Cmd(c, "RETR E.BIN", 40), "150 Opening data connection\r\n226 Transfer complete\r\n");
    EXPECT_EQ(Received(d), Pattern(2000, 4));
    const Bytes up = Pattern(3333, 8);
    d = Passive(c);
    Data(d, up);
    Close(d);
    EXPECT_EQ(Cmd(c, "STOR U.BIN", 60), "150 Opening data connection\r\n226 Transfer complete\r\n");
    ASSERT_NE(_plugin.Get("/U.BIN"), nullptr);
    EXPECT_EQ(*_plugin.Get("/U.BIN"), up);
    EXPECT_EQ(Events(0x66), "") << "no signal bar on the ESP-01S";
    const std::vector<uint8_t> states = ClientStates();
    EXPECT_EQ(states.front(), 1);
}

TEST_F(ZiFiFtpServer_Test, TtdStateRoundTripsInTheMiddleOfATransfer)
{
    _plugin.Put("/BIG.BIN", Pattern(30000));
    StartFtp();
    const uint16_t c = Login();
    Passive(c);
    _z80Asleep = true;
    Data(c, "RETR BIG.BIN\r\n");
    Run(2);
    std::vector<uint8_t> blob;
    _esp->SaveBridge(blob);
    ASSERT_GT(blob.size(), 100u);
    ASSERT_TRUE(_esp->LoadBridge(blob.data(), blob.size(), nullptr));
    std::vector<uint8_t> again;
    _esp->SaveBridge(again);
    EXPECT_EQ(blob, again) << "save, load, save gives the same bytes";
    EXPECT_TRUE(_esp->Ftp().JobActive());
    EXPECT_TRUE(_esp->Vfs().Waiting());
    // An empty section: the bridge as after a restart
    ASSERT_TRUE(_esp->LoadBridge(nullptr, 0, nullptr));
    EXPECT_FALSE(_esp->Ftp().Running());
}
