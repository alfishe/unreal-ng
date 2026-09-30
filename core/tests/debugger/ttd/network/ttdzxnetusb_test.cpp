// TTD state of the network adapters (network adapters TDD §6.3, option A):
// received bytes are journaled NetEvents, checkpoints keep references to them,
// a seek restores the chip from the journal and a replay needs no network.

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/fakehostnet.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/network/zxnetusb.h"
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

class TTDZxNetUsb_Test : public ::testing::Test
{
protected:
    struct Machine
    {
        Emulator* emulator = nullptr;
        EmulatorContext* context = nullptr;
        ttd::TimeTravelManager* ttd = nullptr;
        FakeHostNet* host = nullptr;

        bool Create()
        {
            emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
            if (!emulator)
                return false;
            context = emulator->GetContext();
            ttd = context->pTimeTravelManager;
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
