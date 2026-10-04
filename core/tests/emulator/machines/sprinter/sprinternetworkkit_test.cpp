// The Sprinter RTL8019AS network kit (testdata/machines/sprinter/network/rtl8019a-0.3.8, release 0.3.8) on DSS 1.71
// with the default NE2000 in ISA slot 2 (network tdd §15 T-NET-8): the kit's own programs find the card, read its
// PROM and load the configuration. Needs the MAME pack's system disk (UNREAL_SPRINTER_HDD, the raw sp_hdd_sys.img;
// not in the repository): skipped without it. The kit's files reach DSS on a 1.44 MB floppy the test builds (drive
// B:). Boot-bound (BIOS POST, DSS from the hard disk at 21 MHz, then each program): a few seconds of host time.

#include "sprinterkitdisk.h"
#include "sprinterzxsession.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "_helpers/scriptedhostnet.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/pcserialcard.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/network/vnet/ethernetgateway.h"
#include "emulator/state/devicestate.h"

class SprinterNetworkKit_Test : public SprinterZxSession_Test
{
protected:
    void SetUp() override
    {
        SprinterZxSession_Test::SetUp();
        if (IsSkipped() || HasFatalFailure())
            return;
        std::vector<KitFile> files = KitReleaseFiles("rtl8019a-0.3.8");
        ASSERT_FALSE(files.empty()) << "the RTL kit fixture is missing";
        files.push_back({"NET.CFG", DosText(NetCfg())});
        const std::vector<uint8_t> floppy = BuildFat12Floppy(files);
        _floppy = TestPathHelper::GetUniqueTestScratchPath("rtlkit.img");
        std::ofstream(_floppy, std::ios::binary).write(reinterpret_cast<const char*>(floppy.data()),
                                                      static_cast<std::streamsize>(floppy.size()));
        std::string error;
        ASSERT_TRUE(_emulator->LoadDisk(_floppy, 1, &error)) << error;
    }

    void TearDown() override
    {
        SprinterZxSession_Test::TearDown();
        if (!_floppy.empty())
            std::remove(_floppy.c_str());
    }

    /// RTL_HW: the kit numbers the slots 0 / 1 (pages #D4 / #D6): the emulator's slot 2 is the kit's 1
    virtual std::vector<std::string> NetCfg() const { return {"RTL_HW=1/#300", "IP=DHCP", "TZ=+0"}; }

    /// A kit program at the B:\ prompt, until it prints RESULT OK / RESULT FAIL (every kit tool ends so)
    std::string Run(const std::string& command, int maxFrames = 3000)
    {
        // BIOS 3.06 HF2 does not scroll DSS text at the bottom line (open item): a clear screen per program
        WaitPrompt();
        Dss("CLS");
        WaitPrompt();
        _results = ScreenCount("RESULT OK") + ScreenCount("RESULT FAIL");
        Dss(command);
        EmulatorTestHelper::RunUntil(_emulator.get(),
                                     [&] { return ScreenCount("RESULT OK") + ScreenCount("RESULT FAIL") > _results; },
                                     maxFrames, 5);
        WaitPrompt();
        if (std::getenv("UNREAL_SPRINTER_NET_TRACE"))
            std::printf("--- %s (frame %llu)\n%s", command.c_str(), static_cast<unsigned long long>(Frame()), Compact().c_str());
        return Compact();
    }

    /// The program is done and the shell waits: the last line on the screen is the prompt
    void WaitPrompt(int maxFrames = 1000)
    {
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] {
            const std::string text = Compact();
            const size_t end = text.find_last_not_of('\n');
            const size_t start = text.rfind('\n', end);
            const std::string last = text.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start + 1) + 1);
            return last.size() >= 4 && last.back() == '>' && last.find(":\\") == 1;
        }, maxFrames, 2);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 25);
    }

    /// The screen text without its empty lines (both mode pages)
    std::string Compact()
    {
        std::string out, line;
        for (char ch : ScreenText())
        {
            if (ch != '\n')
            {
                line.push_back(ch);
                continue;
            }
            if (line.find_first_not_of(' ') != std::string::npos)
                out += line.substr(0, line.find_last_not_of(' ') + 1) + "\n";
            line.clear();
        }
        return out;
    }

    std::string _floppy;
    size_t _results = 0;
};

TEST_F(SprinterNetworkKit_Test, NicinfoFindsTheCardAndNetcfgReadsItsMac)
{
    BootToPrompt();
    Dss("B:", 150);   // the drive change reads the floppy: keys typed meanwhile are lost
    std::string screen = Run("NICINFO");
    EXPECT_NE(screen.find("RTL8019"), std::string::npos) << screen;
    EXPECT_NE(screen.find("RESULT OK"), std::string::npos) << screen;
    EXPECT_NE(screen.find("02:53:50:00:00:02"), std::string::npos) << "the PROM's MAC\n" << screen;

    screen = Run("NETCFG -i");
    EXPECT_EQ(ScreenCount("RESULT FAIL"), 0u) << screen;

    // The ISA slot report saw the kit's accesses: the ID read, remote DMA through the data port
    const SprinterIsaBus::Counters& c = _decoder->GetIsaBus().GetCounters(1);
    EXPECT_GT(c.ioReads, 30u);
    EXPECT_GT(c.ioWrites, 10u);
}

// T-NET-9: the kit end to end through the Ethernet gateway - IFUP gets a DHCP lease, PING reaches the router, NSLOOKUP
// asks the host resolver (a scripted one), WGET fetches a file from a scripted HTTP server onto the hard disk; the
// bytes on the disk equal the served ones. The host behind the virtual network is ScriptedHostNet: no real sockets
TEST_F(SprinterNetworkKit_Test, IfupPingNslookupWgetThroughTheGateway)
{
    ASSERT_NE(_context->pVirtualNetwork, nullptr) << "the default NE2000 brings the virtual network";
    ASSERT_NE(_context->pEthernetGateway, nullptr);
    auto host = std::make_unique<ScriptedHostNet>();
    ScriptedHostNet* scripted = host.get();
    constexpr uint32_t kServer = NetIp(192, 0, 2, 10);
    std::vector<uint8_t> body(5000);
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<uint8_t>((i * 31) ^ (i >> 7));
    scripted->AddName("example.test", kServer);
    scripted->AddHttp({kServer, 80}, {{"/f.bin", body}});
    _context->pVirtualNetwork->ReplaceHost(std::move(host));

    BootToPrompt();
    Dss("B:", 150);
    std::string screen = Run("NETCFG -i");
    ASSERT_EQ(ScreenCount("RESULT FAIL"), 0u) << screen;
    screen = Run("IFUP", 3000);
    ASSERT_EQ(ScreenCount("RESULT FAIL"), 0u) << screen;
    EXPECT_NE(screen.find("10.0.2.15"), std::string::npos) << "the lease\n" << screen;
    screen = Run("PING -n 2 10.0.2.2", 3000);
    EXPECT_EQ(ScreenCount("RESULT FAIL"), 0u) << screen;
    EXPECT_NE(screen.find("Reply from 10.0.2.2"), std::string::npos) << screen;
    screen = Run("NSLOOKUP example.test", 3000);
    EXPECT_EQ(ScreenCount("RESULT FAIL"), 0u) << screen;
    EXPECT_NE(screen.find("192.0.2.10"), std::string::npos) << screen;
    screen = Run("WGET http://example.test/f.bin -o C:\\F.BIN -y", 6000);
    EXPECT_EQ(ScreenCount("RESULT FAIL"), 0u) << screen;

    std::vector<uint8_t> got;
    ASSERT_TRUE(Disk().Read("/F.BIN", got)) << screen;
    EXPECT_EQ(got, body);

    // The automation view of it: the gateway saw the lease, the TCP connection, the frames both ways
    const StateNode net = DeviceState::Network(_context);
    const StateNode* gateway = net.find("ethernet_gateway");
    ASSERT_NE(gateway, nullptr);
    EXPECT_EQ(gateway->find("ports")->items[0].find("dhcp_lease")->s, "10.0.2.15");
    EXPECT_GE(gateway->find("counters")->find("tcp_connections")->i, 1);
    EXPECT_GT(_context->pEthernetGateway->Capture().size(), 10u);
}

// T-NET-10: the same fetch recorded by TTD, then replayed from its first checkpoint with no host behind the network:
// the cards' blob (DP8390, packet RAM, the gateway's tables) and the virtual network's tables at the last checkpoint
// are byte for byte the recorded ones - every host answer came from the journal
TEST_F(SprinterNetworkKit_Test, TtdReplaysTheFetchWithoutTheHost)
{
    auto host = std::make_unique<ScriptedHostNet>();
    ScriptedHostNet* scripted = host.get();
    constexpr uint32_t kServer = NetIp(192, 0, 2, 10);
    scripted->AddName("example.test", kServer);
    scripted->AddHttp({kServer, 80}, {{"/g.bin", std::vector<uint8_t>(3000, 0xA5)}});
    _context->pVirtualNetwork->ReplaceHost(std::move(host));

    BootToPrompt();
    Dss("B:", 150);
    Run("NETCFG -i");
    ASSERT_EQ(ScreenCount("RESULT FAIL"), 0u);

    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelManager* ttd = _context->pTimeTravelManager;
    ASSERT_TRUE(ttd->StartRecording());
    std::string screen = Run("IFUP", 3000);
    ASSERT_EQ(ScreenCount("RESULT FAIL"), 0u) << screen;
    screen = Run("WGET http://example.test/g.bin -o C:\\G.BIN -y", 6000);
    ASSERT_EQ(ScreenCount("RESULT FAIL"), 0u) << screen;
    EmulatorTestHelper::RunFramesFast(_emulator.get(), 5);
    ttd->StopRecording();
    ASSERT_GE(ttd->GetCheckpointCount(), 10u);

    const size_t last = ttd->GetCheckpointCount() - 1;
    const ttd::TTDCheckpoint* end = ttd->GetCheckpoint(last);
    const uint64_t endFrame = end->time.frame;
    const auto recorded = end->peripheralBlobs;
    ASSERT_EQ(recorded.count(static_cast<uint8_t>(ttd::PeripheralId::EthernetNics)), 1u);

    // No host at all now: a replay that asked it would get nothing
    _context->pVirtualNetwork->ReplaceHost(std::make_unique<FakeHostNet>());
    ASSERT_TRUE(ttd->SeekTo({ttd->GetCheckpoint(0)->time.frame, 0}));
    _emulator->DisableTurboMode();
    while (Frame() < endFrame)
        _emulator->RunNFrames(1, true);
    std::unordered_map<uint8_t, std::vector<uint8_t>> live;
    ttd->GetPeripheralRegistry().CaptureAll(live);
    for (ttd::PeripheralId id : {ttd::PeripheralId::EthernetNics, ttd::PeripheralId::ZxNetUsb, ttd::PeripheralId::SprinterIsa})
    {
        const uint8_t key = static_cast<uint8_t>(id);
        ASSERT_EQ(live.count(key), 1u) << int(key);
        const std::vector<uint8_t> a = ttd::TTDPeripheralRegistry::DecodeBlob(key, live[key]);
        const std::vector<uint8_t> b = ttd::TTDPeripheralRegistry::DecodeBlob(key, recorded.at(key));
        ASSERT_EQ(a.size(), b.size()) << "device " << int(key);
        size_t first = 0;
        while (first < a.size() && a[first] == b[first])
            ++first;
        EXPECT_EQ(first, a.size()) << "device " << int(key) << " differs from byte " << first << " after the replay";
    }
}

// The Sprinter ESP Network Kit (testdata/machines/sprinter/network/sprinter-esp-0.2.1, release 0.2.1) with the
// SprinterESP card in ISA slot 1 (network tdd §15 T-NET-11): the kit's own programs drive the TL16C550C and the
// emulated ESP-12F with ESP-AT 2.2.2 - NETUP joins the virtual access point (DHCP), PING resolves a name and pings
// it, WGET fetches a file from a scripted HTTP server onto the hard disk byte for byte; a TTD replay of the session
// without the host gives the same blobs. Same disk setup as the RTL kit: BIOS 3.06 HF2, the kit on a floppy (B:)
class SprinterEspKit_Test : public SprinterZxSession_Test
{
protected:
    static constexpr uint32_t kServer = NetIp(192, 0, 2, 10);

    void ConfigureMachine(CONFIG& config) override
    {
        config.sprinter.isa.slot[0].kind = static_cast<uint8_t>(sprinterisa::CardKind::SprinterEsp);
        config.network.espChip = static_cast<uint8_t>(EspFirmware());
    }
    virtual EspModule::Firmware EspFirmware() const { return EspModule::Firmware::Esp8266At222; }

    void SetUp() override
    {
        SprinterZxSession_Test::SetUp();
        if (IsSkipped() || HasFatalFailure())
            return;
        std::vector<KitFile> files = KitReleaseFiles("sprinter-esp-0.2.1");
        ASSERT_FALSE(files.empty()) << "the ESP kit fixture is missing";
        // The UNET DLL and its smoke test from the kit's tag 0.2.1 (the release archive has neither)
        for (KitFile& f : KitReleaseFiles("sprinter-esp-0.2.1-unet"))
        {
            if (f.name != "LICENSE")
                files.push_back(std::move(f));
        }
        // NETUP reads NET.CFG beside NETUP.EXE (the kit's template keys)
        files.push_back({"NET.CFG", DosText({"SSID=UnrealNG", "PASS=", "DHCP=1", "IP=", "GATEWAY=", "NETMASK=", "DNS1=",
                                             "DNS2=", "TZ=+0", "NTP=pool.ntp.org", "AUTOJOIN=1", "BAUD=115200"})});
        const std::vector<uint8_t> floppy = BuildFat12Floppy(files);
        _floppy = TestPathHelper::GetUniqueTestScratchPath("espkit.img");
        std::ofstream(_floppy, std::ios::binary).write(reinterpret_cast<const char*>(floppy.data()),
                                                      static_cast<std::streamsize>(floppy.size()));
        std::string error;
        ASSERT_TRUE(_emulator->LoadDisk(_floppy, 1, &error)) << error;
        ASSERT_NE(_context->pVirtualNetwork, nullptr);

        auto host = std::make_unique<ScriptedHostNet>();
        _scripted = host.get();
        _scripted->AddName("example.test", kServer);
        _scripted->SetPingable(kServer);
        _context->pVirtualNetwork->ReplaceHost(std::move(host));
    }

    void TearDown() override
    {
        SprinterZxSession_Test::TearDown();
        if (!_floppy.empty())
            std::remove(_floppy.c_str());
    }

    /// A kit program at the B:\ prompt, until the prompt is back (the ESP kit prints no RESULT line)
    std::string Run(const std::string& command, int maxFrames = 3000)
    {
        WaitPrompt();
        Dss("CLS");   // BIOS 3.06 HF2 does not scroll DSS text at the bottom line
        WaitPrompt();
        Dss(command);
        WaitPrompt(maxFrames);
        const std::string screen = Compact();
        if (std::getenv("UNREAL_SPRINTER_NET_TRACE"))
            std::printf("--- %s (frame %llu)\n%s", command.c_str(), static_cast<unsigned long long>(Frame()), screen.c_str());
        return screen;
    }

    void WaitPrompt(int maxFrames = 1000)
    {
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] {
            const std::string text = Compact();
            const size_t end = text.find_last_not_of('\n');
            const size_t start = text.rfind('\n', end);
            const size_t from = start == std::string::npos ? 0 : start + 1;
            const std::string last = text.substr(from, end - from + 1);
            return last.size() >= 4 && last.back() == '>' && last.find(":\\") == 1;
        }, maxFrames, 2);
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 25);
    }

    std::string Compact()
    {
        std::string out, line;
        for (char ch : ScreenText())
        {
            if (ch != '\n')
            {
                line.push_back(ch);
                continue;
            }
            if (line.find_first_not_of(' ') != std::string::npos)
                out += line.substr(0, line.find_last_not_of(' ') + 1) + "\n";
            line.clear();
        }
        return out;
    }

    /// The ESP's recent AT exchanges, for a failure message
    std::string Exchanges()
    {
        std::string out;
        if (PcSerialCard* card = _context->pCore->GetNetworkManager()->SerialCard("isa1"))
        {
            if (EspModule* esp = card->Esp())
            {
                for (const EspModule::Exchange& e : esp->RecentExchanges())
                    out += "> " + e.request + "\n< " + e.reply + "\n";
            }
        }
        return out;
    }

    std::string _floppy;
    ScriptedHostNet* _scripted = nullptr;
};

TEST_F(SprinterEspKit_Test, NetupPingWgetThroughTheEsp)
{
    std::vector<uint8_t> body(5000);
    for (size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<uint8_t>((i * 37) ^ (i >> 5));
    _scripted->AddHttp({kServer, 80}, {{"/f.bin", body}});

    BootToPrompt();
    Dss("B:", 150);
    std::string screen = Run("NETUP", 4000);
    ASSERT_NE(screen.find("NETUP done."), std::string::npos) << screen << Exchanges();
    EXPECT_NE(screen.find("ESP firmware profile: 2.2.2."), std::string::npos) << screen;
    EXPECT_NE(screen.find("10.0.2.15"), std::string::npos) << "the DHCP lease\n" << screen;

    screen = Run("PING example.test", 3000);
    EXPECT_NE(screen.find("Reply time:"), std::string::npos) << screen << Exchanges();
    size_t lookups = 0;
    for (const FakeHostNet::Command& c : _scripted->commands)
        lookups += c.op == "dns" ? 1 : 0;
    EXPECT_GE(lookups, 1u) << "the name went to the host resolver";

    screen = Run("WGET http://example.test/f.bin -o C:\\F.BIN -y", 6000);
    EXPECT_NE(screen.find("Downloaded: 5000"), std::string::npos) << screen << Exchanges();
    std::vector<uint8_t> got;
    ASSERT_TRUE(Disk().Read("/F.BIN", got)) << screen;
    EXPECT_EQ(got, body);

    // The automation view: the slot row shows the ESP's session and the card's counters
    const StateNode net = DeviceState::Network(_context);
    const StateNode& row = net.find("slots")->items[0];
    EXPECT_EQ(row.find("card")->s, "sprinteresp");
    EXPECT_EQ(row.find("esp")->find("wifi")->s, "got_ip");
    EXPECT_EQ(row.find("esp")->find("ip")->s, "10.0.2.15");
    EXPECT_GT(row.find("uart")->find("bytes_in")->i, 5000);
}

TEST_F(SprinterEspKit_Test, TtdReplaysTheSessionWithoutTheHost)
{
    _scripted->AddHttp({kServer, 80}, {{"/g.bin", std::vector<uint8_t>(3000, 0x5A)}});
    BootToPrompt();
    Dss("B:", 150);

    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelManager* ttd = _context->pTimeTravelManager;
    ASSERT_TRUE(ttd->StartRecording());
    std::string screen = Run("NETUP", 4000);
    ASSERT_NE(screen.find("NETUP done."), std::string::npos) << screen << Exchanges();
    screen = Run("WGET http://example.test/g.bin -o C:\\G.BIN -y", 6000);
    ASSERT_NE(screen.find("Downloaded: 3000"), std::string::npos) << screen << Exchanges();
    // The recorded state, taken at the same frame phase the replay ends in (after the frame's network work: a
    // checkpoint is captured before it, when the UART has not caught up with the frame end yet)
    for (int f = 0; f < 5; ++f)
        _emulator->RunNFrames(1, true);
    const uint64_t endFrame = Frame();
    std::unordered_map<uint8_t, std::vector<uint8_t>> recordedRaw;
    ttd->GetPeripheralRegistry().CaptureAll(recordedRaw);
    ttd->StopRecording();
    ASSERT_GE(ttd->GetCheckpointCount(), 10u);
    std::unordered_map<uint8_t, std::vector<uint8_t>> recorded;
    for (auto& [key, blob] : recordedRaw)
        recorded[key] = blob;
    ASSERT_EQ(recorded.count(static_cast<uint8_t>(ttd::PeripheralId::SlotSerial1)), 1u) << "blob 46: the card's UART + ESP";

    _context->pVirtualNetwork->ReplaceHost(std::make_unique<FakeHostNet>());
    ASSERT_TRUE(ttd->SeekTo({ttd->GetCheckpoint(0)->time.frame, 0}));
    _emulator->DisableTurboMode();
    while (Frame() < endFrame)
        _emulator->RunNFrames(1, true);
    std::unordered_map<uint8_t, std::vector<uint8_t>> live;
    ttd->GetPeripheralRegistry().CaptureAll(live);
    for (ttd::PeripheralId id : {ttd::PeripheralId::SlotSerial1, ttd::PeripheralId::ZxNetUsb, ttd::PeripheralId::SprinterIsa,
                                 ttd::PeripheralId::EthernetNics})
    {
        const uint8_t key = static_cast<uint8_t>(id);
        ASSERT_EQ(live.count(key), 1u) << int(key);
        const std::vector<uint8_t> a = ttd::TTDPeripheralRegistry::DecodeBlob(key, live[key]);
        const std::vector<uint8_t> b = ttd::TTDPeripheralRegistry::DecodeBlob(key, recorded.at(key));
        ASSERT_EQ(a.size(), b.size()) << "device " << int(key);
        std::string diffs;
        size_t count = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i] != b[i] && count++ < 16)
                diffs += " @" + std::to_string(i) + ":" + std::to_string(b[i]) + "->" + std::to_string(a[i]);
        }
        EXPECT_EQ(count, 0u) << "device " << int(key) << " differs in " << count << " bytes after the replay (recorded->replayed)"
                             << diffs;
    }
}

// UNETESP.DLL (the kit's UNET interface, the Gopher browser's and network games' path) through its own smoke test:
// libman loads the DLL, NETINIT (it insists on NETUP's 2.2.2 profile), resolve, ping, CONNECT, SEND an HTTP HEAD,
// RECV (2.2.2 passive receive: AT+CIPRECVDATA), CLOSE
TEST_F(SprinterEspKit_Test, UnetDllThroughItsSmokeTest)
{
    _scripted->AddHttp({kServer, 80}, {});
    BootToPrompt();
    Dss("B:", 150);
    std::string screen = Run("NETUP", 4000);
    ASSERT_NE(screen.find("NETUP done."), std::string::npos) << screen << Exchanges();
    screen = Run("UNETTEST example.test 80", 4000);
    EXPECT_NE(screen.find("NETINIT ok"), std::string::npos) << screen << Exchanges();
    EXPECT_NE(screen.find("192.0.2.10"), std::string::npos) << "resolve\n" << screen;
    EXPECT_NE(screen.find("HTTP/1.0 404"), std::string::npos) << "the server's answer to HEAD /\n" << screen << Exchanges();
    EXPECT_NE(screen.find("done."), std::string::npos) << screen;
}

// The ESP-AT 2.2.1 compatibility path: NETUP's AT+SYSSTORE? probe gets ERROR, the kit takes its 2.2.1 profile
class SprinterEspKitAt221_Test : public SprinterEspKit_Test
{
protected:
    EspModule::Firmware EspFirmware() const override { return EspModule::Firmware::Esp8266At221; }
};

TEST_F(SprinterEspKitAt221_Test, NetupSelectsThe221Profile)
{
    BootToPrompt();
    Dss("B:", 150);
    const std::string screen = Run("NETUP", 4000);
    ASSERT_NE(screen.find("NETUP done."), std::string::npos) << screen << Exchanges();
    EXPECT_NE(screen.find("ESP firmware profile: 2.2.1."), std::string::npos) << screen;
}
