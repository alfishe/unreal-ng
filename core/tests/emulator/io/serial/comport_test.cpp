// The serial port on #xxEF (network adapters TDD §7): the ZX-Evo AVR's UART
// (always there, its peer by [NETWORK] ComPort=, its behavior by [EVO] Avr=)
// or a ZX-WiFi card (Card=ZXWIFI, its ESP by ZxWifi=); decoded like the
// ZX-Evo FPGA (low byte #EF, register = A10..A8), reached by Z80 code, kept
// in the TTD blob

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "_helpers/fakehostnet.h"
#include "common/serial/hostserialport.h"
#include "debugger/ttd/network/ttdserialport.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/serial/comport.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"
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

    bool Apply(const std::vector<std::pair<std::string, std::string>>& settings)
    {
        NetworkManager::Change change;
        std::string error;
        std::vector<std::pair<std::string, std::string>> all = settings;
        all.emplace_back("host_access", "off");
        if (!NetworkManager::ParseChange(all, change, error))
            return false;
        return _context->pCore->GetNetworkManager()->RequestChange(change, error);
    }

    bool Fit(const std::string& comPort) { return Apply({{"com_port", comPort}}); }

    std::vector<std::string> Notes() { return _context->pCore->GetNetworkManager()->GetStatus().notes; }

    static bool HasNote(const std::vector<std::string>& notes, const char* text)
    {
        for (const std::string& n : notes)
        {
            if (n.find(text) != std::string::npos)
                return true;
        }
        return false;
    }

    /// The AVR's wait for one access, in CPU clocks at the current speed
    uint32_t ClocksFor(uint32_t avrCycles) const
    {
        const uint64_t cpuHz = _context->emulatorState.current_z80_frequency;
        return static_cast<uint32_t>((static_cast<uint64_t>(avrCycles) * cpuHz + 11059199) / 11059200);
    }

    void Load(uint16_t at, const std::vector<uint8_t>& code)
    {
        for (size_t i = 0; i < code.size(); ++i)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(at + i), code[i]);
    }
};

TEST_F(ComPort_Test, TheEvoAvrUartIsAlwaysThere)
{
    Create("ATM3", 4096);
    ComPort* com = _context->pComPort;
    ASSERT_NE(com, nullptr) << "the AVR's UART is on the mainboard: there with ComPort=NONE";
    EXPECT_EQ(com->Peer(), nullptr);
    EXPECT_EQ(_context->pVirtualNetwork, nullptr) << "nothing on the line: no virtual network";
    EXPECT_EQ(com->Uart().GetParams().avr, Uart16550::AvrFirmware::Base2023) << "the default AVR firmware";
    Z80* z80 = _context->pCore->GetZ80();
    EXPECT_EQ(z80->in(0xFFEF), 0xFF) << "SCR after the AVR's power-on";
    EXPECT_EQ(z80->in(0xFDEF), 0x60) << "LSR";
}

TEST_F(ComPort_Test, OtherMachinesHaveNoSerialPortOfTheirOwn)
{
    Create("PENTAGON", 128);
    EXPECT_EQ(_context->pComPort, nullptr);
    ASSERT_TRUE(Fit("loopback"));
    EXPECT_EQ(_context->pComPort, nullptr);
    EXPECT_TRUE(HasNote(Notes(), "no serial port of its own")) << "the status says why ComPort= did nothing";
}

TEST_F(ComPort_Test, TheEvoDecodesOnlyA10ToA8)
{
    Create("ATM3", 4096);
    ASSERT_TRUE(Fit("loopback"));
    ComPort* com = _context->pComPort;
    ASSERT_NE(com, nullptr);
    EXPECT_EQ(com->Uart().GetParams().flavor, Uart16550::Flavor::EvoAvr);

    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0x07EF, 0x42);                // A10..A8 = 7: SCR, A15..A11 ignored
    EXPECT_EQ(z80->in(0xFFEF), 0x42);
    EXPECT_EQ(z80->in(0x3FEF), 0x42);
    EXPECT_EQ(z80->in(0xFDEF), 0x60) << "LSR";
}

TEST_F(ComPort_Test, EveryEvoAccessWaitsForTheAvr)
{
    Create("ATM3", 4096);
    const Uart16550::Params& p = _context->pComPort->Uart().GetParams();
    Z80* z80 = _context->pCore->GetZ80();
    uint32_t before = z80->t;
    (void)z80->in(0xFDEF);
    const uint32_t first = z80->t - before;
    EXPECT_GE(first, ClocksFor(p.isrCycles + p.serviceRead)) << "the Z80 is held on /WAIT while the AVR serves the port";
    EXPECT_LE(first, ClocksFor(p.isrCycles + p.loopCycles + p.serviceRead));

    // A polling loop (IN A,(C): RRCA: JP NC: ~25 T between accesses) waits
    // for most of the main loop's next pass
    z80->AddWaitStates(25 * _context->emulatorState.current_z80_frequency_multiplier);
    before = z80->t;
    (void)z80->in(0xFDEF);
    const uint32_t polled = z80->t - before;
    const double us = polled * 1e6 / static_cast<double>(_context->emulatorState.current_z80_frequency);
    EXPECT_GT(us, 44.0) << "45..50 us per LSR poll (reference-evo-com-port.md §3)";
    EXPECT_LT(us, 51.0);
}

TEST_F(ComPort_Test, ARefitKeepsTheEvoUartRegisters)
{
    Create("ATM3", 4096);
    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0xFFEF, 0x5C);
    ASSERT_TRUE(Fit("loopback"));
    EXPECT_NE(_context->pComPort->Peer(), nullptr);
    EXPECT_EQ(z80->in(0xFFEF), 0x5C) << "a new cable, the same chip";
    ASSERT_TRUE(Fit("none"));
    ASSERT_NE(_context->pComPort, nullptr);
    EXPECT_EQ(_context->pComPort->Peer(), nullptr);
    EXPECT_EQ(_context->pVirtualNetwork, nullptr) << "nothing left on a line: the virtual network goes";
    EXPECT_EQ(z80->in(0xFFEF), 0x5C);
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
    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0xFFEF, 0x11);
    _emulator->Reset();
    EXPECT_EQ(_context->pCore->GetZ80()->in(0xFFEF), 0x11) << "the AVR resets its UART only on its own hard reset";
    _manager->RemoveEmulator(_emulator->GetId());
    _emulator.reset();

    Create("PENTAGON", 128);
    ASSERT_TRUE(Apply({{"card", "zxwifi"}, {"zx_wifi", "loopback"}}));
    ASSERT_NE(_context->pComPort, nullptr);
    EXPECT_EQ(_context->pComPort->Uart().GetParams().flavor, Uart16550::Flavor::Chip16550);
    _context->pCore->GetZ80()->out(0xFFEF, 0x11);
    _emulator->Reset();
    EXPECT_EQ(_context->pCore->GetZ80()->in(0xFFEF), 0x00) << "ZX-Bus /RESET resets the 16550";
}

TEST_F(ComPort_Test, TheZxWifiCardRunsAtByDefault)
{
    Create("PENTAGON", 128);
    ASSERT_TRUE(Apply({{"card", "zxwifi"}}));
    ASSERT_NE(_context->pComPort, nullptr);
    ASSERT_NE(_context->pComPort->Peer(), nullptr);
    EXPECT_STREQ(_context->pComPort->Peer()->Kind(), "at") << "the card's ESP ships with the AT firmware";
    EXPECT_EQ(_context->pCore->GetZ80()->in(0xFDEF), 0x60) << "no /WAIT logic, a real 16550";

    ASSERT_TRUE(Apply({{"card", "zxnetusb,zxwifi"}}));
    EXPECT_NE(_context->pZxNetUsb, nullptr);
    EXPECT_NE(_context->pComPort, nullptr) << "both cards on the ZX-Bus";
    ASSERT_TRUE(Apply({{"card", "none"}}));
    EXPECT_EQ(_context->pComPort, nullptr);
    EXPECT_EQ(_context->pVirtualNetwork, nullptr);
}

TEST_F(ComPort_Test, AZxWifiCardClashesWithTheEvoAvr)
{
    Create("ATM3", 4096);
    ASSERT_TRUE(Apply({{"card", "zxnetusb,zxwifi"}}));
    EXPECT_NE(_context->pZxNetUsb, nullptr) << "the other card still fits";
    ASSERT_NE(_context->pComPort, nullptr);
    EXPECT_EQ(_context->pComPort->Uart().GetParams().flavor, Uart16550::Flavor::EvoAvr) << "#xxEF stays the AVR's";
    EXPECT_TRUE(HasNote(Notes(), "ZXWIFI: not fitted"));
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
    EXPECT_NE(text.find("evo-avr"), std::string::npos);
    EXPECT_NE(text.find("BASE2023"), std::string::npos);
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

    ttd::TTDSerialPort serializer(_context);
    ASSERT_EQ(serializer.TTDStateSize(), sizeof(netstate::SerialPort)) << "a peer: the full blob";
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

TEST_F(ComPort_Test, TtdKeepsAnUnconnectedEvoUartInAShortBlob)
{
    Create("ATM3", 4096);
    ttd::TTDSerialPort serializer(_context);
    EXPECT_EQ(serializer.TTDStateSize(), netstate::kSerialPortShortSize) << "no peer: the UART only";
    EXPECT_LT(serializer.TTDStateSize(), 1024u);
    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0xFFEF, 0x21);
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());
    z80->out(0xFFEF, 0x43);
    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(_context->pComPort->Uart().GetView().scr, 0x21);
}

TEST_F(ComPort_Test, TsConfKeepsItsPortsForZiFi)
{
    Create("TSL", 4096);
    ASSERT_TRUE(Fit("loopback"));
    EXPECT_EQ(_context->pComPort, nullptr) << "#xxEF is ZiFi on TS-Conf";
    EXPECT_TRUE(HasNote(Notes(), "ZiFi"));
    ASSERT_TRUE(Apply({{"card", "zxwifi"}}));
    EXPECT_EQ(_context->pComPort, nullptr);
    EXPECT_TRUE(HasNote(Notes(), "ZXWIFI: not fitted"));
}

TEST_F(ComPort_Test, TheAvrFirmwareSetsTheUart)
{
    Create("ATM3", 4096);
    _context->config.atm.evo_avr = static_cast<uint8_t>(Uart16550::AvrFirmware::Ts2013);
    ASSERT_TRUE(Fit("loopback"));
    EXPECT_EQ(_context->pComPort->Uart().GetParams().rxDepth, 256);
    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_EQ(st.com.firmware, "TS2013");
}

TEST_F(ComPort_Test, ATsFirmwareFrom2016MissesTheRegistersOnBaseConf)
{
    // 2016-02 .. 2021-04: the index from SPI #42 (0..7) lands in the ZiFi data area
    Create("ATM3", 4096);
    _context->config.atm.evo_avr = static_cast<uint8_t>(Uart16550::AvrFirmware::Ts2016Feb);
    ASSERT_TRUE(Fit("loopback"));
    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0xFFEF, 0x42);
    EXPECT_EQ(z80->in(0xFFEF), 0xFF);
    EXPECT_EQ(z80->in(0xFDEF), 0xFF);
    EXPECT_EQ(_context->pComPort->Uart().GetView().scr, 0xFF) << "the write never reached SCR";
}

TEST_F(ComPort_Test, TheCurrentTsFirmwareTakesTheGlukAddressAsTheIndex)
{
    Create("ATM3", 4096);
    _context->config.atm.evo_avr = static_cast<uint8_t>(Uart16550::AvrFirmware::Ts2016Apr);
    ASSERT_TRUE(Fit("loopback"));
    Z80* z80 = _context->pCore->GetZ80();
    // The clock's address latch (#DFF7, or #DEF7 in shadow - the Gluk decode has its own tests)
    Ds12887& gluk = static_cast<PortDecoder_ATM3*>(_context->pPortDecoder)->GetRtc();
    gluk.WriteAddress(0xFF);   // Kondratyev register 7 (SCR)
    z80->out(0xF8EF, 0x5A);    // A10..A8 = 0 does not matter
    EXPECT_EQ(_context->pComPort->Uart().GetView().scr, 0x5A);
    EXPECT_EQ(z80->in(0xFDEF), 0x5A);
    gluk.WriteAddress(0x0C);   // a clock cell: the ZiFi data area
    EXPECT_EQ(z80->in(0xFFEF), 0xFF);
    gluk.WriteAddress(0xE0);   // nothing there
    EXPECT_EQ(z80->in(0xFFEF), 0x00);
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

TEST_F(ComPort_Test, TheAvrFirmwareChangesAtRuntime)
{
    Create("ATM3", 4096);
    Z80* z80 = _context->pCore->GetZ80();
    z80->out(0xFFEF, 0x5C);
    ASSERT_TRUE(Apply({{"avr_firmware", "ts2013"}}));
    ASSERT_NE(_context->pComPort, nullptr);
    EXPECT_EQ(_context->pComPort->Uart().GetParams().avr, Uart16550::AvrFirmware::Ts2013);
    EXPECT_EQ(z80->in(0xFFEF), 0xFF) << "a new firmware restarts the AVR: SCR back to #FF";
    EXPECT_EQ(_context->pCore->GetNetworkManager()->GetStatus().settings.avrFirmware, "TS2013");

    NetworkManager::Change change;
    std::string error;
    EXPECT_FALSE(NetworkManager::ParseChange({{"avr_firmware", "ts2099"}}, change, error));
    EXPECT_NE(error.find("avr_firmware"), std::string::npos);
}

TEST_F(ComPort_Test, StatusCarriesTheSettingsInForce)
{
    Create("PENTAGON", 128);
    ASSERT_TRUE(Apply({{"card", "zxnetusb,zxwifi"}, {"zx_wifi", "espnet"}, {"esp_chip", "esp8266"}}));
    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_EQ(st.settings.card, "ZXNETUSB,ZXWIFI");
    EXPECT_EQ(st.settings.zxWifi, "ESPNET");
    EXPECT_EQ(st.settings.espChip, "ESP8266");
    EXPECT_EQ(st.settings.comPort, "NONE");
    EXPECT_FALSE(st.settings.hostAccess) << "the fixture turns host access off";

    const std::string text = DeviceState::ToText(DeviceState::Network(_context));
    EXPECT_NE(text.find("settings"), std::string::npos);
    EXPECT_NE(text.find("host_serial_devices"), std::string::npos);
}

TEST_F(ComPort_Test, HostSerialDevicesAreSortedDeviceNames)
{
    const std::vector<std::string> devices = HostSerialPort::ListDevices();
    EXPECT_TRUE(std::is_sorted(devices.begin(), devices.end()) || devices.empty());
    for (const std::string& d : devices)
        EXPECT_FALSE(d.empty());
}
