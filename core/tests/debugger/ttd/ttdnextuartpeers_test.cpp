/// @file ttdnextuartpeers_test.cpp
/// @brief Time travel on the ZX Next: the peers of the UARTs. The UART state is in the NextState image (the FIFOs, the shifters, the
/// break state); the other end of a line is a peer with a state of its own: UART 0's is the machine serial peer's blob (27), UART 1's
/// (the Pi's, ComPort2=) is MachineSerialPeer2 (id 64), the joystick connector's device (ComPortJoy=) JoystickSerialPeer (id 65). Ids from
/// 64 on need the wide peripheral mask of the session files: the tests also read the device set back from a written file in the engine's
/// format.
///
/// Boots a Next and records a few frames: slower than the 50 ms guideline, an acceptance check.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/z80n/nextuart.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
constexpr uint8_t kIdPeer2 = static_cast<uint8_t>(ttd::PeripheralId::MachineSerialPeer2);
constexpr uint8_t kIdJoystick = static_cast<uint8_t>(ttd::PeripheralId::JoystickSerialPeer);

/// A Next with a loopback on UART 1 (the Pi's line) and one on the joystick connector
struct NextWithPeers
{
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    PortDecoder_Next* decoder = nullptr;
    LoopbackPeer* pi = nullptr;
    LoopbackPeer* joystick = nullptr;

    NextWithPeers()
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero, [](CONFIG& config) {
            std::snprintf(config.network.comPort2, sizeof config.network.comPort2, "LOOPBACK");
            std::snprintf(config.network.comPortJoy, sizeof config.network.comPortJoy, "LOOPBACK");
        });
        if (!emulator)
            return;
        context = emulator->GetContext();
        decoder = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
        pi = dynamic_cast<LoopbackPeer*>(context->pMachineSerialPeer2);
        joystick = dynamic_cast<LoopbackPeer*>(context->pJoystickSerialPeer);
        if (decoder)
            decoder->Board().WriteNextReg(0xA0, 0x30);   // UART 1's pins reach the Pi header (NR #A0 bits 5:4), else its line is cut
        emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
    }
    ~NextWithPeers()
    {
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }
    bool Ready() const { return emulator && decoder && pi && joystick; }
};

/// What the peers and the UART hold at a moment
struct Moment
{
    std::vector<uint8_t> piQueue;
    bool piBreak = false;
    unsigned piRx = 0;
    std::vector<uint8_t> joystickQueue;
    bool operator==(const Moment& o) const
    {
        return piQueue == o.piQueue && piBreak == o.piBreak && piRx == o.piRx && joystickQueue == o.joystickQueue;
    }
};

Moment Look(const NextWithPeers& n)
{
    Moment m;
    m.piQueue.assign(n.pi->Queue().begin(), n.pi->Queue().end());
    m.piBreak = n.pi->ForcedBreak();
    m.piRx = n.decoder->Uart().GetView(1).rxCount;
    m.joystickQueue.assign(n.joystick->Queue().begin(), n.joystick->Queue().end());
    return m;
}

std::string Describe(const Moment& m)
{
    std::ostringstream o;
    o << "pi queue " << m.piQueue.size() << (m.piBreak ? " break" : "") << ", UART 1 rx " << m.piRx << ", joystick queue "
      << m.joystickQueue.size();
    return o.str();
}

bool Has(const ttd::TTDRecordedMachine& machine, const char* name)
{
    return std::find(machine.peripherals.begin(), machine.peripherals.end(), name) != machine.peripherals.end();
}
}  // namespace

TEST(TTDNextUartPeers_Test, TheSecondPortAndTheConnectorPeersHaveBlobsOfTheirOwnAndRestoreOnASeek)
{
    NextWithPeers n;
    ASSERT_TRUE(n.Ready());
    auto controller = std::make_unique<ttd::TimeTravelController>(n.context);
    n.context->pTimeTravelHooks = controller.get();
    n.context->ttdWriteSink = controller.get();
    struct Detach
    {
        EmulatorContext* c;
        ttd::TimeTravelController* t;
        ~Detach()
        {
            t->StopRecording();
            c->pTimeTravelHooks = c->pTimeTravelController;
            c->ttdWriteSink = c->pTimeTravelController;
        }
    } detach{n.context, controller.get()};

    ASSERT_TRUE(controller->StartRecording());
    const ttd::TTDRecordedMachine machine = controller->GetSessionInfo().machine;
    EXPECT_TRUE(machine.V1Mask().Test(kIdPeer2)) << "UART 1's peer is a device of the session";
    EXPECT_TRUE(machine.V1Mask().Test(kIdJoystick)) << "and the connector's";
    EXPECT_TRUE(Has(machine, "machine-serial-peer-2"));
    EXPECT_TRUE(Has(machine, "joystick-serial-peer"));

    // The moments: the line held low with bytes waiting behind it, then released (the UART takes them), the connector's device gets
    // bytes of its own
    std::vector<Moment> live;
    live.push_back(Look(n));
    n.emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    live.push_back(Look(n));
    n.pi->SetQueue(std::vector<uint8_t>{0x11, 0x22, 0x33}.data(), 3);
    n.pi->SetBreak(true);
    n.joystick->SetQueue(std::vector<uint8_t>{0x77}.data(), 1);
    n.emulator->RunNFrames(1, true);
    live.push_back(Look(n));
    ASSERT_EQ(live.back().piQueue.size(), 3u) << "a held line starts no byte";
    ASSERT_TRUE(live.back().piBreak);
    n.pi->SetBreak(false);
    n.emulator->RunNFrames(1, true);
    n.emulator->RunNFrames(1, true);
    live.push_back(Look(n));
    ASSERT_TRUE(live.back().piQueue.empty());
    ASSERT_EQ(live.back().piRx, 3u) << "released, the UART took the three bytes";
    controller->StopRecording();

    const size_t count = controller->GetCheckpointCount();
    ASSERT_GE(count, 4u);
    const uint64_t firstFrame = controller->GetCheckpoint(0)->time.frame;
    size_t compared = 0;
    for (size_t i = count; i-- > 0;)   // newest first: a restore that does nothing would keep the end state
    {
        const uint64_t frame = controller->GetCheckpoint(i)->time.frame;
        ASSERT_TRUE(controller->SeekTo(ttd::TTDTimePoint{frame, 0})) << "checkpoint " << i;
        const uint64_t index = frame - firstFrame;
        if (index >= live.size())
            continue;
        const Moment now = Look(n);
        EXPECT_TRUE(now == live[index]) << "checkpoint " << i << " (frame " << index << "): " << Describe(now) << " (live "
                                        << Describe(live[index]) << ")";
        compared++;
    }
    EXPECT_GE(compared, 3u);

    // The device set reads back from a written file (the engine's format)
    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(controller->SerializeSession(out, err)) << err;
    ttd::TTDFileInfo info;
    std::istringstream in(out.str(), std::ios::binary);
    ASSERT_TRUE(ttd::ReadTTDFileInfo(in, info, err)) << err;
    EXPECT_TRUE(info.machine.V1Mask().Test(kIdPeer2));
    EXPECT_TRUE(info.machine.V1Mask().Test(kIdJoystick));
    EXPECT_TRUE(Has(info.machine, "machine-serial-peer-2"));
}
