// ZiFi (zifi.h): the TS-Labs AVR firmware's ZiFi API on #xxEF beside its 16550 - the FPGA's high-byte decode, the
// API switch, the data-register selector, the counts, the rings, the interrupt, the line to the ESP, TTD.
// docs/inprogress/2026-10-02-tsconf-zifi/reference-zifi.md

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "debugger/ttd/network/ttdserialport.h"
#include "debugger/ttd/network/ttdzifi.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/zifi.h"
#include "emulator/io/serial/esp/atmodule.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/esp/zifinativemodule.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

class ZiFi_Test : public ::testing::Test
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

    void Create(const char* model = "TSL", int ram = 4096)
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("zifi", model, ram, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
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

    /// A TS-Conf with the ZiFi API on and ZiFi selected for the data register
    void CreateWithApi(const char* zifiPeer = "loopback", const char* comPeer = "none")
    {
        Create();
        Configure({{"zifi", zifiPeer}, {"com_port", comPeer}});
        ASSERT_NE(_context->pZiFi, nullptr);
        Out(0xC7EF, 0xF1);   // SETAPI 1
        ASSERT_EQ(In(0xC7EF), 0x00);
    }

    ZiFi& Block() { return *_context->pZiFi; }
};

TEST_F(ZiFi_Test, TheFpgaPacksTheHighByte)
{
    Create();
    const PortDecoder::NetworkCapabilities caps = _context->pPortDecoder->DescribeNetwork();
    ASSERT_TRUE(caps.zifi);
    ASSERT_TRUE(caps.serialRegister);
    for (unsigned high = 0; high < 0x100; ++high)
    {
        const int reg = caps.serialRegister(static_cast<uint16_t>(high << 8 | 0xEF));
        if (high < 0xC0 || (high >= 0xF0 && high < 0xF8))
            EXPECT_EQ(reg, ComPortRegister::kDataRegion) << std::hex << high;
        else if (high >= 0xF8)
            EXPECT_EQ(reg, static_cast<int>(high - 0xF8)) << std::hex << high;
        else
            EXPECT_EQ(reg, ComPortRegister::kZiFiBase + static_cast<int>(high & 0x0F))
                << std::hex << high << ": #D0..#EF alias #C0..#CF";
    }
}

TEST_F(ZiFi_Test, TheAvrIsThereWithoutABoard)
{
    // The 16550 and the API block are the AVR's: present with ComPort=NONE and ZiFi=NONE
    Create();
    ASSERT_NE(_context->pComPort, nullptr);
    ASSERT_NE(_context->pZiFi, nullptr);
    Out(0xFFEF, 0x5A);   // SCR
    EXPECT_EQ(In(0xFFEF), 0x5A);
    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_EQ(st.serialPort, "zifi");
    EXPECT_TRUE(st.zifiMachine);
    EXPECT_TRUE(st.zifi.fitted);
    EXPECT_EQ(st.settings.zifi, "NONE");
}

TEST_F(ZiFi_Test, WithTheApiOffEverythingReadsFF)
{
    Create();
    Configure({{"zifi", "loopback"}});
    for (uint16_t port : {0x00EF, 0xBFEF, 0xC0EF, 0xC1EF, 0xC4EF, 0xC5EF, 0xC7EF, 0xC9EF, 0xCFEF, 0xF3EF})
        EXPECT_EQ(In(port), 0xFF) << std::hex << port;
    EXPECT_FALSE(Block().GetView().selectZf) << "a ZIFR read with the API off does not select ZiFi";
    Out(0xC7EF, 0x03);   // CLRFIFO before SETAPI: ignored (the FT812 SDK's zifi_init does this)
    Out(0x00EF, 0x41);   // dropped
    EXPECT_EQ(Block().GetView().zfTx + Block().GetView().rsTx, 0);
    Out(0xC5EF, 0x10);   // the threshold takes the write anyway
    EXPECT_EQ(Block().GetView().zibtr, 0x10);
}

TEST_F(ZiFi_Test, SetApiAndGetVer)
{
    Create();
    Out(0xC7EF, 0xF1);
    EXPECT_EQ(In(0xC7EF), 0x00) << "SETAPI: ER = OK";
    Out(0xC7EF, 0xFF);
    EXPECT_EQ(In(0xC7EF), 0x01) << "GETVER: version 1";
    Out(0xC7EF, 0x55);
    EXPECT_EQ(In(0xC7EF), 0x01) << "an unknown command leaves ER (no REJ)";
    EXPECT_EQ(In(0xC5EF), 0x80) << "ZIBTR reset value";
    EXPECT_EQ(In(0xC6EF), 0x01) << "ZITOR reset value";
    EXPECT_EQ(In(0xCAEF), 0xFF);
    EXPECT_EQ(In(0xD7EF), 0x01) << "#D7EF aliases ER";
    Out(0xC7EF, 0xF2);   // a version that does not exist turns the API off
    EXPECT_EQ(Block().GetView().api, 0);
    EXPECT_EQ(In(0xC7EF), 0xFF);
}

TEST_F(ZiFi_Test, TheSelectorPicksTheRing)
{
    CreateWithApi();
    // Bytes taken: in the ring, on the line or sent (each access's AVR wait lets the line move on)
    auto taken = [](const Uart16550& uart) {
        const Uart16550::View v = uart.GetView();
        return v.txCount + (v.txBusy ? 1u : 0u) + v.bytesOut;
    };
    Uart16550& rs = _context->pComPort->Uart();
    Uart16550& line = Block().Line().Uart();
    Out(0x00EF, 0x31);   // after reset: the enhanced RS-232 TX ring (the 16550's)
    EXPECT_EQ(taken(rs), 1u);
    EXPECT_EQ(In(0xC1EF), 0xBF) << "ZOFR: 255 free, capped";
    Out(0x00EF, 0x32);
    Out(0x7FEF, 0x33);
    EXPECT_EQ(taken(rs), 1u);
    EXPECT_EQ(taken(line), 2u) << "ZiFi took them";
    In(0xC2EF);   // RIFR: back to RS-232
    Out(0x00EF, 0x34);
    EXPECT_EQ(taken(rs), 2u);
}

TEST_F(ZiFi_Test, CountsCapAndInirDrainsExactly)
{
    CreateWithApi();
    ASSERT_EQ(In(0xC1EF), 0xBF);
    for (int i = 0; i < 200; ++i)
        Out(0x00EF, static_cast<uint8_t>(i));
    const uint8_t free = In(0xC1EF);
    EXPECT_GE(free, 55);
    EXPECT_LT(free, 0xBF);
    _emulator->RunNFrames(4);   // 200 bytes at 115200 8N2 out, 8N1 back: about 19 ms
    EXPECT_EQ(Block().GetView().zfRx, 200);
    const uint8_t count = In(0xC0EF);
    EXPECT_EQ(count, 0xBF) << "ZIFR caps at #BF so INIR stays in the data area";
    // INIR from #BFEF: B counts down, every high byte is the data register
    for (uint16_t b = count; b > 0; --b)
        EXPECT_EQ(In(static_cast<uint16_t>(b << 8 | 0xEF)), static_cast<uint8_t>(count - b));
    EXPECT_EQ(In(0xC0EF), 200 - 0xBF);
    while (In(0xC0EF))
        In(0x00EF);
    EXPECT_EQ(In(0x00EF), 0xFF) << "an empty ring reads #FF";
}

TEST_F(ZiFi_Test, TheRingsDropWhenFull)
{
    CreateWithApi();
    In(0xC1EF);
    for (int burst = 0; burst < 3; ++burst)
    {
        for (int i = 0; i < 200; ++i)
            Out(0x00EF, 0x55);
        _emulator->RunNFrames(4);
    }
    EXPECT_EQ(Block().GetView().zfRx, 511) << "511 usable; the rest dropped without a flag";
    for (int i = 0; i < 300; ++i)
        Out(0x00EF, 0x66);
    EXPECT_LE(Block().GetView().zfTx, 255);
}

TEST_F(ZiFi_Test, TheEnhancedDataRegisterSharesThe16550Rings)
{
    CreateWithApi("none", "loopback");
    Out(0xFBEF, 0x03);   // LCR 8N1: divisor 1 = 115200 at reset
    Out(0xFCEF, 0x02);   // MCR: RTS
    Out(0xF8EF, 0x4A);   // THR
    _emulator->RunNFrames(2);
    EXPECT_EQ(In(0xC2EF), 1) << "RIFR";
    EXPECT_EQ(In(0x00EF), 0x4A) << "read through the data register";
    EXPECT_EQ(In(0xFDEF) & 0x01, 0) << "gone from the 16550 too (LSR.DR clear)";
}

TEST_F(ZiFi_Test, TheWaitPortInterrupt)
{
    CreateWithApi();
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    TsConfState& ts = decoder->GetState();
    ts.regs[TsConfReg::IntMask] = TsConfInt::WaitPort;
    ts.intPending = 0;

    Out(0xC5EF, 4);      // ZIBTR
    Out(0xC4EF, ZiFi::kZfIbt);
    In(0xC1EF);
    // (the loopback plug echoes at the line's next advance: two frames per round trip)
    for (int i = 0; i < 3; ++i)
        Out(0x00EF, 0x20);
    _emulator->RunNFrames(2);
    ASSERT_EQ(Block().GetView().zfRx, 3);
    EXPECT_EQ(Block().GetView().isr, 0) << "3 bytes: below the threshold";
    Out(0x00EF, 0x21);
    _emulator->RunNFrames(2);
    EXPECT_EQ(Block().GetView().isr, ZiFi::kZfIbt);
    EXPECT_EQ(Block().GetView().imr & ZiFi::kZfIbt, 0) << "one-shot: the bit left IMR";
    EXPECT_NE(ts.intPending & TsConfInt::WaitPort, 0) << "vector #F9 pending";
    EXPECT_EQ(In(0xC4EF), ZiFi::kZfIbt);
    EXPECT_EQ(In(0xC4EF), 0) << "ISR clears on read";

    // The timeout: a non-empty ring and ZITOR ms of silence
    Out(0xC6EF, 5);
    Out(0xC4EF, ZiFi::kZfIto);
    _emulator->RunNFrames(1);
    EXPECT_EQ(Block().GetView().isr, ZiFi::kZfIto);

    // Masked: the AVR strobes, the FPGA latches nothing
    In(0xC4EF);
    ts.regs[TsConfReg::IntMask] = 0;
    ts.intPending = 0;
    Out(0xC4EF, ZiFi::kZfIto);
    _emulator->RunNFrames(1);
    EXPECT_EQ(ts.intPending & TsConfInt::WaitPort, 0);
}

TEST_F(ZiFi_Test, TheLineRunsAt115200)
{
    // 255 bytes out at 8N2 take 24.3 ms: one frame (20.5 ms) is not enough, as on the wire
    CreateWithApi();
    In(0xC1EF);
    for (int i = 0; i < 255; ++i)
        Out(0x00EF, static_cast<uint8_t>(i));
    _emulator->RunNFrames(1);
    const uint16_t first = Block().GetView().zfRx;
    EXPECT_GT(first, 150);
    EXPECT_LT(first, 255);
    _emulator->RunNFrames(2);
    EXPECT_EQ(Block().GetView().zfRx, 255);
    EXPECT_EQ(Block().Line().Uart().Baud(), 115200u);
}

TEST_F(ZiFi_Test, EveryAccessWaitsForTheAvr)
{
    CreateWithApi();
    Z80* z80 = _context->pCore->GetZ80();
    const uint32_t t = z80->t;
    In(0xC0EF);
    EXPECT_GT(z80->t, t);
}

TEST_F(ZiFi_Test, AZ80ResetKeepsTheAvr)
{
    CreateWithApi();
    Out(0xC5EF, 0x22);
    _context->pPortDecoder->reset();
    _context->pCore->GetNetworkManager()->Reset();
    EXPECT_EQ(In(0xC7EF), 0x00) << "the API stays on: rs232_init runs only when the AVR starts";
    EXPECT_EQ(In(0xC5EF), 0x22);
}

TEST_F(ZiFi_Test, TtdRestoresTheBlockAndTheLine)
{
    CreateWithApi();
    Out(0xC9EF, 0x07);
    In(0xC1EF);
    for (int i = 0; i < 5; ++i)
        Out(0x00EF, static_cast<uint8_t>(0x60 + i));
    _emulator->RunNFrames(2);

    ttd::TTDZiFi registers(_context);
    ttd::TTDSerialPort line(_context, [this]() { return &_context->pZiFi->Line(); }, ttd::PeripheralId::ZiFiLine,
                            "ZiFiLine");
    std::vector<uint8_t> a(registers.TTDStateSize()), b(line.TTDStateSize());
    registers.TTDSaveState(a.data());
    line.TTDSaveState(b.data());

    In(0xC2EF);
    Out(0xC9EF, 0x01);
    while (Block().GetView().zfRx)
        Block().Line().Uart().DataRead(Block().Line().Now());
    registers.TTDLoadState(a.data());
    line.TTDLoadState(b.data());
    EXPECT_TRUE(Block().GetView().selectZf);
    EXPECT_EQ(In(0xC9EF), 0x07);
    ASSERT_EQ(In(0xC0EF), 5);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(In(0x00EF), 0x60 + i);
}

TEST_F(ZiFi_Test, TheOriginalBoardAnswersAt)
{
    // ZiFi=AT: an ESP-01 with Espressif's AT firmware at 115200; zifi.spg polls ZIFR and reads with INIR
    CreateWithApi("at");
    auto* module = dynamic_cast<EspModule*>(Block().Line().Peer());
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->Baud(), 115200u);
    In(0xC1EF);
    for (char c : std::string("AT\r\n"))
        Out(0x00EF, static_cast<uint8_t>(c));
    std::string reply;
    for (int frame = 0; frame < 30 && reply.find("OK") == std::string::npos; ++frame)
    {
        _emulator->RunNFrames(1);
        for (uint8_t n = In(0xC0EF); n; --n)
            reply.push_back(static_cast<char>(In(0x00EF)));
    }
    EXPECT_NE(reply.find("OK"), std::string::npos) << "reply: " << reply;
    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_EQ(st.zifi.peer, module->Kind());
    EXPECT_EQ(st.settings.zifi, "AT");
}

TEST_F(ZiFi_Test, ZxEvoWithTheTsFirmwareHasItWithoutTheInterrupt)
{
    Create("ATM3", 4096);
    Configure({{"avr_firmware", "ts2016-04"}, {"zifi", "loopback"}});
    ASSERT_NE(_context->pZiFi, nullptr);
    EXPECT_FALSE(_context->pPortDecoder->DescribeNetwork().waitPortInterrupt) << "the BaseConf FPGA has no wait-port INT";
}

TEST_F(ZiFi_Test, OtherMachinesSayWhy)
{
    Create("ATM3", 4096);
    Configure({{"zifi", "at"}});
    EXPECT_EQ(_context->pZiFi, nullptr) << "the NedoPC firmware has no ZiFi";
    const NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    bool noted = false;
    for (const std::string& note : st.notes)
        noted = noted || note.find("ZiFi") != std::string::npos;
    EXPECT_TRUE(noted);
}

TEST_F(ZiFi_Test, TheBoardsEsp01RunsAnEsp8266Build)
{
    // The ZiFi board carries an ESP-01: NonOS AT 1.7.4 unless EspChip or the spec names another ESP8266 build;
    // its 1 MB flash has no OTA
    CreateWithApi("at");
    auto* at = dynamic_cast<AtModule*>(Block().Line().Peer());
    ASSERT_NE(at, nullptr);
    EXPECT_EQ(at->GetFirmware(), EspModule::Firmware::Esp8266NonOs174) << "EspChip is ESP32 by default: not an ESP-01's";
    EXPECT_EQ(at->GetFlash(), atdialect::Flash::OneMb);
    Configure({{"zifi", "at,esp8266-at222"}});
    at = dynamic_cast<AtModule*>(Block().Line().Peer());
    ASSERT_NE(at, nullptr);
    EXPECT_EQ(at->GetFirmware(), EspModule::Firmware::Esp8266At222);
    NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_EQ(st.settings.zifi, "AT,ESP8266-AT222");
    ASSERT_TRUE(st.zifi.esp.isObject());
    EXPECT_EQ(st.zifi.esp["firmware"].s, "ESP8266-AT222");
}

TEST_F(ZiFi_Test, TheNativeModuleAnswersThroughTheRegisters)
{
    // ZiFi=ZIFI-NATIVE: the new zifi.spg's ZiFi_PutChar / ZiFi_ReadBurst path (ZOFR, DR, ZIFR, INIR)
    CreateWithApi("zifi-native");
    ASSERT_NE(dynamic_cast<ZiFiNativeModule*>(Block().Line().Peer()), nullptr);
    In(0xC1EF);
    for (uint8_t b : ZiFiNativeModule::Frame(ZiFiNativeModule::kPing))
        Out(0xBFEF, b);
    std::vector<uint8_t> reply;
    for (int frame = 0; frame < 10 && reply.size() < 5; ++frame)
    {
        _emulator->RunNFrames(1);
        for (uint8_t n = In(0xC0EF); n; --n)
            reply.push_back(In(0xBFEF));
    }
    EXPECT_EQ(reply, ZiFiNativeModule::Frame(ZiFiNativeModule::kReady));
    NetworkManager::Status st = _context->pCore->GetNetworkManager()->GetStatus();
    EXPECT_EQ(st.zifi.peer, "zifi-native");
    ASSERT_TRUE(st.zifi.esp.isObject());
    EXPECT_EQ(st.zifi.esp["firmware"].s, "ZIFI-NATIVE S3 (s3-native-0.6.94)");
    EXPECT_EQ(st.zifi.esp["native_session"]["last_step"].s, "#04");
}

TEST_F(ZiFi_Test, TtdKeepsTheNativeModule)
{
    CreateWithApi("zifi-native,esp01s");
    In(0xC1EF);
    for (uint8_t b : ZiFiNativeModule::Frame(ZiFiNativeModule::kEcho, {'z'}))
        Out(0xBFEF, b);
    _emulator->RunNFrames(2);
    ttd::TTDSerialPort line(_context, [this]() { return &_context->pZiFi->Line(); }, ttd::PeripheralId::ZiFiLine,
                            "ZiFiLine");
    std::vector<uint8_t> a(line.TTDStateSize()), b(line.TTDStateSize());
    line.TTDSaveState(a.data());
    line.TTDLoadState(a.data());
    line.TTDSaveState(b.data());
    EXPECT_EQ(a, b) << "the line and the module (peer kind 7) round-trip";
    auto* native = dynamic_cast<ZiFiNativeModule*>(Block().Line().Peer());
    ASSERT_NE(native, nullptr);
    EXPECT_EQ(native->LastStep(), ZiFiNativeModule::kEcho);
}
