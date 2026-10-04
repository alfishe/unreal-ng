// The ESP-01S WebDAV server (zifiwebdavserver.h) end to end: the ZIFI-NATIVE,ESP01S module on the virtual network,
// a host WebDAV client through the forward 8080 -> guest port 80, and the Z80 side played by FakeZiFiPlugin with the
// ESP-01S plugin's flavor (ZIFIWDAV.WMF: one-byte OPEN answers, 512-byte READ / BLOCK, plain directory entries).
// Source: ZiFi-ESP-01S-Native-C-Project 90834e4 src/webdav_server.cpp, src/main.cpp (handleFtpStart).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fakehostnet.h"
#include "_helpers/fakezifiplugin.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/esp/zifinativemodule.h"
#include "emulator/io/serial/esp/zifiwebdavserver.h"

class ZiFiWebDavServer_Test : public ::testing::Test
{
protected:
    using Bytes = std::vector<uint8_t>;

    void SetUp() override
    {
        auto host = std::make_unique<FakeHostNet>();
        _host = host.get();
        VirtualNetworkConfig config;
        config.forwards[80] = 8080;
        config.forwards[21] = 2121;
        _net = std::make_unique<VirtualNetwork>(nullptr, std::move(host), config);
        _esp = std::make_unique<ZiFiNativeModule>(_net.get(), ZiFiNativeModule::Variant::Esp01s);
        _esp->SetClock([this]() { return _now; }, 3500000);
        _esp->OnLineSettings(SerialLine());
        _plugin.windows = false;
        _plugin.filex = false;
        _plugin.dates = false;
    }

    void Send(uint8_t cmd, const Bytes& data = {})
    {
        for (uint8_t b : ZiFiNativeModule::Frame(cmd, data))
            _esp->Transmit(b);
    }

    /// Run `us` of emulated time: the module, the network, and the plugin answering every VFS request at once
    void Run(uint64_t us = 20000)
    {
        const uint64_t step = 2000;
        for (uint64_t t = 0; t < us; t += step)
        {
            _now += step * 35 / 10;
            _esp->OnFrame();
            _net->Pump();
            while (_esp->HasByte())
                _out.push_back(_esp->TakeByte());
            while (_out.size() >= 5)
            {
                const size_t length = static_cast<size_t>(_out[2] | (_out[3] << 8));
                if (_out.size() < 5 + length)
                    break;
                const uint8_t cmd = _out[1];
                const Bytes data(_out.begin() + 4, _out.begin() + 4 + static_cast<std::ptrdiff_t>(length));
                _out.erase(_out.begin(), _out.begin() + 5 + static_cast<std::ptrdiff_t>(length));
                if (cmd >= 0x40 && cmd <= 0x5E)
                {
                    for (const FakeZiFiPlugin::Frame& a : _plugin.Handle(cmd, data))
                        Send(a.cmd, a.data);
                }
                else
                    _replies.push_back(cmd);
            }
        }
    }

    void StartServers()
    {
        Send(ZiFiNativeModule::kFtpStart);
        Run();
        ASSERT_EQ(_replies, Bytes({0xFE, 0x86}));
        _replies.clear();
        ASSERT_TRUE(_esp->WebDav().Running());
        _listen = 0;
        for (const FakeHostNet::Command& c : _host->commands)
        {
            if (c.op == "listen" && c.endpoint.port == 8080)
                _listen = c.socket;
        }
        ASSERT_NE(_listen, 0) << "guest port 80 on host port 8080";
    }

    /// A host client sends `request`; the bytes the server sent back on that connection
    std::string Http(const std::string& request, uint64_t us = 300000)
    {
        const uint16_t socket = _nextSocket++;
        _host->Push(NetEventType::Accepted, _listen, NetEventStatus::Ok, {NetIp(127, 0, 0, 1), 50000},
                    {static_cast<uint8_t>(socket), static_cast<uint8_t>(socket >> 8)});
        _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, Bytes(request.begin(), request.end()));
        _net->Pump();
        Run(us);
        std::string answer;
        for (const FakeHostNet::Command& c : _host->commands)
        {
            if (c.op == "send" && c.socket == socket)
                answer.append(c.data.begin(), c.data.end());
        }
        return answer;
    }

    static std::string StatusLine(const std::string& answer) { return answer.substr(0, answer.find("\r\n")); }

    uint64_t _now = 1000;
    FakeHostNet* _host = nullptr;
    std::unique_ptr<VirtualNetwork> _net;
    std::unique_ptr<ZiFiNativeModule> _esp;
    FakeZiFiPlugin _plugin;
    Bytes _out;
    Bytes _replies;
    uint16_t _listen = 0;
    uint16_t _nextSocket = 0x8000;
};

TEST_F(ZiFiWebDavServer_Test, StartsAndStopsWithFtp)
{
    StartServers();
    EXPECT_EQ(_esp->WebDav().Port(), 80);
    Send(ZiFiNativeModule::kFtpStop);
    Run();
    EXPECT_EQ(_replies, Bytes({0xFE, 0x87}));
    EXPECT_FALSE(_esp->WebDav().Running()) << "stopStorageServices stops both";
}

TEST_F(ZiFiWebDavServer_Test, OptionsAndUnknownMethods)
{
    StartServers();
    std::string a = Http("OPTIONS * HTTP/1.1\r\nHost: zx\r\n\r\n");
    EXPECT_EQ(StatusLine(a), "HTTP/1.1 200 OK");
    EXPECT_NE(a.find("Allow: OPTIONS, PROPFIND, GET, HEAD, PUT, DELETE, MKCOL\r\n"), std::string::npos);
    EXPECT_NE(a.find("DAV: 1\r\n"), std::string::npos);
    a = Http("LOCK /x HTTP/1.1\r\nHost: zx\r\n\r\n");
    EXPECT_EQ(StatusLine(a), "HTTP/1.1 405 Method Not Allowed");
    a = Http("GET / HTTP/2.0\r\nHost: zx\r\n\r\n");
    EXPECT_EQ(StatusLine(a), "HTTP/1.1 505 HTTP Version Not Supported");
    a = Http("GET /%zz HTTP/1.1\r\nHost: zx\r\n\r\n");
    EXPECT_EQ(StatusLine(a), "HTTP/1.1 400 Bad Path");
    a = Http("PUT /a.txt HTTP/1.1\r\nHost: zx\r\n\r\n");
    EXPECT_EQ(StatusLine(a), "HTTP/1.1 411 Length Required");
}

TEST_F(ZiFiWebDavServer_Test, PropfindListsOneLevel)
{
    _plugin.Mkdir("/GAMES");
    _plugin.Put("/GAMES/ELITE.TRD", Bytes(655360, 0xE5));
    _plugin.Put("/GAMES/MY GAME.SCL", Bytes(1234, 1));
    _plugin.Mkdir("/GAMES/NEW");
    StartServers();
    const std::string body = "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:allprop/></d:propfind>";
    const std::string a = Http("PROPFIND /GAMES HTTP/1.1\r\nHost: zx\r\nDepth: 1\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\n\r\n" + body);
    EXPECT_EQ(StatusLine(a), "HTTP/1.1 207 Multi-Status") << "the WinSCP body is read first";
    EXPECT_NE(a.find("<d:href>/GAMES</d:href>\n<d:propstat><d:prop>\n<d:resourcetype><d:collection/>"), std::string::npos) << a;
    EXPECT_NE(a.find("<d:href>/GAMES/ELITE.TRD</d:href>\n<d:propstat><d:prop>\n<d:resourcetype/><d:getcontentlength>655360</d:getcontentlength>"),
              std::string::npos);
    EXPECT_NE(a.find("<d:href>/GAMES/MY%20GAME.SCL</d:href>"), std::string::npos) << "the space escaped";
    EXPECT_NE(a.find("<d:href>/GAMES/NEW</d:href>"), std::string::npos);
    EXPECT_EQ(a.substr(a.size() - 17), "</d:multistatus>\n");

    const std::string zero = Http("PROPFIND /GAMES HTTP/1.1\r\nHost: zx\r\nDepth: 0\r\n\r\n");
    EXPECT_EQ(zero.find("ELITE"), std::string::npos) << "Depth 0: the collection alone";
    EXPECT_EQ(StatusLine(Http("PROPFIND /NONE HTTP/1.1\r\nHost: zx\r\n\r\n")), "HTTP/1.1 404 Not Found");
}

TEST_F(ZiFiWebDavServer_Test, GetAndHeadSendTheFile)
{
    Bytes data(1500);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(i * 7);
    _plugin.Put("/README.TXT", data);
    StartServers();
    const std::string a = Http("GET /README.TXT HTTP/1.1\r\nHost: zx\r\n\r\n");
    EXPECT_EQ(StatusLine(a), "HTTP/1.1 200 OK");
    EXPECT_NE(a.find("Content-Length: 1500\r\n"), std::string::npos);
    EXPECT_NE(a.find("Content-Type: application/octet-stream\r\n"), std::string::npos);
    const size_t bodyAt = a.find("\r\n\r\n") + 4;
    EXPECT_EQ(Bytes(a.begin() + static_cast<std::ptrdiff_t>(bodyAt), a.end()), data) << "byte exact through 512-byte READs";

    const std::string head = Http("HEAD /README.TXT HTTP/1.1\r\nHost: zx\r\n\r\n");
    EXPECT_NE(head.find("Content-Length: 1500\r\n"), std::string::npos);
    EXPECT_EQ(head.size(), head.find("\r\n\r\n") + 4) << "HEAD: no body";
    EXPECT_EQ(StatusLine(Http("GET / HTTP/1.1\r\nHost: zx\r\n\r\n")), "HTTP/1.1 404 Not Found") << "a collection";
    EXPECT_EQ(StatusLine(Http("GET /../../README.TXT HTTP/1.1\r\nHost: zx\r\n\r\n")), "HTTP/1.1 200 OK") << "no way above the root";
}

TEST_F(ZiFiWebDavServer_Test, PutWritesTheFileInBlocks)
{
    StartServers();
    std::string body(3000, 'x');
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<char>('A' + i % 26);
    const std::string a = Http("PUT /UP.TXT HTTP/1.1\r\nHost: zx\r\nContent-Length: 3000\r\n\r\n" + body, 600000);
    EXPECT_EQ(StatusLine(a), "HTTP/1.1 201 Created");
    const Bytes* stored = _plugin.Get("/UP.TXT");
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(std::string(stored->begin(), stored->end()), body);
    EXPECT_GE(std::count(_plugin.commands.begin(), _plugin.commands.end(), uint8_t{0x56}), 12) << "512-byte BLOCK fragments";
    EXPECT_EQ(_esp->WebDav().LastStatus(), 201);
}

TEST_F(ZiFiWebDavServer_Test, AShortPutIsRemoved)
{
    StartServers();
    const uint16_t socket = _nextSocket++;
    _host->Push(NetEventType::Accepted, _listen, NetEventStatus::Ok, {NetIp(127, 0, 0, 1), 50001},
                {static_cast<uint8_t>(socket), static_cast<uint8_t>(socket >> 8)});
    const std::string request = "PUT /CUT.BIN HTTP/1.1\r\nContent-Length: 1000\r\n\r\n" + std::string(100, 'z');
    _host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, Bytes(request.begin(), request.end()));
    _host->Push(NetEventType::PeerClosed, socket);
    _net->Pump();
    Run(400000);
    EXPECT_EQ(_plugin.Get("/CUT.BIN"), nullptr) << "a body cut short leaves no file";
    EXPECT_EQ(_esp->WebDav().LastStatus(), 507);
}

TEST_F(ZiFiWebDavServer_Test, DeleteAndMkcol)
{
    _plugin.Put("/OLD.TXT", Bytes(10, 1));
    StartServers();
    EXPECT_EQ(StatusLine(Http("DELETE /OLD.TXT HTTP/1.1\r\n\r\n")), "HTTP/1.1 400 Bad Request")
        << "a request line with no header line (the firmware finds no CRLF left)";
    EXPECT_EQ(StatusLine(Http("DELETE /OLD.TXT HTTP/1.1\r\nHost: zx\r\n\r\n")), "HTTP/1.1 204 No Content");
    EXPECT_EQ(_plugin.Get("/OLD.TXT"), nullptr);
    EXPECT_EQ(StatusLine(Http("DELETE /OLD.TXT HTTP/1.1\r\nHost: zx\r\n\r\n")), "HTTP/1.1 404 Not Found");
    EXPECT_EQ(StatusLine(Http("MKCOL /DOCS HTTP/1.1\r\nHost: zx\r\n\r\n")), "HTTP/1.1 201 Created");
    ASSERT_TRUE(_plugin.files.count("/DOCS"));
    EXPECT_TRUE(_plugin.files["/DOCS"].dir);
}

TEST_F(ZiFiWebDavServer_Test, TheUartWaitsWhileARequestRuns)
{
    _plugin.Put("/A.BIN", Bytes(2048, 3));
    _plugin.swallowCmd = 0x51;   // the plugin never answers READ: the GET hangs in its VFS wait
    StartServers();
    Http("GET /A.BIN HTTP/1.1\r\nHost: zx\r\n\r\n", 40000);
    EXPECT_TRUE(_esp->WebDav().JobActive());
    Send(ZiFiNativeModule::kPing);
    Run();
    EXPECT_EQ(_replies, Bytes({0xF0})) << "inside a VFS wait PING is still answered";
    _replies.clear();
    Send(ZiFiNativeModule::kSysInfo);
    Run();
    EXPECT_TRUE(_replies.empty()) << "the rest is lost while the VFS waits (handleUnexpected)";
}

TEST_F(ZiFiWebDavServer_Test, StateSurvivesACheckpointMidRequest)
{
    _plugin.Put("/A.BIN", Bytes(2048, 3));
    _plugin.swallowCmd = 0x51;
    StartServers();
    Http("GET /A.BIN HTTP/1.1\r\nHost: zx\r\n\r\n", 40000);
    std::vector<uint8_t> blob;
    _esp->SaveBridge(blob);
    auto copy = std::make_unique<ZiFiNativeModule>(_net.get(), ZiFiNativeModule::Variant::Esp01s);
    copy->SetClock([this]() { return _now; }, 3500000);
    ASSERT_TRUE(copy->LoadBridge(blob.data(), blob.size(), nullptr));
    EXPECT_TRUE(copy->WebDav().Running());
    EXPECT_TRUE(copy->WebDav().JobActive());
    EXPECT_EQ(copy->WebDav().JobText(), _esp->WebDav().JobText());
    std::vector<uint8_t> again;
    copy->SaveBridge(again);
    EXPECT_EQ(again, blob) << "the section round-trips";
}
