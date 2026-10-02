// ATM2IOESP on the ATM Turbo 2+ INTERNAL I/O connector (atm2ioesp.h): the #FB bus address latch, the #FA data
// strobes, the card's TL16C550C and its peer. docs/inprogress/2026-10-02-atm2ioesp/reference-atm2ioesp.md

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "debugger/ttd/atm/ttdatmiobus.h"
#include "debugger/ttd/network/ttdserialport.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/atm2kbc.h"
#include "emulator/io/network/atm2ioesp.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/ports/models/portdecoder_atm710.h"

class Atm2IoEsp_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_ATM710* _decoder = nullptr;

    void SetUp() override { _manager = EmulatorManager::GetInstance(); }

    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    void Create(const char* model = "ATM710", int ram = 1024)
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("atm2ioesp", model, ram, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_ATM710*>(_context->pPortDecoder);
    }

    void Configure(const std::vector<std::pair<std::string, std::string>>& settings)
    {
        NetworkManager::Change change;
        std::string error;
        ASSERT_TRUE(NetworkManager::ParseChange(settings, change, error)) << error;
        ASSERT_TRUE(_context->pCore->GetNetworkManager()->RequestChange(change, error)) << error;
    }

    void Out(uint16_t port, uint8_t value) { _context->pCore->GetZ80()->out(port, value); }
    uint8_t In(uint16_t port) { return _context->pCore->GetZ80()->in(port); }

    /// A 16550 register through the bus: the address on #FB, the data on #FA
    void WriteRegister(uint8_t address, uint8_t value)
    {
        Out(0x00FB, address);
        Out(0x00FA, value);
    }
    uint8_t ReadRegister(uint8_t address)
    {
        Out(0x00FB, address);
        return In(0x00FA);
    }
};

TEST_F(Atm2IoEsp_Test, FaIsNotTheKeyboardPort)
{
    // ATM 7.10: #FE is A2..A0 = 110, #FA (010) is the INTERNAL I/O data port - with no card the bus floats
    Create();
    _emulator->RunNFrames(9);
    Atm2Kbc* kbc = _decoder->GetKeyboardController();
    ASSERT_NE(kbc, nullptr);
    const uint64_t reads = kbc->Reads();
    const uint32_t t = _context->pCore->GetZ80()->t;
    EXPECT_EQ(In(0xF7FA), 0xFF);
    EXPECT_EQ(kbc->Reads(), reads) << "no keyboard controller command, no wait";
    EXPECT_EQ(_context->pCore->GetZ80()->t, t);

    const uint8_t border = _context->emulatorState.border_attr;
    Out(0x00FA, 0x05);
    EXPECT_EQ(_context->emulatorState.border_attr, border) << "OUT (#FA) is no border write";
    EXPECT_EQ(In(0x7FFE) | 0x00, In(0x7FFE)) << "#FE still reads";
    EXPECT_GT(kbc->Reads(), reads);
}

TEST_F(Atm2IoEsp_Test, FbLatchesTheBusAddress)
{
    Create();
    Out(0x00FB, 0xF3);
    EXPECT_EQ(_decoder->IoBusAddress(), 0xF3);
    Out(0x127B, 0x42);   // any port with A2..A0 = 011: #7B too (A7 = 0 also strobes the printer)
    EXPECT_EQ(_decoder->IoBusAddress(), 0x42);
    EXPECT_EQ(In(0x00FB), 0xFF) << "IN (#FB) is the printer status (BUSY pulled up), not the latch";
}

TEST_F(Atm2IoEsp_Test, TheCardAnswersItsAddresses)
{
    Create();
    Configure({{"card", "atm2ioesp"}, {"atm2ioesp", "loopback"}});
    ASSERT_NE(_context->pAtm2IoEsp, nullptr);
    EXPECT_EQ(_context->pAtm2IoEsp->Address(), 0xF0);

    WriteRegister(0xF7, 0x5A);   // SCR
    EXPECT_EQ(ReadRegister(0xF7), 0x5A);
    Out(0x00FB, 0xF7);
    EXPECT_EQ(In(0x00FA), 0x5A) << "the latch holds: a second read reaches the same register";
    EXPECT_EQ(ReadRegister(0xFF), 0xFF) << "#F8..#FF: Rev 1.0's addresses, nothing answers";
    EXPECT_EQ(ReadRegister(0x07), 0xFF);

    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_TRUE(st.internalIo);
    EXPECT_TRUE(st.atm2IoEsp.fitted);
    EXPECT_EQ(st.atm2IoEspAddress, 0xF0u);
    EXPECT_EQ(st.cards, "ATM2IOESP");
}

TEST_F(Atm2IoEsp_Test, Rev10SitsAtF8)
{
    Create();
    Configure({{"card", "atm2ioesp"}, {"atm2ioesp", "loopback"}, {"atm2ioesp_address", "0xF8"}});
    WriteRegister(0xFF, 0x33);
    EXPECT_EQ(ReadRegister(0xFF), 0x33);
    EXPECT_EQ(ReadRegister(0xF7), 0xFF);
}

TEST_F(Atm2IoEsp_Test, Divisor1Is115200)
{
    Create();
    Configure({{"card", "atm2ioesp"}, {"atm2ioesp", "loopback"}});
    WriteRegister(0xF3, 0x83);   // LCR: DLAB, 8N1
    WriteRegister(0xF0, 0x01);   // DLL
    WriteRegister(0xF1, 0x00);   // DLM
    WriteRegister(0xF3, 0x03);
    EXPECT_EQ(_context->pAtm2IoEsp->Com().Uart().Baud(), 115200u) << "1.8432 MHz / 16";
}

TEST_F(Atm2IoEsp_Test, ALoopbackByteComesBackWithAnRtsPulse)
{
    Create();
    Configure({{"card", "atm2ioesp"}, {"atm2ioesp", "loopback"}});
    WriteRegister(0xF3, 0x83);
    WriteRegister(0xF0, 0x01);
    WriteRegister(0xF1, 0x00);
    WriteRegister(0xF3, 0x03);
    WriteRegister(0xF2, 0x87);   // FCR: FIFOs on and cleared
    WriteRegister(0xF4, 0x02);   // MCR: RTS (AFE off, as NedoOS)
    WriteRegister(0xF0, 0x6B);   // THR
    _emulator->RunNFrames(2);    // out at the first frame end, the echo back by the next
    EXPECT_NE(ReadRegister(0xF5) & 0x01, 0) << "LSR.DR";
    EXPECT_EQ(ReadRegister(0xF0), 0x6B);
}

TEST_F(Atm2IoEsp_Test, ResetClearsTheUartNotThePeer)
{
    Create();
    Configure({{"card", "atm2ioesp"}, {"atm2ioesp", "loopback"}});
    WriteRegister(0xF3, 0x1B);
    ISerialPeer* peer = _context->pAtm2IoEsp->Com().Peer();
    _context->pPortDecoder->reset();
    EXPECT_EQ(ReadRegister(0xF3), 0x00) << "the connector's RS resets the 16550";
    ASSERT_NE(_context->pAtm2IoEsp, nullptr);
    EXPECT_EQ(_context->pAtm2IoEsp->Com().Peer(), peer);
}

TEST_F(Atm2IoEsp_Test, TtdKeepsTheLatchAndTheCard)
{
    Create();
    Configure({{"card", "atm2ioesp"}, {"atm2ioesp", "loopback"}});
    WriteRegister(0xF7, 0x99);
    Out(0x00FB, 0xF3);

    ttd::TTDAtmIoBus bus(_context);
    ttd::TTDSerialPort card(
        _context, [this]() { return _context->pAtm2IoEsp ? &_context->pAtm2IoEsp->Com() : nullptr; },
        ttd::PeripheralId::Atm2IoEsp, "Atm2IoEsp");
    std::vector<uint8_t> busBlob(bus.TTDStateSize()), cardBlob(card.TTDStateSize());
    bus.TTDSaveState(busBlob.data());
    card.TTDSaveState(cardBlob.data());

    WriteRegister(0xF7, 0x11);
    Out(0x00FB, 0x00);
    bus.TTDLoadState(busBlob.data());
    card.TTDLoadState(cardBlob.data());
    EXPECT_EQ(_decoder->IoBusAddress(), 0xF3);
    EXPECT_EQ(ReadRegister(0xF7), 0x99);
}

TEST_F(Atm2IoEsp_Test, OnlyTheAtmTurbo2HasTheConnector)
{
    Create("PENTAGON", 128);
    Configure({{"card", "atm2ioesp"}});
    EXPECT_EQ(_context->pAtm2IoEsp, nullptr);
    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    ASSERT_FALSE(st.notes.empty());
    EXPECT_NE(st.notes.front().find("INTERNAL I/O"), std::string::npos) << st.notes.front();
}

TEST_F(Atm2IoEsp_Test, ItsEspAnswersAt115200)
{
    // The shipped card: an ESP32 with the AT firmware at 115200, RTS / CTS; NedoOS pulses RTS
    Create();
    Configure({{"card", "atm2ioesp"}, {"atm2ioesp", "at"}});
    auto* module = dynamic_cast<EspModule*>(_context->pAtm2IoEsp->Com().Peer());
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->Baud(), 115200u);
    WriteRegister(0xF3, 0x83);
    WriteRegister(0xF0, 0x01);
    WriteRegister(0xF1, 0x00);
    WriteRegister(0xF3, 0x03);
    WriteRegister(0xF2, 0x87);
    for (char c : std::string("AT\r\n"))
        WriteRegister(0xF0, static_cast<uint8_t>(c));

    // As NedoOS's esp_fill3: a short RTS pulse (MCR 2, then 0), then read what arrived; the program's own
    // time between polls advances the clock
    std::string reply;
    Z80* z80 = _context->pCore->GetZ80();
    for (int frame = 0; frame < 30 && reply.find("OK") == std::string::npos; ++frame)
    {
        _emulator->RunNFrames(1);
        for (int poll = 0; poll < 150; ++poll)
        {
            WriteRegister(0xF4, 0x02);
            WriteRegister(0xF4, 0x00);
            z80->t += 300;   // the loop's own instructions: about one character time at 115200
            while (ReadRegister(0xF5) & 0x01)
                reply.push_back(static_cast<char>(ReadRegister(0xF0)));
        }
    }
    EXPECT_NE(reply.find("OK"), std::string::npos) << "reply: " << reply;
    EXPECT_EQ(_context->pAtm2IoEsp->Com().Uart().GetView().overruns, 0u);
}

TEST_F(Atm2IoEsp_Test, OnlyCtsIsWired)
{
    // DCD' and DSR' tied asserted, RI' tied off: a TCP peer's lines do not reach the card's MSR
    Create();
    Configure({{"card", "atm2ioesp"}, {"atm2ioesp", "loopback"}});
    const uint8_t msr = ReadRegister(0xF6);
    EXPECT_EQ(msr & 0xA0, 0xA0) << "DCD and DSR asserted";
    EXPECT_EQ(msr & 0x40, 0x00) << "RI never";
    EXPECT_EQ(msr & 0x10, 0x10) << "CTS: the loopback plug is ready";
}
