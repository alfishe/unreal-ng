// The machine's COM port (network adapters TDD §7): fitted by [NETWORK]
// ComPort=, decoded like the ZX-Evo FPGA (low byte #EF, register = A10..A8),
// reached by Z80 code, kept in the TTD blob

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "_helpers/fakehostnet.h"
#include "debugger/ttd/network/ttdzxnetusb.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/comport.h"
#include "emulator/memory/memory.h"
#include "emulator/state/devicestate.h"

class ComPort_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override { _manager = EmulatorManager::GetInstance(); }

    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    void Create(const char* model, int ram)
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("comport", model, ram, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    bool Fit(const std::string& comPort, const std::string& flavor = "auto")
    {
        NetworkManager::Change change;
        std::string error;
        if (!NetworkManager::ParseChange({{"com_port", comPort}, {"com_flavor", flavor}, {"host_access", "off"}},
                                         change, error))
            return false;
        return _context->pCore->GetNetworkManager()->RequestChange(change, error);
    }

    void Load(uint16_t at, const std::vector<uint8_t>& code)
    {
        for (size_t i = 0; i < code.size(); ++i)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(at + i), code[i]);
    }
};

TEST_F(ComPort_Test, NothingIsFittedByDefault)
{
    Create("ATM3", 4096);
    EXPECT_EQ(_context->pComPort, nullptr);
    EXPECT_EQ(_context->pVirtualNetwork, nullptr);
}

TEST_F(ComPort_Test, TheEvoDecodesOnlyA10ToA8)
{
    Create("ATM3", 4096);
    ASSERT_TRUE(Fit("loopback"));
    ComPort* com = _context->pComPort;
    ASSERT_NE(com, nullptr);
    EXPECT_EQ(com->Uart().GetParams().flavor, Uart16550::Flavor::EvoAvr) << "AUTO on the ZX-Evo: the AVR's UART";

    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0x07EF, 0x42);                // A10..A8 = 7: SCR, A15..A11 ignored
    EXPECT_EQ(z80->in(0xFFEF), 0x42);
    EXPECT_EQ(z80->in(0x3FEF), 0x42);
    EXPECT_EQ(z80->in(0xFDEF), 0x60) << "LSR";
}

TEST_F(ComPort_Test, EveryEvoAccessWaitsForTheAvr)
{
    Create("ATM3", 4096);
    ASSERT_TRUE(Fit("loopback"));
    Z80* z80 = _context->pCore->GetZ80();
    const uint32_t before = z80->t;
    (void)z80->in(0xFDEF);
    const uint32_t waited = z80->t - before;
    const uint32_t expected = _context->pComPort->Uart().GetParams().accessWaitT;
    EXPECT_GE(waited, expected) << "the Z80 is held on /WAIT while the AVR serves the port";
}

TEST_F(ComPort_Test, Z80CodeSendsAByteAndReadsTheEcho)
{
    Create("ATM3", 4096);
    ASSERT_TRUE(Fit("loopback"));
    _emulator->RunNFrames(1);

    // DI; LCR=3; MCR=2 (RTS); THR=#5A; wait LSR.DR; LD (#9000),RBR; JR $
    Load(0x8000, {0xF3,
                  0x01, 0xEF, 0xFB, 0x3E, 0x03, 0xED, 0x79,
                  0x01, 0xEF, 0xFC, 0x3E, 0x02, 0xED, 0x79,
                  0x01, 0xEF, 0xF8, 0x3E, 0x5A, 0xED, 0x79,
                  0x01, 0xEF, 0xFD, 0xED, 0x78, 0xE6, 0x01, 0x28, 0xF7,
                  0x01, 0xEF, 0xF8, 0xED, 0x78, 0x32, 0x00, 0x90,
                  0x18, 0xFE});
    _context->pMemory->DirectWriteToZ80Memory(0x9000, 0x00);
    _context->pCore->GetZ80()->pc = 0x8000;
    _emulator->RunNFrames(1);

    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9000), 0x5A);
    const Uart16550::View v = _context->pComPort->Uart().GetView();
    EXPECT_EQ(v.bytesOut, 1u);
    EXPECT_EQ(v.bytesIn, 1u);
}

TEST_F(ComPort_Test, AZ80ResetKeepsTheEvoUartButResetsAZxWifiCard)
{
    Create("ATM3", 4096);
    ASSERT_TRUE(Fit("loopback"));
    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0xFFEF, 0x11);
    _emulator->Reset();
    EXPECT_EQ(_context->pCore->GetZ80()->in(0xFFEF), 0x11) << "the AVR resets its UART only on its own hard reset";
    _manager->RemoveEmulator(_emulator->GetId());
    _emulator.reset();

    Create("PENTAGON", 128);
    ASSERT_TRUE(Fit("loopback"));
    EXPECT_EQ(_context->pComPort->Uart().GetParams().flavor, Uart16550::Flavor::ZxWifi) << "AUTO elsewhere: ZX-WiFi";
    _context->pCore->GetZ80()->out(0xFFEF, 0x11);
    _emulator->Reset();
    EXPECT_EQ(_context->pCore->GetZ80()->in(0xFFEF), 0x00) << "ZX-Bus /RESET resets the 16550";
}

TEST_F(ComPort_Test, FlavorCanBeChosen)
{
    Create("PENTAGON", 128);
    ASSERT_TRUE(Fit("loopback", "evo"));
    EXPECT_EQ(_context->pComPort->Uart().GetParams().flavor, Uart16550::Flavor::EvoAvr);
    ASSERT_TRUE(Fit("none"));
    EXPECT_EQ(_context->pComPort, nullptr);
    EXPECT_EQ(_context->pVirtualNetwork, nullptr) << "nothing left fitted: the virtual network goes too";
}

TEST_F(ComPort_Test, StatusShowsTheUartAndThePeer)
{
    Create("ATM3", 4096);
    ASSERT_TRUE(Fit("loopback"));
    _context->pCore->GetZ80()->out(0xFBEF, 0x03);
    _emulator->RunNFrames(1);
    const StateNode net = DeviceState::Network(_context);
    const std::string text = DeviceState::ToText(net);
    EXPECT_NE(text.find("com_port"), std::string::npos);
    EXPECT_NE(text.find("loopback"), std::string::npos);
    EXPECT_NE(text.find("evo"), std::string::npos);
}

TEST_F(ComPort_Test, TtdStateRestoresTheUartAndTheEchoQueue)
{
    Create("ATM3", 4096);
    ASSERT_TRUE(Fit("loopback"));
    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0xFBEF, 0x03);
    z80->out(0xFFEF, 0x33);
    z80->out(0xF8EF, 0x77);   // RTS off: the echo waits in the peer
    _emulator->RunNFrames(1);
    ASSERT_EQ(_context->pComPort->Peer()->Pending(), 1u);

    ttd::TTDZxNetUsb serializer(_context);
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());

    z80->out(0xFFEF, 0x99);
    z80->out(0xFCEF, 0x02);
    _emulator->RunNFrames(1);
    ASSERT_EQ(_context->pComPort->Peer()->Pending(), 0u);

    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(_context->pComPort->Uart().GetView().scr, 0x33);
    EXPECT_EQ(_context->pComPort->Uart().GetView().mcr, 0x00);
    EXPECT_EQ(_context->pComPort->Peer()->Pending(), 1u) << "the echo queue came back";
}

TEST_F(ComPort_Test, TsConfKeepsItsPortsForZiFi)
{
    Create("TSL", 4096);
    EXPECT_FALSE(Fit("loopback") && _context->pComPort != nullptr) << "#xxEF is ZiFi on TS-Conf";
    EXPECT_EQ(_context->pComPort, nullptr);
}

TEST_F(ComPort_Test, AMachineResetKeepsTheTcpLink)
{
    Create("ATM3", 4096);
    ASSERT_TRUE(Fit("tcp:127.0.0.1:2323"));
    auto fake = std::make_unique<FakeHostNet>();
    FakeHostNet* host = fake.get();
    _context->pVirtualNetwork->ReplaceHost(std::move(fake));
    auto* peer = dynamic_cast<StreamPeer*>(_context->pComPort->Peer());
    ASSERT_NE(peer, nullptr);
    peer->Reconnect();
    const uint16_t socket = host->Last("connect")->socket;
    host->Push(NetEventType::Connected, socket);
    _emulator->RunNFrames(1);
    ASSERT_TRUE(peer->Connected());

    _emulator->Reset();
    EXPECT_TRUE(peer->Connected()) << "a ZX-Bus reset does not unplug the cable";
    for (const FakeHostNet::Command& c : host->commands)
        EXPECT_FALSE((c.op == "close" && c.socket == socket) || c.op == "closeall") << c.op;

    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0xFBEF, 0x03);
    z80->out(0xF8EF, 'A');
    _emulator->RunNFrames(1);
    ASSERT_NE(host->Last("send"), nullptr);
    EXPECT_EQ(host->Last("send")->socket, socket);
    EXPECT_EQ(host->Last("send")->data, (std::vector<uint8_t>{'A'}));
}
