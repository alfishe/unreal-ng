// PcSerialCard, preset SPRINTERESP (network tdd §8, T-NET-5): the SprinterESP Wi-Fi card in a Sprinter ISA slot -
// the TL16C550C at #3E8 as the rev 1.0.5 schematic decodes it (A13-A3, no AEN), its 14.7456 MHz clock, INTR to IRQ3
// without OUT2 gating, OUT1 holding the ESP-12F in reset, OUT2 pulling its GPIO0 low, ISA RESET DRV to the UART's MR
// only; the slot reports; a runtime change keeps the UART's registers. Every access goes through the ISA bus as the
// Z80 makes it (IsaAccess: the same cycles as window 3). The tests that wait for the ESP's boot banner run ~0.8 s of
// emulated time (40 frames, ~100-150 ms): the module's power-on to "ready" is 0.4 s, nothing shorter shows it.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/pcserialcard.h"
#include "emulator/io/serial/esp/atmodule.h"
#include "emulator/io/serial/hayesmodempeer.h"
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
    // Espressif form 5C:CF:7F:5A:<instance>:<slot>. The instance byte is the emulator's place among the live Sprinters
    // (an emulator an earlier test of the shard left alive shifts it), so only the fixed bytes and the slot are checked
    const std::string& mac = row.find("esp")->find("mac")->s;
    ASSERT_EQ(mac.size(), 17u) << mac;
    EXPECT_EQ(mac.substr(0, 12), "5C:CF:7F:5A:") << "Espressif form";
    EXPECT_EQ(mac.substr(14), ":01") << "slot 1";
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

// Presets MODEM and DUAL16552 (network phase SN4, T-NET-6 card part): the ISA Hayes modem at #3F8 (16550A at
// 1.8432 MHz, A9-A3 with AEN, IRQ through the OUT2 tri-state driver) and SprinterSerial (PC16552D: COM1 #3F8 /
// COM2 #2F8 by A8, A15-A10 decoded by D3 or not, AFR concurrent write, IRQ jumpers J5 / J6, unwired modem inputs)
class PcSerialCardPresets_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void Make(sprinterisa::CardKind kind, const std::function<void(sprinterisa::SlotConfig&)>& tune = nullptr)
    {
        _manager = EmulatorManager::GetInstance();
        _emulator = _manager->CreateEmulatorWithModelAndRAM(
            "sprinter-uart", "SPRINTER", 4096, LoggerLevel::LogError, nullptr, [kind, tune](CONFIG& config) {
                sprinterisa::SlotConfig& slot = config.sprinter.isa.slot[0];
                slot.kind = static_cast<uint8_t>(kind);
                if (kind == sprinterisa::CardKind::Modem)
                {
                    slot.base = sprinterisa::kModemDefaultBase;
                    slot.irq = sprinterisa::kModemDefaultIrq;
                }
                if (kind == sprinterisa::CardKind::Dual16552)
                    slot.irq = sprinterisa::kSerialDefaultIrqA;
                if (tune)
                    tune(slot);
            });
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
    }

    void TearDown() override
    {
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    NetworkManager* Network() { return _context->pCore->GetNetworkManager(); }
    PcSerialCard* Card() { return Network()->SerialCard("isa1"); }

    uint8_t Io(const std::string& action, uint32_t address, int value = -1)
    {
        StateNode result;
        std::string error;
        EXPECT_TRUE(IsaAccess::Execute(_context, action, 1, address, value, "test", result, error)) << error;
        const StateNode* v = result.find("value");
        return v ? static_cast<uint8_t>(std::strtoul(v->s.c_str() + 1, nullptr, 16)) : 0;
    }

    /// BC-Term's line setup at 57 600 (divisor 2 of 1.8432 MHz), FIFO on, DTR + RTS + OUT2
    void InitUart(uint16_t base)
    {
        Io("io_write", base + 2, 0xC7);
        Io("io_write", base + 3, 0x83);
        Io("io_write", base + 0, 0x02);
        Io("io_write", base + 1, 0x00);
        Io("io_write", base + 3, 0x03);
        Io("io_write", base + 4, 0x0B);
    }

    void Send(uint16_t base, const std::string& text)
    {
        for (char c : text)
        {
            while (!(Io("io_read", base + 5) & 0x20))
                _emulator->RunNFrames(1, true);
            Io("io_write", base, static_cast<uint8_t>(c));
        }
    }

    std::string Drain(uint16_t base, int frames)
    {
        std::string out;
        for (int f = 0; f < frames; ++f)
        {
            _emulator->RunNFrames(1, true);
            while (Io("io_read", base + 5) & 0x01)
                out.push_back(static_cast<char>(Io("io_read", base)));
        }
        return out;
    }
};

TEST_F(PcSerialCardPresets_Test, Modem_DecodeWithAen_Out2GatesTheIrq_AtAnswers)
{
    Make(sprinterisa::CardKind::Modem);
    ASSERT_NE(Card(), nullptr);
    EXPECT_STREQ(_decoder->GetIsaBus().Card(0)->Kind(), "modem");
    Io("io_write", 0x3FF, 0x5A);
    EXPECT_EQ(Io("io_read", 0x3FF), 0x5A);
    EXPECT_EQ(Io("io_read", 0x7FF), 0x5A) << "A15-A10 not decoded: mirrored every #400";
    _decoder->GetIsaBus().WriteLatch(SprinterIsaBus::kLatchAen);
    EXPECT_EQ(Io("io_read", 0x3FF), 0xFF) << "a PC card ignores AEN cycles";
    _decoder->GetIsaBus().WriteLatch(0);

    InitUart(0x3F8);
    EXPECT_EQ(Card()->Com().Uart().Baud(), 57600u) << "1.8432 MHz / 16 / 2";
    Io("io_write", 0x3FC, 0x2B);
    EXPECT_EQ(Io("io_read", 0x3FC), 0x0B) << "a 16550A has no AFE: MCR bits 7-5 read 0";
    Io("io_write", 0x3F9, 0x01);   // receive interrupt
    Send(0x3F8, "AT\r");
    EXPECT_EQ(Drain(0x3F8, 3), "AT\r\r\nOK\r\n");
    Io("io_write", 0x3FC, 0x03);   // OUT2 clear: the IRQ driver is off, the slot line floats
    Send(0x3F8, "AT\r");
    _emulator->RunNFrames(2, true);
    EXPECT_TRUE(Card()->Com().Uart().IntrPin());
    EXPECT_FALSE(Card()->IrqDriven()) << "OUT2 clear: tri-state";
    Io("io_write", 0x3FC, 0x0B);
    EXPECT_TRUE(Card()->IrqDriven());
    EXPECT_TRUE(Card()->Irq());

    const StateNode isa = DeviceState::Isa(_context);
    const StateNode& slot1 = isa.find("slots")->items[0];
    EXPECT_EQ(slot1.find("resources")->find("io")->s, "#3F8-#3FF");
    EXPECT_EQ(slot1.find("resources")->find("irq")->i, 4);
    Network()->OnFrame();
    const StateNode net = DeviceState::Network(_context);
    const StateNode& row = net.find("slots")->items[0];
    EXPECT_EQ(row.find("chip")->s, "16550A");
    EXPECT_EQ(row.find("peer_spec")->s, "MODEM");
    ASSERT_NE(row.find("modem"), nullptr);
    EXPECT_EQ(row.find("modem")->find("mode")->s, "command");
    EXPECT_EQ(row.find("modem")->find("last_result")->s, "OK");
}

TEST_F(PcSerialCardPresets_Test, Dual_ChannelsByA8_FullDecode_AfrConcurrentWrite_Jumpers)
{
    Make(sprinterisa::CardKind::Dual16552, [](sprinterisa::SlotConfig& slot) {
        std::snprintf(slot.peer, sizeof(slot.peer), "LOOPBACK");
        std::snprintf(slot.peerB, sizeof(slot.peerB), "MODEM");
        slot.irqB = 4;
    });
    ASSERT_NE(Card(), nullptr);
    EXPECT_EQ(Card()->Channels(), 2);
    Io("io_write", 0x3FF, 0x11);
    Io("io_write", 0x2FF, 0x22);
    EXPECT_EQ(Io("io_read", 0x3FF), 0x11) << "A8 = 1: channel A (COM1)";
    EXPECT_EQ(Io("io_read", 0x2FF), 0x22) << "A8 = 0: channel B (COM2)";
    EXPECT_EQ(Io("io_read", 0x7FF), 0xFF) << "D3 decodes A15-A10";
    _decoder->GetIsaBus().WriteLatch(SprinterIsaBus::kLatchAen);
    EXPECT_EQ(Io("io_read", 0x3FF), 0x11) << "AEN is not connected";
    _decoder->GetIsaBus().WriteLatch(0);
    EXPECT_EQ(Io("io_read", 0x103FF), 0x11) << "A19-A16 are not connected";
    EXPECT_EQ(Io("io_read", 0x43FF), 0xFF) << "A14 is decoded by D3";

    // AFR (DLAB set, register 2): bit 0 writes both channels at once; reads stay per channel
    Io("io_write", 0x3FB, 0x80);
    Io("io_write", 0x2FB, 0x80);
    Io("io_write", 0x3FA, 0x01);
    EXPECT_EQ(Io("io_read", 0x2FA), 0x01) << "one AFR for both register sets";
    Io("io_write", 0x3FF, 0x33);
    EXPECT_EQ(Io("io_read", 0x2FF), 0x33) << "the concurrent write reached COM2";
    Io("io_write", 0x2FA, 0x00);
    Io("io_write", 0x3FB, 0x03);
    Io("io_write", 0x2FB, 0x03);

    // Unwired modem inputs: COM1 sees none (the CH340's pins are inputs too), COM2 only CTS
    InitUart(0x3F8);
    InitUart(0x2F8);
    EXPECT_EQ(Io("io_read", 0x3FE) & 0xF0, 0x00) << "COM1: CTS, DSR, RI, DCD inactive";
    EXPECT_EQ(Io("io_read", 0x2FE) & 0xF0, 0x10) << "COM2: CTS from the modem, nothing else";
    Send(0x3F8, "hi");
    EXPECT_EQ(Drain(0x3F8, 2), "hi") << "COM1's line (a loopback plug here)";
    Send(0x2F8, "AT\r");
    EXPECT_EQ(Drain(0x2F8, 3), "AT\r\r\nOK\r\n") << "an external modem on COM2's DB-9";

    // J5 = IRQ 3 (COM1), J6 = IRQ 4 (COM2): on the Sprinter one line; both drive it
    EXPECT_EQ(Card()->IrqLine(), 3);
    EXPECT_TRUE(Card()->IrqDriven());
    Io("io_write", 0x2F9, 0x01);
    Send(0x2F8, "AT\r");
    _emulator->RunNFrames(3, true);
    EXPECT_TRUE(Card()->Irq());
    EXPECT_TRUE(Card()->IrqContention()) << "COM1's INTR drives low, COM2's high, on one net";

    Network()->OnFrame();
    const StateNode net = DeviceState::Network(_context);
    const StateNode& row = net.find("slots")->items[0];
    EXPECT_EQ(row.find("chip")->s, "PC16552D");
    EXPECT_EQ(row.find("port_key")->s, "isa1.uart0");
    ASSERT_NE(row.find("channel_b"), nullptr);
    EXPECT_EQ(row.find("channel_b")->find("port_key")->s, "isa1.uart1");
    EXPECT_EQ(row.find("channel_b")->find("peer_spec")->s, "MODEM");
    EXPECT_EQ(row.find("channel_b")->find("base")->s, "#2F8");
    EXPECT_NE(row.find("channel_b")->find("modem"), nullptr);
    EXPECT_TRUE(row.find("irq_contention")->b);
}

TEST_F(PcSerialCardPresets_Test, Dual_PartialDecodeMirrors_RuntimePeerB)
{
    Make(sprinterisa::CardKind::Dual16552, [](sprinterisa::SlotConfig& slot) { slot.partialDecode = 1; });
    ASSERT_NE(Card(), nullptr);
    Io("io_write", 0x3FF, 0x44);
    EXPECT_EQ(Io("io_read", 0x7FF), 0x44) << "D3 left out, J1 + J2 closed: A15-A10 not decoded";
    EXPECT_EQ(Io("io_read", 0x3BF), 0xFF) << "A6 is decoded";

    NetworkManager::Change change;
    std::string error;
    ASSERT_TRUE(NetworkManager::ParseChange({{"isa1_peer_b", "loopback"}}, change, error)) << error;
    ASSERT_TRUE(Network()->RequestChange(change, error)) << error;
    ASSERT_NE(Card(), nullptr);
    EXPECT_STREQ(Card()->Com(1).Peer()->Kind(), "loopback");
    EXPECT_EQ(Card()->Com(0).Peer(), nullptr) << "COM1 stays unconnected";
    EXPECT_EQ(Io("io_read", 0x3FF), 0x44) << "the same chip: its registers stay";
}

// A modem that answers calls owns its Forward= guest port: the Ethernet gateway of the slot-2 NE2000 does not listen
// on it (else a host client of the port would reach whichever device waited first)
TEST_F(PcSerialCardPresets_Test, Modem_AnswerPortIsNotTakenByTheEthernetGateway)
{
    Make(sprinterisa::CardKind::Modem, [](sprinterisa::SlotConfig& slot) {
        std::snprintf(slot.peer, sizeof(slot.peer), "MODEM,2323");
    });
    ASSERT_NE(Card(), nullptr);
    NetworkManager::Change change;
    std::string error;
    ASSERT_TRUE(NetworkManager::ParseChange({{"forwards", "tcp:23924:2323,tcp:8080:80"}}, change, error)) << error;
    ASSERT_TRUE(Network()->RequestChange(change, error)) << error;
    Network()->OnFrame();
    const StateNode net = DeviceState::Network(_context);
    const StateNode* gateway = net.find("ethernet_gateway");
    ASSERT_NE(gateway, nullptr);
    const StateNode* ports = gateway->find("forwarded_guest_ports");
    ASSERT_NE(ports, nullptr);
    ASSERT_EQ(ports->items.size(), 1u) << DeviceState::ToText(*gateway);
    EXPECT_EQ(ports->items[0].i, 80) << "2323 is the modem's";
    ASSERT_NE(Card()->Modem(), nullptr);
    EXPECT_EQ(Card()->Modem()->ListenPort(), 2323);
    const StateNode* servers = net.find("virtual_network")->find("guest_servers");
    ASSERT_NE(servers, nullptr);
    for (const StateNode& s : servers->items)
    {
        if (s.find("guest_port")->i == 2323)
            EXPECT_EQ(s.find("waiting_sockets")->i, 1) << "only the modem waits on 2323";
    }
}
