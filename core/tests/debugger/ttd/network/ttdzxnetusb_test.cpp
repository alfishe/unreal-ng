// TTD state of the network adapters (network adapters TDD §6.3, option A):
// received bytes are journaled NetEvents, checkpoints keep references to them,
// a seek restores the chip from the journal and a replay needs no network.

#include <gtest/gtest.h>

#include <cstring>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/fakehostnet.h"
#include "common/network/dnsmessage.h"
#include "base/featuremanager.h"
#include "debugger/ttd/network/ttdserialport.h"
#include "debugger/ttd/network/ttdzxnetusb.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/networkspec.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/network/zxnetusb.h"
#include "emulator/io/serial/comport.h"
#include "emulator/cpu/z80.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/memory/memory.h"

namespace
{
/// The card as the NedoOS driver reaches it: socket n's register window
struct CardPorts
{
    ZxNetUsb* card = nullptr;

    void Select(int socket)
    {
        card->portDeviceOutMethod(0x82AB, 0x10);
        card->portDeviceOutMethod(0x81AB, static_cast<uint8_t>(8 + socket));
    }
    void Out(uint8_t reg, uint8_t v) { card->portDeviceOutMethod(static_cast<uint16_t>((reg << 8) | 0xAB), v); }
    uint8_t In(uint8_t reg) { return card->portDeviceInMethod(static_cast<uint16_t>((reg << 8) | 0xAB)); }
};

std::vector<uint8_t> StateBlob(const ZxNetUsb& card)
{
    auto state = std::make_unique<netstate::Adapters>();
    card.SaveState(*state);
    const auto* p = reinterpret_cast<const uint8_t*>(state.get());
    return std::vector<uint8_t>(p, p + sizeof(*state));
}
}  // namespace

namespace
{
/// The network blob and the serial port blob, one after the other: a replay must reproduce both
std::vector<uint8_t> BothBlobs(EmulatorContext* context)
{
    ttd::TTDZxNetUsb network(context);
    ttd::TTDSerialPort serial(context);
    std::vector<uint8_t> b(network.TTDStateSize() + serial.TTDStateSize());
    network.TTDSaveState(b.data());
    serial.TTDSaveState(b.data() + network.TTDStateSize());
    return b;
}
}  // namespace

class TTDZxNetUsb_Test : public ::testing::Test
{
protected:
    struct Machine
    {
        Emulator* emulator = nullptr;
        EmulatorContext* context = nullptr;
        ttd::TimeTravelController* ttd = nullptr;
        FakeHostNet* host = nullptr;

        bool Create()
        {
            emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
            if (!emulator)
                return false;
            context = emulator->GetContext();
            ttd = context->pTimeTravelController;
            FeatureManager* features = emulator->GetFeatureManager();
            features->setFeature(Features::kDebugMode, true);
            features->setFeature(Features::kTimeTravel, true);
            context->pMemory->UpdateFeatureCache();

            context->config.network.card = 1;
            context->config.network.hostAccess = 0;
            context->pCore->ApplyNetworkConfiguration();
            if (!context->pVirtualNetwork || !context->pZxNetUsb)
                return false;
            auto fake = std::make_unique<FakeHostNet>();
            host = fake.get();
            context->pVirtualNetwork->ReplaceHost(std::move(fake));
            return ttd != nullptr;
        }

        void Destroy()
        {
            if (emulator)
                EmulatorTestHelper::CleanupEmulator(emulator);
            emulator = nullptr;
        }

        /// Socket 0: TCP, connected to 1.2.3.4:80 (before any recording)
        void Connect()
        {
            CardPorts ports{context->pZxNetUsb};
            ports.card->portDeviceOutMethod(0x83AB, 0x10);   // W5300 out of reset
            ports.Select(0);
            ports.Out(0x01, 0x01);                           // Sn_MR = TCP
            ports.Out(0x03, 0x01);                           // OPEN
            for (uint8_t i = 0; i < 4; ++i)
                ports.Out(static_cast<uint8_t>(0x14 + i), static_cast<uint8_t>(i + 1));
            ports.Out(0x12, 0);
            ports.Out(0x13, 80);
            ports.Out(0x03, 0x04);                           // CONNECT
            host->Push(NetEventType::Connected, host->Last("connect")->socket);
            emulator->RunNFrames(1);
        }

        uint32_t Rsr() const { return context->pZxNetUsb->Chip().GetSocket(0).rxReceived; }

        std::string Save()
        {
            std::ostringstream out;
            std::string err;
            EXPECT_TRUE(ttd->SerializeSession(out, err)) << err;
            return out.str();
        }

        bool Load(const std::string& file, std::string* errOut)
        {
            std::istringstream in(file);
            return ttd->DeserializeSession(in, *errOut);
        }
    };

    Machine _rec;
    Machine _play;

    void SetUp() override
    {
        ASSERT_TRUE(_rec.Create());
        ASSERT_TRUE(_play.Create());
    }

    void TearDown() override
    {
        _rec.Destroy();
        _play.Destroy();
    }

    /// Record: two TCP chunks arrive while recording. Returns the frame
    /// before the first chunk and the chip state at the end
    void RecordSession(uint64_t& beforeData, uint64_t& afterFirst, uint64_t& end, std::vector<uint8_t>& endState,
                       uint32_t& rsrAfterFirst)
    {
        _rec.Connect();
        ASSERT_EQ(_rec.context->pZxNetUsb->Chip().GetSocket(0).state, W5300::kSockEstablished);
        ASSERT_TRUE(_rec.ttd->StartRecording());
        _rec.emulator->RunNFrames(2);
        beforeData = _rec.context->emulatorState.frame_counter;

        const uint16_t id = _rec.host->Last("connect")->socket;
        _rec.host->Push(NetEventType::Data, id, NetEventStatus::Ok, {}, std::vector<uint8_t>(3000, 'A'));
        _rec.emulator->RunNFrames(2);
        afterFirst = _rec.context->emulatorState.frame_counter;
        rsrAfterFirst = _rec.Rsr();
        _rec.host->Push(NetEventType::Data, id, NetEventStatus::Ok, {}, std::vector<uint8_t>(100, 'B'));
        _rec.emulator->RunNFrames(3);
        end = _rec.context->emulatorState.frame_counter;
        endState = StateBlob(*_rec.context->pZxNetUsb);
        _rec.ttd->StopRecording();
    }
};

TEST_F(TTDZxNetUsb_Test, TheReceivedBytesAreJournaledNetEvents)
{
    uint64_t before = 0, afterFirst = 0, end = 0;
    uint32_t rsr = 0;
    std::vector<uint8_t> endState;
    RecordSession(before, afterFirst, end, endState, rsr);
    if (HasFatalFailure())
        return;

    const auto& journal = _rec.ttd->GetInputJournal();
    ASSERT_EQ(journal.NetInputs().size(), 2u);
    EXPECT_EQ(journal.NetInputs()[0].payloadLength, 3000u);
    EXPECT_EQ(journal.NetInputs()[1].payloadLength, 100u);
    EXPECT_EQ(journal.Payload().size(), 3100u);
    EXPECT_EQ(rsr, 3u * 2u + 3000u) << "three packets with size headers (1460 + 1460 + 80), no pad";
}

/// A seek back before the data and a run forward replays the NetEvents from
/// the journal: the chip ends where the recording ended, the host is not asked
TEST_F(TTDZxNetUsb_Test, ReplayFromBeforeTheDataReachesTheRecordedState)
{
    uint64_t before = 0, afterFirst = 0, end = 0;
    uint32_t rsr = 0;
    std::vector<uint8_t> endState;
    RecordSession(before, afterFirst, end, endState, rsr);
    if (HasFatalFailure())
        return;

    const size_t hostCommands = _rec.host->commands.size();
    ASSERT_TRUE(_rec.ttd->SeekTo({before, 0}));
    EXPECT_EQ(_rec.Rsr(), 0u) << "the checkpoint before the data";
    _rec.emulator->RunNFrames(static_cast<int>(end - before));
    ASSERT_EQ(_rec.context->emulatorState.frame_counter, end);
    EXPECT_EQ(StateBlob(*_rec.context->pZxNetUsb), endState) << "the replay diverged";
    EXPECT_EQ(_rec.host->commands.size(), hostCommands) << "a replay never talks to the host";
}

/// A seek to a checkpoint with bytes in the chip restores them from the journal
TEST_F(TTDZxNetUsb_Test, SeekRestoresBufferedBytesFromTheJournal)
{
    uint64_t before = 0, afterFirst = 0, end = 0;
    uint32_t rsr = 0;
    std::vector<uint8_t> endState;
    RecordSession(before, afterFirst, end, endState, rsr);
    if (HasFatalFailure())
        return;

    ASSERT_TRUE(_rec.ttd->SeekTo({afterFirst, 0}));
    EXPECT_EQ(_rec.Rsr(), rsr);
    CardPorts ports{_rec.context->pZxNetUsb};
    ports.Select(0);
    EXPECT_EQ(ports.In(0x30), 0x05);   // first packet: 1460 = #05B4
    EXPECT_EQ(ports.In(0x31), 0xB4);
    EXPECT_EQ(ports.In(0x30), 'A');
    EXPECT_EQ(ports.In(0x31), 'A');
}

/// The file keeps the network inputs: loaded into a fresh instance, the
/// session replays to the same chip state
TEST_F(TTDZxNetUsb_Test, LoadedSessionReplaysTheNetworkInputs)
{
    uint64_t before = 0, afterFirst = 0, end = 0;
    uint32_t rsr = 0;
    std::vector<uint8_t> endState;
    RecordSession(before, afterFirst, end, endState, rsr);
    if (HasFatalFailure())
        return;

    const std::string file = _rec.Save();
    std::string err;
    ASSERT_TRUE(_play.Load(file, &err)) << err;
    EXPECT_EQ(_play.ttd->GetInputJournal().NetInputs().size(), 2u);
    ASSERT_TRUE(_play.ttd->SeekTo({before, 0}));
    _play.emulator->RunNFrames(static_cast<int>(end - before));
    ASSERT_EQ(_play.context->emulatorState.frame_counter, end);
    EXPECT_EQ(StateBlob(*_play.context->pZxNetUsb), endState);
}

/// The COM port in the same blob (network TDD §7): a TCP peer's bytes are
/// journaled NetEvents; a replay from before them fills the UART the same way
/// without the host
class TTDComPort_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    FakeHostNet* _host = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();

        std::strcpy(_context->config.network.zxWifi, "TCP:127.0.0.1:2323");   // a ZX-WiFi card: Pentagon has no serial port

        _context->config.network.card |= networkspec::kCardZxWifi;
        _context->config.network.hostAccess = 0;
        _context->pCore->ApplyNetworkConfiguration();
        ASSERT_NE(_context->pComPort, nullptr);
        auto fake = std::make_unique<FakeHostNet>();
        _host = fake.get();
        _context->pVirtualNetwork->ReplaceHost(std::move(fake));
        auto* peer = dynamic_cast<StreamPeer*>(_context->pComPort->Peer());
        ASSERT_NE(peer, nullptr);
        peer->Reconnect();
        ASSERT_NE(_host->Last("connect"), nullptr);
        _host->Push(NetEventType::Connected, _host->Last("connect")->socket);
        _emulator->RunNFrames(1);
        ASSERT_TRUE(peer->Connected());

        // NedoOS type 2 (ZX-WiFi): 115200 8N1, FIFO trigger 8, AFE + RTS
        Z80* z80 = _context->pCore->GetZ80();
        z80->out(0xFAEF, 0x87);
        z80->out(0xFBEF, 0x03);
        z80->out(0xFCEF, 0x2F);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    std::vector<uint8_t> Blob()
    {
        return BothBlobs(_context);
    }
};

TEST_F(TTDComPort_Test, ReplayWithoutTheHostFillsTheUartTheSameWay)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2);
    const uint64_t before = _context->emulatorState.frame_counter;
    std::vector<uint8_t> text(40);
    for (size_t i = 0; i < text.size(); ++i)
        text[i] = static_cast<uint8_t>('a' + i % 26);
    _host->Push(NetEventType::Data, _host->Last("connect")->socket, NetEventStatus::Ok, {}, text);
    _emulator->RunNFrames(3);
    const uint64_t end = _context->emulatorState.frame_counter;
    const Uart16550::View v = _context->pComPort->Uart().GetView();
    EXPECT_EQ(v.rxCount, 8) << "auto-RTS held the peer at the trigger level";
    EXPECT_EQ(_context->pComPort->Peer()->Pending(), 32u);
    const std::vector<uint8_t> endBlob = Blob();
    _ttd->StopRecording();
    ASSERT_EQ(_ttd->GetInputJournal().NetInputs().size(), 1u);

    const size_t hostCommands = _host->commands.size();
    ASSERT_TRUE(_ttd->SeekTo({before, 0}));
    EXPECT_EQ(_context->pComPort->Uart().GetView().rxCount, 0);
    EXPECT_EQ(_context->pComPort->Peer()->Pending(), 0u);
    _emulator->RunNFrames(static_cast<int>(end - before));
    ASSERT_EQ(_context->emulatorState.frame_counter, end);
    EXPECT_EQ(Blob(), endBlob) << "the replay diverged";
    EXPECT_EQ(_host->commands.size(), hostCommands) << "a replay never talks to the host";
}

TEST_F(TTDComPort_Test, SeekRestoresBytesWaitingInThePeerFromTheJournal)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(1);
    _host->Push(NetEventType::Data, _host->Last("connect")->socket, NetEventStatus::Ok, {},
                std::vector<uint8_t>(20, 'z'));
    _emulator->RunNFrames(2);
    const uint64_t mid = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(1);
    const std::vector<uint8_t> afterMid = Blob();
    _emulator->RunNFrames(1);
    _ttd->StopRecording();

    // The checkpoint of frame `mid` holds the COM port caught up at that
    // boundary (the network devices' frame work runs before the checkpoint):
    // no character on the line, the 20 bytes in the peer or the FIFO - the
    // peer's restored from the journal
    ASSERT_TRUE(_ttd->SeekTo({mid, 0}));
    const Uart16550::View v = _context->pComPort->Uart().GetView();
    EXPECT_EQ(_context->pComPort->Peer()->Pending() + v.rxCount, 20u) << "every byte in the peer or the FIFO";
    EXPECT_GT(v.rxCount, 0) << "some reached the FIFO before the checkpoint";
    _emulator->RunNFrames(1);
    EXPECT_EQ(Blob(), afterMid) << "the restored bytes continue as recorded";
}

/// A COM peer given by name: the DNS answer is journaled like the data, so a
/// replay from before the lookup resolves and connects the same way without
/// the host
TEST(TTDComPortName_Test, TheNameLookupReplaysFromTheJournal)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    std::strcpy(context->config.network.zxWifi, "TCP:bbs.example.org:23");   // a ZX-WiFi card: Pentagon has no serial port
    context->config.network.card |= networkspec::kCardZxWifi;
    context->config.network.hostAccess = 0;
    context->pCore->ApplyNetworkConfiguration();
    ASSERT_NE(context->pComPort, nullptr);
    auto fake = std::make_unique<FakeHostNet>();
    FakeHostNet* host = fake.get();
    context->pVirtualNetwork->ReplaceHost(std::move(fake));
    auto* peer = dynamic_cast<StreamPeer*>(context->pComPort->Peer());
    ASSERT_NE(peer, nullptr);

    auto blob = [&] {
        return BothBlobs(context);
    };

    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(1);
    const uint64_t before = context->emulatorState.frame_counter;
    peer->Reconnect();   // the lookup starts inside the recording
    const FakeHostNet::Command query = *host->Last("dns");
    dns::Question q;
    ASSERT_TRUE(dns::ParseQuery(query.data.data(), query.data.size(), q));
    host->Push(NetEventType::Datagram, query.socket, NetEventStatus::Ok, query.endpoint,
               dns::BuildAnswer(query.data.data(), query.data.size(), q, {NetIp(93, 184, 216, 34)}, dns::kRcodeNoError));
    emulator->RunNFrames(3);
    ASSERT_EQ(peer->ResolvedAddress(), NetIp(93, 184, 216, 34));
    const FakeHostNet::Command connect = *host->Last("connect");
    EXPECT_EQ(connect.endpoint.addr, NetIp(93, 184, 216, 34));
    host->Push(NetEventType::Connected, connect.socket);
    host->Push(NetEventType::Data, connect.socket, NetEventStatus::Ok, {}, {'W', 'e', 'l', 'c', 'o', 'm', 'e'});
    emulator->RunNFrames(3);
    ASSERT_TRUE(peer->Connected());
    const uint64_t end = context->emulatorState.frame_counter;
    const std::vector<uint8_t> endBlob = blob();
    ttd->StopRecording();

    // Reconnect() between the checkpoints is outside the journal: seek to the
    // first frame after it and replay the rest
    const size_t hostCommands = host->commands.size();
    ASSERT_TRUE(ttd->SeekTo({before + 1, 0}));
    emulator->RunNFrames(static_cast<int>(end - before - 1));
    EXPECT_EQ(blob(), endBlob) << "the replay diverged";
    EXPECT_TRUE(peer->Connected());
    EXPECT_EQ(host->commands.size(), hostCommands) << "a replay never talks to the host";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// An emulated ESP module (ESPNET) in the same blob: its socket buffers by
/// journal reference, its protocol state by value; a replay from before the
/// data rebuilds it without the host
TEST(TTDEspModule_Test, ReplayWithoutTheHostRebuildsTheModule)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    std::strcpy(context->config.network.zxWifi, "ESPNET");   // a ZX-WiFi card: Pentagon has no serial port
    context->config.network.card |= networkspec::kCardZxWifi;
    context->config.network.hostAccess = 0;
    context->pCore->ApplyNetworkConfiguration();
    ASSERT_NE(context->pComPort, nullptr);
    auto fake = std::make_unique<FakeHostNet>();
    FakeHostNet* host = fake.get();
    context->pVirtualNetwork->ReplaceHost(std::move(fake));

    // The ZX side: FIFOs on, 115200 8N1, RTS off (the module's replies wait in it)
    Z80* z80 = context->pCore->GetZ80();
    z80->out(0xFAEF, 0x07);
    z80->out(0xFBEF, 0x03);
    auto frame = [&](const std::vector<uint8_t>& bytes) {
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            z80->out(0xF8EF, bytes[i]);
            if (i % 12 == 11)
                emulator->RunNFrames(1);   // the 16-byte TX FIFO drains at the line rate
        }
        emulator->RunNFrames(1);
    };
    frame({0xA5, 0x01, 0xFF, 0x01, 0x01, 0x01, 0x00, 0x02});   // SOCKET TCP
    frame({0xA5, 0x03, 0x00, 0x00, 0x02, 0x0F, 0x00, 0x02, 0x00, 0x50, 93, 184, 216, 34, 0, 0, 0, 0, 0, 0, 0, 0});
    ASSERT_NE(host->Last("connect"), nullptr);
    const uint16_t socket = host->Last("connect")->socket;
    host->Push(NetEventType::Connected, socket);
    emulator->RunNFrames(2);

    auto blob = [&] {
        return BothBlobs(context);
    };

    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(1);
    const uint64_t before = context->emulatorState.frame_counter;
    host->Push(NetEventType::Data, socket, NetEventStatus::Ok, {}, std::vector<uint8_t>(500, 'q'));
    emulator->RunNFrames(3);
    const uint64_t end = context->emulatorState.frame_counter;
    auto* esp = dynamic_cast<EspModule*>(context->pComPort->Peer());
    ASSERT_NE(esp, nullptr);
    EXPECT_EQ(esp->Stack().GetSlot(0).rx.size(), 500u) << "the bytes wait in the module until READ";
    const std::vector<uint8_t> endBlob = blob();
    ttd->StopRecording();

    const size_t hostCommands = host->commands.size();
    ASSERT_TRUE(ttd->SeekTo({before, 0}));
    EXPECT_EQ(esp->Stack().GetSlot(0).rx.size(), 0u);
    emulator->RunNFrames(static_cast<int>(end - before));
    EXPECT_EQ(blob(), endBlob) << "the replay diverged";
    EXPECT_EQ(esp->Stack().GetSlot(0).rx.size(), 500u);
    EXPECT_EQ(host->commands.size(), hostCommands) << "a replay never talks to the host";
    EmulatorTestHelper::CleanupEmulator(emulator);
}
