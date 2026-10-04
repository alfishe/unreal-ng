// PcSerialCard, preset SPRINTERESP (network tdd §8, T-NET-5): the SprinterESP Wi-Fi card in a Sprinter ISA slot -
// the TL16C550C at #3E8 as the rev 1.0.5 schematic decodes it (A13-A3, no AEN), its 14.7456 MHz clock, INTR to IRQ3
// without OUT2 gating, OUT1 holding the ESP-12F in reset, OUT2 pulling its GPIO0 low, ISA RESET DRV to the UART's MR
// only; the slot reports; a runtime change keeps the UART's registers. Every access goes through the ISA bus as the
// Z80 makes it (IsaAccess: the same cycles as window 3). The tests that wait for the ESP's boot banner run ~0.8 s of
// emulated time (40 frames, ~100-150 ms): the module's power-on to "ready" is 0.4 s, nothing shorter shows it.

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/pcserialcard.h"
#include "emulator/io/serial/esp/atmodule.h"
#include "emulator/io/sprinter/isa/isaaccess.h"
#include "emulator/io/sprinter/isa/sprinterisabus.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/state/devicestate.h"

class PcSerialCard_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        // The SprinterESP in slot 1 (the default NE2000 stays in slot 2)
        _emulator = _manager->CreateEmulatorWithModelAndRAM(
            "sprinter-esp", "SPRINTER", 4096, LoggerLevel::LogError, nullptr, [](CONFIG& config) {
                config.sprinter.isa.slot[0].kind = static_cast<uint8_t>(sprinterisa::CardKind::SprinterEsp);
            });
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
    }

    void TearDown() override
    {
        _emulator.reset();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }

    NetworkManager* Network() { return _context->pCore->GetNetworkManager(); }
    PcSerialCard* Card() { return Network()->SerialCard("isa1"); }

    uint8_t Io(const std::string& action, uint32_t address, int value = -1, int slot = 1)
    {
        StateNode result;
        std::string error;
        EXPECT_TRUE(IsaAccess::Execute(_context, action, slot, address, value, "test", result, error)) << error;
        const StateNode* v = result.find("value");
        return v ? static_cast<uint8_t>(std::strtoul(v->s.c_str() + 1, nullptr, 16)) : 0;
    }
    void Reg(int reg, uint8_t value) { Io("io_write", 0x3E8 + reg, value); }
    uint8_t Reg(int reg) { return Io("io_read", 0x3E8 + reg); }

    /// The kit's UART_INIT at 115 200 (divisor 8 from 14.7456 MHz), FIFO on, AFE + RTS (the UART paces the ESP)
    void InitUart()
    {
        Reg(2, 0xC7);
        Reg(1, 0x00);
        Reg(3, 0x83);
        Reg(0, 0x08);
        Reg(1, 0x00);
        Reg(3, 0x03);
        Reg(4, 0x22);
    }

    /// Run `frames` frames, emptying the receive FIFO after each (what the ESP sent meanwhile)
    std::string Drain(int frames)
    {
        std::string out;
        for (int f = 0; f < frames; ++f)
        {
            _emulator->RunNFrames(1, true);
            while (Reg(5) & 0x01)
                out.push_back(static_cast<char>(Reg(0)));
        }
        return out;
    }

    void Send(const std::string& text)
    {
        for (char c : text)
        {
            while (!(Reg(5) & 0x20))
                _emulator->RunNFrames(1, true);
            Reg(0, static_cast<uint8_t>(c));
        }
    }
};

TEST_F(PcSerialCard_Test, Decode_A13ToA3_NoAen_NoMirrorInTheWindow)
{
    ASSERT_NE(Card(), nullptr);
    EXPECT_STREQ(_decoder->GetIsaBus().Card(0)->Kind(), "sprinteresp");
    // The kit's probe (ESPKit UART_FIND): IER high nibble 0, the scratch register keeps #55 / #AA
    EXPECT_EQ(Reg(1) & 0xF0, 0);
    Reg(7, 0x55);
    EXPECT_EQ(Reg(7), 0x55);
    Reg(7, 0xAA);
    EXPECT_EQ(Reg(7), 0xAA);
    EXPECT_EQ(Io("io_read", 0x43EF), 0xAA) << "A19-A14 are not decoded: every 16 KB ISA page";
    EXPECT_EQ(Io("io_read", 0x7EF), 0xFF) << "A10 is decoded: no mirror at #7E8";
    EXPECT_EQ(Io("io_read", 0x2EF), 0xFF) << "#2E8 is not the card";
    EXPECT_EQ(Io("io_read", 0x3EF, -1, 2), 0xFF) << "slot 2 holds the NE2000 at #300";
    // AEN = 1: an ordinary I/O card ignores the cycle; this card's decoder does not see AEN
    _decoder->GetIsaBus().WriteLatch(SprinterIsaBus::kLatchAen);
    EXPECT_EQ(Reg(7), 0xAA);
    _decoder->GetIsaBus().WriteLatch(0);
}

TEST_F(PcSerialCard_Test, Reports_ResourcesThePathAndTheEsp)
{
    Network()->OnFrame();
    const StateNode isa = DeviceState::Isa(_context);
    const StateNode& slot1 = isa.find("slots")->items[0];
    EXPECT_EQ(slot1.find("card")->s, "sprinteresp");
    EXPECT_EQ(slot1.find("resources")->find("io")->s, "#3E8-#3EF");
    EXPECT_EQ(slot1.find("resources")->find("irq")->i, 3);
    const std::string path = slot1.find("z80_access")->find("io")->s;
    EXPECT_NE(path.find("page #D4"), std::string::npos) << path;
    EXPECT_NE(path.find("#C3E8-#C3EF"), std::string::npos) << path;
    EXPECT_NE(path.find("any #9FBD AEN"), std::string::npos) << path;
    EXPECT_EQ(slot1.find("chip")->s, "TL16C550C");
    EXPECT_EQ(slot1.find("esp")->find("firmware")->s, "ESP8266-AT222") << "the kit's firmware by default";
    EXPECT_NE(isa.find("summary")->s.find("slot 1: sprinteresp I/O #3E8-#3EF IRQ 3"), std::string::npos);

    const StateNode net = DeviceState::Network(_context);
    const StateNode& row = net.find("slots")->items[0];
    EXPECT_EQ(row.find("card")->s, "sprinteresp");
    EXPECT_EQ(row.find("port_key")->s, "isa1.uart0");
    EXPECT_EQ(row.find("uart_clock_hz")->i, 14745600);
    EXPECT_EQ(row.find("esp")->find("mac")->s, "5C:CF:7F:5A:00:01") << "Espressif form, instance 0, slot 1";
    EXPECT_EQ(row.find("esp")->find("state")->s, "running");
}

TEST_F(PcSerialCard_Test, Out1HoldsTheEspInReset_ReleaseBootsIt)
{
    InitUart();
    Drain(40);   // the power-up banner
    Send("AT\r\n");
    EXPECT_NE(Drain(5).find("OK"), std::string::npos);
    EXPECT_EQ(Card()->Com().Uart().Baud(), 115200u) << "14.7456 MHz / 16 / 8";

    // The kit's ESP_RESET: MCR = OUT1 (#04), 1 ms, MCR = RTS (#02)
    Reg(4, 0x04);
    EXPECT_TRUE(Card()->Esp()->ResetHeld());
    Reg(4, 0x22);
    EXPECT_FALSE(Card()->Esp()->ResetHeld());
    const std::string boot = Drain(40);
    EXPECT_NE(boot.find("ready"), std::string::npos) << boot;
    EXPECT_EQ(Card()->Esp()->HardwareResets(), 1u);
}

TEST_F(PcSerialCard_Test, Out2LowAtReleaseIsDownloadMode)
{
    InitUart();
    Drain(40);
    Reg(4, 0x0C);   // OUT1 + OUT2: reset held, GPIO0 low
    Reg(4, 0x2A);   // OUT2 stays: the ROM reads GPIO0 low
    EXPECT_TRUE(Card()->Esp()->DownloadMode());
    EXPECT_EQ(Drain(40), "") << "the boot ROM waits for a flasher";
    const StateNode net = DeviceState::Network(_context);
    (void)net;
    Reg(4, 0x24);
    Reg(4, 0x22);
    EXPECT_NE(Drain(40).find("ready"), std::string::npos);
}

TEST_F(PcSerialCard_Test, IsaResetDrvReachesTheUartOnly)
{
    InitUart();
    Drain(40);
    Reg(7, 0x5A);
    Reg(4, 0x04);   // the ESP held in reset by OUT1
    ASSERT_TRUE(Card()->Esp()->ResetHeld());
    // The kit's ISA_RESET: #9FBD <- #C0, 1 ms, #00: MR clears MCR, -OUT1 goes high, the ESP boots
    _decoder->GetIsaBus().WriteLatch(0xC0);
    _decoder->GetIsaBus().WriteLatch(0x00);
    EXPECT_EQ(Reg(4), 0x00);
    EXPECT_EQ(Reg(7), 0x00) << "the 16550's reset values";
    EXPECT_FALSE(Card()->Esp()->ResetHeld());
}

TEST_F(PcSerialCard_Test, IntrToIrq3IsNotGatedByOut2)
{
    InitUart();
    Drain(40);
    Reg(1, 0x01);   // receive-data interrupt
    Reg(4, 0x22);   // OUT2 clear (it is the ESP's GPIO0 here)
    Send("AT\r\n");
    _emulator->RunNFrames(3, true);
    EXPECT_TRUE(Card()->Irq()) << "INTR straight to IRQ3";
    EXPECT_FALSE(Card()->Com().Uart().InterruptActive()) << "a PC card would gate it by OUT2";
}

TEST_F(PcSerialCard_Test, RuntimeChange_NewFirmwareSameUart)
{
    InitUart();
    Reg(7, 0x77);
    NetworkManager::Change change;
    std::string error;
    ASSERT_TRUE(NetworkManager::ParseChange({{"esp_chip", "esp8266-at221"}}, change, error)) << error;
    ASSERT_TRUE(Network()->RequestChange(change, error)) << error;
    ASSERT_NE(Card(), nullptr);
    const auto* at = dynamic_cast<const AtModule*>(Card()->Esp());
    ASSERT_NE(at, nullptr);
    EXPECT_EQ(at->GetFirmware(), EspModule::Firmware::Esp8266At221);
    EXPECT_EQ(Reg(7), 0x77) << "the same 16550: its registers stay";
    EXPECT_EQ(Card()->Com().Uart().Baud(), 115200u);

    ASSERT_TRUE(NetworkManager::ParseChange({{"isa1_peer", "loopback"}}, change, error)) << error;
    ASSERT_TRUE(Network()->RequestChange(change, error)) << error;
    ASSERT_NE(Card(), nullptr);
    EXPECT_STREQ(Card()->Com().Peer()->Kind(), "loopback");
    Send("hi");
    EXPECT_EQ(Drain(3), "hi") << "the line looped back";
    EXPECT_NE(Network()->EthernetCard("isa2.eth"), nullptr) << "the NE2000 in slot 2 is untouched";
}
