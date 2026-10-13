// The Next's UARTs (docs/inprogress/2026-10-07-zx-next: D1). Source: zxnext.vhd, serial/uart.vhd, uart_rx.vhd, uart_tx.vhd, fifop.vhd

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/serial/esp/espmodule.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/z80n/nextinterrupts.h"
#include "emulator/io/z80n/nextuart.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"

namespace
{
constexpr uint16_t kTx = NextUart::kPortTx, kRx = NextUart::kPortRx, kSelect = NextUart::kPortSelect, kFrame = NextUart::kPortFrame;
constexpr uint64_t kFrame8N1 = 10 * NextUart::kDefaultPrescaler;  // start + 8 + stop bits
constexpr uint8_t kSelectPi = 0x40, kPrescalerFollows = 0x10;

/// A peer for tests: records what the ZX sent and has bytes of its own to give
class ScriptedPeer final : public ISerialPeer
{
public:
    std::vector<uint8_t> sent;
    std::deque<uint8_t> toZx;
    bool cts = true;
    void Transmit(uint8_t byte) override { sent.push_back(byte); }
    bool HasByte() const override { return !toZx.empty(); }
    uint8_t TakeByte() override
    {
        const uint8_t b = toZx.front();
        toZx.pop_front();
        return b;
    }
    bool Cts() const override { return cts; }
    void OnLineSettings(const SerialLine& line) override { settings = line; }
    void OnModemLines(bool rts, bool) override { rtsFromZx = rts; }
    bool BreakHeld() const override { return breakHeld; }
    void OnBreak(bool held) override { breakFromZx = held; }
    const char* Kind() const override { return "scripted"; }
    size_t Pending() const override { return toZx.size(); }
    SerialLine settings;
    bool rtsFromZx = true;
    bool breakHeld = false;     ///< the peer holds its line low
    bool breakFromZx = false;   ///< the ZX holds its Tx low
};
}  // namespace

TEST(NextUart_Test, TheBlockIsDecodedByTheVhdlsPortRule)
{
    for (uint16_t port : {0x133B, 0x143B, 0x153B, 0x163B})
        EXPECT_TRUE(NextUart::Decodes(port)) << std::hex << port;
    for (uint16_t port : {0x123B, 0x173B, 0x103B, 0x113B, 0x183B, 0x133A, 0x1B3B, 0x0F3B})
        EXPECT_FALSE(NextUart::Decodes(port)) << std::hex << port;
}

TEST(NextUart_Test, ResetValuesAndTheSelectRegister)
{
    NextUart uart;
    EXPECT_EQ(uart.Read(kFrame, 0), NextUart::kDefaultFrame);
    EXPECT_EQ(uart.Read(kSelect, 0), 0x00);
    uart.Write(kSelect, kSelectPi | kPrescalerFollows | 0x05, 0);
    EXPECT_EQ(uart.Read(kSelect, 0), 0x45) << "UART 1 selected, its prescaler's top bits 5";
    EXPECT_EQ(uart.GetView(1).prescaler, (5u << 14) | NextUart::kDefaultPrescaler);
    EXPECT_EQ(uart.GetView(0).prescaler, NextUart::kDefaultPrescaler) << "UART 0 untouched";
    uart.Write(kSelect, 0, 0);
    EXPECT_EQ(uart.Read(kSelect, 0), 0x00);
    // the prescaler's low half: bit 7 picks which seven bits
    uart.Write(kRx, 0x12, 0);
    uart.Write(kRx, 0x80 | 0x03, 0);
    EXPECT_EQ(uart.GetView(0).prescaler & 0x3FFF, (0x03u << 7) | 0x12u);
}

TEST(NextUart_Test, ABytePutInTheTxFifoLeavesAfterItsFrameTime)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    uart.Write(kTx, 0xA5, 100);
    EXPECT_EQ(uart.Read(kTx, 101) & NextUart::kStatusTxEmpty, 0) << "the shifter is busy";
    uart.Advance(100 + kFrame8N1 - 1);
    EXPECT_TRUE(peer.sent.empty());
    uart.Advance(100 + kFrame8N1);
    ASSERT_EQ(peer.sent.size(), 1u);
    EXPECT_EQ(peer.sent[0], 0xA5);
    EXPECT_NE(uart.Read(kTx, 100 + kFrame8N1) & NextUart::kStatusTxEmpty, 0);
    // two more: back to back
    uart.Write(kTx, 1, 5000);
    uart.Write(kTx, 2, 5000);
    uart.Advance(5000 + 2 * kFrame8N1);
    ASSERT_EQ(peer.sent.size(), 3u);
    EXPECT_EQ(peer.sent[2], 2);
}

TEST(NextUart_Test, TheFrameFormatSetsTheFrameTimeAndTheLineSettings)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    // 7 data bits (10), parity (bit 2), odd (bit 1), two stops (bit 0): 1 + 7 + 1 + 2 = 11 bits
    uart.Write(kFrame, 0x10 | 0x04 | 0x02 | 0x01, 0);
    EXPECT_EQ(uart.FrameClocks(0), 11u * NextUart::kDefaultPrescaler);
    EXPECT_EQ(peer.settings.dataBits, 7);
    EXPECT_EQ(peer.settings.parity, 'O');
    EXPECT_EQ(peer.settings.stopBits, 2);
    EXPECT_EQ(peer.settings.baud, 115200u) << "28 MHz / 243 = 115226 is told as the standard rate";
}

TEST(NextUart_Test, ReceivedBytesFillTheFifoAtTheLineRateAndAReadTakesThem)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    peer.toZx = {0x11, 0x22, 0x33};
    uart.Advance(1000);
    EXPECT_EQ(uart.GetView(0).rxCount, 0u) << "the first frame is still on the line";
    uart.Advance(1000 + 3 * kFrame8N1);
    EXPECT_EQ(uart.GetView(0).rxCount, 3u);
    EXPECT_NE(uart.Read(kTx, 1000 + 3 * kFrame8N1) & NextUart::kStatusRxAvail, 0);
    EXPECT_EQ(uart.Read(kRx, 100000), 0x11);
    EXPECT_EQ(uart.Read(kRx, 100000), 0x22);
    EXPECT_EQ(uart.Read(kRx, 100000), 0x33);
    EXPECT_EQ(uart.Read(kRx, 100000), 0x00) << "empty reads 0";
    EXPECT_EQ(uart.Read(kTx, 100000) & NextUart::kStatusRxAvail, 0);
}

TEST(NextUart_Test, TheRxFifoHoldsFiveHundredAndTwelveAndFlagsAnOverflow)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    for (unsigned i = 0; i < NextUart::kRxFifo + 3; i++)
        peer.toZx.push_back(static_cast<uint8_t>(i));
    uart.Advance(1);
    uart.Advance(1 + (NextUart::kRxFifo + 10) * kFrame8N1);
    const NextUart::View v = uart.GetView(0);
    EXPECT_EQ(v.rxCount, NextUart::kRxFifo);
    EXPECT_NE(v.status & NextUart::kStatusOverflow, 0);
    EXPECT_NE(v.status & NextUart::kStatusNearFull, 0);
    // the status read clears the overflow
    const uint8_t first = uart.Read(kTx, 1 + (NextUart::kRxFifo + 10) * kFrame8N1);
    EXPECT_NE(first & NextUart::kStatusOverflow, 0);
    EXPECT_EQ(uart.Read(kTx, 1 + (NextUart::kRxFifo + 10) * kFrame8N1) & NextUart::kStatusOverflow, 0);
    // the bytes stored after an error carry the flag; the first 512 were stored clean
    EXPECT_EQ(uart.Read(kRx, 0), 0x00);
}

TEST(NextUart_Test, TheTxFifoHoldsSixtyFourBytes)
{
    NextUart uart;
    for (unsigned i = 0; i < NextUart::kTxFifo + 5; i++)
        uart.Write(kTx, static_cast<uint8_t>(i), 0);
    const NextUart::View v = uart.GetView(0);
    EXPECT_EQ(v.txCount, NextUart::kTxFifo) << "one byte went to the shifter, then the FIFO filled and the rest were dropped";
    EXPECT_TRUE(v.txBusy);
    EXPECT_NE(uart.Read(kTx, 0) & NextUart::kStatusTxFull, 0);
}

TEST(NextUart_Test, TheFrameResetBitEmptiesTheFifosAndHoldsThem)
{
    NextUart uart;
    uart.Write(kTx, 1, 0);
    uart.Write(kTx, 2, 0);
    uart.Write(kFrame, NextUart::kDefaultFrame | NextUart::kFrameReset, 0);
    EXPECT_EQ(uart.GetView(0).txCount, 0u);
    EXPECT_FALSE(uart.GetView(0).txBusy);
    uart.Write(kTx, 3, 0);
    EXPECT_EQ(uart.GetView(0).txCount, 0u) << "held in reset: a write goes nowhere";
    uart.Write(kFrame, NextUart::kDefaultFrame, 0);
    uart.Write(kTx, 4, 0);
    EXPECT_TRUE(uart.GetView(0).txBusy);
}

TEST(NextUart_Test, HardwareFlowControlWaitsForClearToSendAndDropsRts)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    uart.Write(kFrame, NextUart::kDefaultFrame | NextUart::kFrameFlow, 0);
    peer.cts = false;
    uart.Write(kTx, 0x42, 0);
    uart.Advance(10 * kFrame8N1);
    EXPECT_TRUE(peer.sent.empty()) << "S_RTR: the receiver is not ready";
    peer.cts = true;
    uart.Advance(10 * kFrame8N1 + 10);
    uart.Advance(12 * kFrame8N1 + 10);
    ASSERT_EQ(peer.sent.size(), 1u);

    // RTS goes inactive when the Rx FIFO is within two bytes of full
    EXPECT_TRUE(peer.rtsFromZx);
    for (unsigned i = 0; i < NextUart::kRxFifo; i++)
        peer.toZx.push_back(0);
    uart.Advance(20 * kFrame8N1);
    uart.Advance(20 * kFrame8N1 + (NextUart::kRxFifo + 5) * kFrame8N1);
    EXPECT_FALSE(peer.rtsFromZx);
    EXPECT_FALSE(uart.GetView(0).rtsAsserted);
    EXPECT_EQ(uart.GetView(0).overflow, false) << "the peer stopped before the FIFO overflowed";
}

TEST(NextUart_Test, InterruptRequestsAreRisingEdgesOfTheLevels)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    struct Hit { unsigned channel; bool tx; };
    std::vector<Hit> hits;
    uart.onInterrupt = [&](unsigned channel, bool tx) { hits.push_back({channel, tx}); };
    constexpr uint8_t kRxOnAnyByte = 0x01, kTxEnable = 0x04;
    uart.PollInterrupts(kRxOnAnyByte | kTxEnable);
    ASSERT_EQ(hits.size(), 2u) << "channel 0 and 1 Tx empty rise after a reset";
    hits.clear();
    uart.PollInterrupts(kRxOnAnyByte | kTxEnable);
    EXPECT_TRUE(hits.empty()) << "a level is no new request";
    peer.toZx = {0x55};
    uart.Advance(1);
    uart.Advance(1 + kFrame8N1);
    uart.PollInterrupts(kRxOnAnyByte);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].channel, 0u);
    EXPECT_FALSE(hits[0].tx);
    // Tx: a byte makes the FIFO non-empty (the level falls) and the next empty is a request
    hits.clear();
    uart.Write(kTx, 1, 2000000);
    uart.PollInterrupts(kTxEnable);
    EXPECT_TRUE(hits.empty());
    uart.Write(kTx, 2, 2000000);
    uart.PollInterrupts(kTxEnable);  // the second byte waits in the FIFO: the level is low
    EXPECT_TRUE(hits.empty());
    uart.Advance(2000000 + 3 * kFrame8N1);
    uart.PollInterrupts(kTxEnable);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_TRUE(hits[0].tx);
}

class NextUartMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;
    LoopbackPeer _echo;  // outlives the emulator (TearDown runs first): the decoder detaches from its peers when it goes
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

TEST_F(NextUartMachine_Test, ThePortsReachTheDeviceAndNR0x82To0x85Gate)
{
    _ports->Uart().SetPeer(0, &_echo);
    _ports->DecodePortOut(kTx, 0x7E, 0x8000);
    EXPECT_EQ(_ports->Uart().GetView(0).txCount + (_ports->Uart().GetView(0).txBusy ? 1u : 0u), 1u);
    EXPECT_EQ(_ports->DecodePortIn(kFrame, 0x8000), NextUart::kDefaultFrame);
    // the UARTs are bit 12 of the enable word: NR #83 bit 4
    _ports->Board().WriteNextReg(0x83, 0xEF);
    EXPECT_EQ(_ports->DecodePortIn(kFrame, 0x8000), 0xFF) << "off: the idle bus";
    _ports->DecodePortOut(kFrame, 0x00, 0x8000);
    _ports->Board().WriteNextReg(0x83, 0xFF);
    EXPECT_EQ(_ports->DecodePortIn(kFrame, 0x8000), NextUart::kDefaultFrame) << "the write while off did not land";
}

TEST_F(NextUartMachine_Test, TheEnableAndStatusBitsOfNR0xC6AndNR0xCAAreTheVhdlsPlaces)
{
    NextInterruptSource& ints = _ports->Interrupts();
    _ports->Board().WriteNextReg(0xC0, 0x01);  // hardware IM2 mode
    _ports->Board().WriteNextReg(0xC6, 0xFF);
    EXPECT_EQ(_ports->Board().Read(0xC6), 0x77) << "bits 7 and 3 read 0";
    ints.Raise(NextInterruptSource::kUart1Rx);
    ints.Raise(NextInterruptSource::kUart0Tx);
    EXPECT_EQ(_ports->Board().Read(0xCA), 0x30 | 0x04) << "UART 1 Rx is bits 5:4, UART 0 Tx bit 2";
    _ports->Board().WriteNextReg(0xCA, 0x04);
    EXPECT_EQ(_ports->Board().Read(0xCA), 0x30);
    _ports->Board().WriteNextReg(0xCA, 0x10);
    EXPECT_EQ(_ports->Board().Read(0xCA), 0x00) << "either of bits 5:4 clears UART 1 Rx";
}

TEST(NextUartConfigured_Test, ThePeersComeFromComPortAndComPort2)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero, [](CONFIG& config) {
        std::snprintf(config.network.comPort, sizeof config.network.comPort, "LOOPBACK");
        std::snprintf(config.network.comPort2, sizeof config.network.comPort2, "LOOPBACK");
    });
    ASSERT_NE(emulator, nullptr);
    auto* ports = dynamic_cast<PortDecoder_Next*>(emulator->GetContext()->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    ASSERT_NE(ports->Uart().Peer(0), nullptr) << "ComPort= is UART 0, the ESP's line";
    ASSERT_NE(ports->Uart().Peer(1), nullptr) << "ComPort2= is UART 1, the Pi's";
    // a byte out of UART 1 comes back on UART 1 only (once NR #A0 connects its pins: bit 4 the UART, bit 5 the Pi orientation)
    ports->Board().WriteNextReg(0xA0, 0x30);
    ports->DecodePortOut(kSelect, kSelectPi, 0x8000);
    ports->DecodePortOut(kTx, 0x5A, 0x8000);
    EmulatorTestHelper::RunFramesFast(emulator, 2);
    EXPECT_NE(ports->DecodePortIn(kTx, 0x8000) & NextUart::kStatusRxAvail, 0);
    EXPECT_EQ(ports->DecodePortIn(kRx, 0x8000), 0x5A);
    ports->DecodePortOut(kSelect, 0x00, 0x8000);
    EXPECT_EQ(ports->DecodePortIn(kTx, 0x8000) & NextUart::kStatusRxAvail, 0) << "UART 0 saw nothing";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST_F(NextUartMachine_Test, TheReportShowsTheChannelsTheStatusAndTheLine)
{
    _ports->Uart().SetPeer(1, &_echo);
    _ports->DecodePortOut(kFrame, 0x1C, 0x8000);  // 8 data bits, parity enabled, even
    _ports->DecodePortOut(kSelect, kSelectPi | kPrescalerFollows | 0x01, 0x8000);
    _ports->DecodePortOut(kTx, 0x41, 0x8000);
    const StateNode report = DeviceState::NextUartReport(_context);
    ASSERT_TRUE(report.find("available")->b);
    EXPECT_EQ(report.find("selected_name")->s, "pi");
    const StateNode* channels = report.find("channels");
    ASSERT_NE(channels, nullptr);
    ASSERT_EQ(channels->items.size(), 2u);
    const StateNode& esp = channels->items[0];
    const StateNode& pi = channels->items[1];
    EXPECT_EQ(esp.find("name")->s, "esp");
    EXPECT_EQ(esp.find("frame")->find("parity")->s, "E") << "NR frame #1C: parity on, even";
    EXPECT_TRUE(esp.find("peer")->find("attached")->b) << "the board ships with its ESP module on UART 0";
    EXPECT_TRUE(pi.find("peer")->find("attached")->b);
    EXPECT_EQ(pi.find("peer")->find("kind")->s, "loopback");
    EXPECT_EQ(pi.find("prescaler")->i, (1 << 14) | NextUart::kDefaultPrescaler);
    EXPECT_TRUE(pi.find("tx")->find("busy")->b);
    EXPECT_FALSE(pi.find("status")->find("tx_empty")->b);
    // the report does not take the sticky bits: a second look is the same
    EXPECT_EQ(DeviceState::NextUartReport(_context).find("channels")->items[1].find("status")->find("value")->s, pi.find("status")->find("value")->s);
}

// The board ships with its ESP-12 module on UART 0: nothing is configured, "AT" gets "OK" (the default ComPort of a Next is AT)
TEST(NextUartConfigured_Test, TheShippedEspAnswersAtOnUart0)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    auto* ports = dynamic_cast<PortDecoder_Next*>(emulator->GetContext()->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    ASSERT_NE(ports->Uart().Peer(0), nullptr);
    EXPECT_STREQ(ports->Uart().Peer(0)->Kind(), "at") << "an ESP8266 with Espressif's AT firmware";
    for (char c : std::string("AT\r\n"))
        ports->DecodePortOut(kTx, static_cast<uint8_t>(c), 0x8000);
    std::string answer;
    for (int frame = 0; frame < 20 && answer.find("OK") == std::string::npos; frame++)
    {
        EmulatorTestHelper::RunFramesFast(emulator, 1);
        while (ports->DecodePortIn(kTx, 0x8000) & NextUart::kStatusRxAvail)
            answer += static_cast<char>(ports->DecodePortIn(kRx, 0x8000));
    }
    EXPECT_NE(answer.find("OK"), std::string::npos) << "got: " << answer;
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// NR #A8 / #A9 drive the ESP's GPIO0: low when the module boots (AT+RST) it enters its ROM's download mode and stops answering AT
TEST(NextUartConfigured_Test, GpioZeroLowAtAResetPutsTheEspInDownloadMode)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    auto* ports = dynamic_cast<PortDecoder_Next*>(emulator->GetContext()->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    auto* esp = dynamic_cast<EspModule*>(ports->Uart().Peer(0));
    ASSERT_NE(esp, nullptr);
    EXPECT_FALSE(esp->FlashPinLow());
    // the pin as the registers read: pulled up until NR #A8 enables the output
    EXPECT_EQ(ports->Board().Read(0xA9) & 0x05, 0x05);
    ports->Board().WriteNextReg(0xA9, 0x00);
    EXPECT_FALSE(esp->FlashPinLow()) << "the output is not enabled yet";
    ports->Board().WriteNextReg(0xA8, 0x01);
    EXPECT_TRUE(esp->FlashPinLow());
    EXPECT_EQ(ports->Board().Read(0xA9) & 0x01, 0x00) << "the pad now follows the output";
    for (char c : std::string("AT+RST\r\n"))
        ports->DecodePortOut(kTx, static_cast<uint8_t>(c), 0x8000);
    for (int frame = 0; frame < 30 && !esp->DownloadMode(); frame++)
        EmulatorTestHelper::RunFramesFast(emulator, 1);
    EXPECT_TRUE(esp->DownloadMode());
    ports->Board().WriteNextReg(0xA9, 0x01);
    EXPECT_FALSE(esp->FlashPinLow());
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// ---- a break from a peer (serial/uart_rx.vhd: S_ERROR with the shift register at zero, o_err_break) ----

namespace
{
// 8N1 at the default prescaler: half a bit, then 8 data bits, then the stop bit (9 bit times); the break condition eight bit times later
constexpr uint64_t kBit = NextUart::kDefaultPrescaler;
constexpr uint64_t kStopSample = kBit / 2 + 9 * kBit;
constexpr uint64_t kBreakAt = kStopSample + 8 * kBit;
}  // namespace

TEST(NextUart_Test, ABreakFromAPeerIsAFramingErrorThenTheBreakCondition)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    peer.toZx = {0x55};   // waits behind the break: a held line starts no byte
    peer.breakHeld = true;
    uart.Advance(1000);   // the receiver sees the line low from here
    uart.Advance(1000 + kStopSample - 1);
    EXPECT_EQ(uart.GetView(0).status & (NextUart::kStatusFraming | NextUart::kStatusBreak), 0) << "before the stop bit's sample point";
    uart.Advance(1000 + kStopSample);
    EXPECT_NE(uart.GetView(0).status & NextUart::kStatusFraming, 0) << "the stop bit is low: a framing error";
    EXPECT_EQ(uart.GetView(0).status & NextUart::kStatusBreak, 0);
    EXPECT_EQ(uart.GetView(0).rxCount, 0u) << "the byte with the error is thrown away";
    uart.Advance(1000 + kBreakAt - 1);
    EXPECT_EQ(uart.GetView(0).status & NextUart::kStatusBreak, 0) << "eight bit times of zeros are not through";
    // the status read: both flags, then the sticky one is gone and the held one stays
    const uint8_t status = uart.Read(kTx, 1000 + kBreakAt);
    EXPECT_NE(status & NextUart::kStatusBreak, 0);
    EXPECT_NE(status & NextUart::kStatusFraming, 0);
    EXPECT_NE(uart.Read(kTx, 1000 + kBreakAt + 10) & NextUart::kStatusBreak, 0) << "held while the line is low";
    EXPECT_EQ(uart.GetView(0).status & NextUart::kStatusFraming, 0);
    EXPECT_EQ(peer.toZx.size(), 1u) << "no byte was taken from the peer meanwhile";
    // the line rises: the condition goes, the waiting byte arrives
    peer.breakHeld = false;
    uart.Advance(20000);
    EXPECT_EQ(uart.GetView(0).status & NextUart::kStatusBreak, 0);
    uart.Advance(20000 + 2 * kFrame8N1);
    EXPECT_EQ(uart.GetView(0).rxCount, 1u);
    EXPECT_EQ(uart.Read(kRx, 20000 + 2 * kFrame8N1), 0x55);
}

TEST(NextUart_Test, ALinePulseShorterThanAFrameIsAByteWithZeroBits)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    peer.breakHeld = true;
    uart.Advance(500);
    // the data bits are sampled at half a bit + n bits: 4 are inside 1300 clocks, so the low nibble is 0 and the rest 1
    peer.breakHeld = false;
    uart.Advance(500 + 1300);
    EXPECT_EQ(uart.GetView(0).rxCount, 0u) << "the frame ends at the stop bit";
    uart.Advance(500 + kStopSample);
    ASSERT_EQ(uart.GetView(0).rxCount, 1u);
    EXPECT_EQ(uart.GetView(0).status & (NextUart::kStatusFraming | NextUart::kStatusBreak), 0);
    EXPECT_EQ(uart.Read(kRx, 500 + kStopSample), 0xF0);
    // a glitch under half a bit is no start bit
    peer.breakHeld = true;
    uart.Advance(100000);
    peer.breakHeld = false;
    uart.Advance(100000 + kBit / 2 - 1);
    uart.Advance(200000);
    EXPECT_EQ(uart.GetView(0).rxCount, 0u);
}

TEST(NextUart_Test, ALowLineWithTheWrongParityIsAParityErrorCountedWithTheFramingErrors)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    uart.Write(kFrame, NextUart::kDefaultFrame | NextUart::kFrameParity, 0);   // 8E1
    peer.breakHeld = true;
    uart.Advance(500);
    peer.breakHeld = false;
    uart.Advance(500 + 1300);   // the data bits read 0xF0 (even parity) and the parity bit was sampled after the pulse: high
    uart.Advance(500 + 4000);
    EXPECT_EQ(uart.GetView(0).rxCount, 0u);
    EXPECT_NE(uart.GetView(0).status & NextUart::kStatusFraming, 0);
}

TEST(NextUart_Test, AFifoResetPausesTheReceiverUntilTheLineRises)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    peer.breakHeld = true;
    uart.Write(kFrame, NextUart::kDefaultFrame | NextUart::kFrameReset, 0);
    uart.Write(kFrame, NextUart::kDefaultFrame, 100);   // the reset is released with the line still low
    uart.Advance(100 + 3 * kBreakAt);
    EXPECT_EQ(uart.GetView(0).status & (NextUart::kStatusFraming | NextUart::kStatusBreak), 0) << "S_PAUSE waits for the line to rise";
    peer.breakHeld = false;
    peer.toZx = {0x21};
    uart.Advance(100 + 3 * kBreakAt + 5);
    uart.Advance(100 + 3 * kBreakAt + 5 + 2 * kFrame8N1);
    EXPECT_EQ(uart.GetView(0).rxCount, 1u);
}

TEST(NextUart_Test, TheTransmittersBreakHoldsTxLowAndTheTransmitterBusy)
{
    NextUart uart;
    ScriptedPeer peer;
    uart.SetPeer(0, &peer);
    EXPECT_NE(uart.GetView(0).status & NextUart::kStatusTxEmpty, 0);
    uart.Write(kFrame, NextUart::kDefaultFrame | NextUart::kFrameBreak, 0);
    EXPECT_TRUE(peer.breakFromZx) << "the peer is told the Tx pin is held low";
    EXPECT_EQ(uart.GetView(0).status & NextUart::kStatusTxEmpty, 0) << "uart_tx.vhd o_busy holds in break";
    uart.Write(kTx, 0x33, 10);
    uart.Advance(10 + 2 * kFrame8N1);
    EXPECT_TRUE(peer.sent.empty()) << "nothing is sent in the break state";
    uart.Write(kFrame, NextUart::kDefaultFrame, 10 + 2 * kFrame8N1);
    EXPECT_FALSE(peer.breakFromZx);
    uart.Advance(10 + 4 * kFrame8N1);
    ASSERT_EQ(peer.sent.size(), 1u);
    EXPECT_EQ(peer.sent[0], 0x33);
}

TEST(NextUart_Test, ATestPlugReturnsTheTransmittersBreakAsAReceivedBreak)
{
    NextUart uart;
    LoopbackPeer plug(true);
    uart.SetPeer(0, &plug);
    uart.Write(kFrame, NextUart::kDefaultFrame | NextUart::kFrameBreak, 0);
    uart.Advance(kBreakAt);
    EXPECT_NE(uart.GetView(0).status & NextUart::kStatusBreak, 0) << "the wire loops Tx to Rx";
    uart.Write(kFrame, NextUart::kDefaultFrame, kBreakAt + 100);
    uart.Advance(kBreakAt + 200);
    EXPECT_EQ(uart.GetView(0).status & NextUart::kStatusBreak, 0);
}

// ---- the joystick connector's UART (zxnext.vhd 3340-3350: NR #0B bits 7, 5 and 0) ----

TEST(NextUart_Test, TheJoystickConnectorTakesTheSelectedUartAwayFromItsModule)
{
    NextUart uart;
    ScriptedPeer esp, pi, joy;
    uart.SetPeer(0, &esp);
    uart.SetPeer(1, &pi);
    uart.SetJoystickPeer(&joy);
    EXPECT_EQ(uart.Wire(0), &esp) << "the connector carries no UART until NR #0B says so";

    uart.SetJoystickRoute(true, 0);
    EXPECT_EQ(uart.Wire(0), &joy);
    EXPECT_EQ(uart.Wire(1), &pi) << "the other UART keeps its module";
    EXPECT_FALSE(esp.rtsFromZx) << "the module's ready-to-receive pin is forced not ready";
    EXPECT_TRUE(uart.GetView(0).joystick);

    esp.toZx = {0x99};
    joy.toZx = {0x07};
    uart.Write(kTx, 0x42, 0);
    uart.Advance(3 * kFrame8N1);
    ASSERT_EQ(joy.sent.size(), 1u);
    EXPECT_EQ(joy.sent[0], 0x42);
    EXPECT_TRUE(esp.sent.empty()) << "Tx idle for the module (uart0_tx_esp forced 1)";
    EXPECT_EQ(uart.GetView(0).rxCount, 1u);
    EXPECT_EQ(uart.Read(kRx, 3 * kFrame8N1), 0x07) << "only the connector's device is heard";
    EXPECT_EQ(esp.toZx.size(), 1u);

    // the connector lets go: the module is back with the line as it was
    uart.SetJoystickRoute(false, 0);
    EXPECT_EQ(uart.Wire(0), &esp);
    EXPECT_TRUE(esp.rtsFromZx);
    uart.Advance(10 * kFrame8N1);   // the module's byte starts here
    uart.Advance(12 * kFrame8N1);
    EXPECT_EQ(uart.GetView(0).rxCount, 1u);
    EXPECT_EQ(uart.Read(kRx, 12 * kFrame8N1), 0x99);

    // UART 1 on the connector (bit 0 of NR #0B)
    uart.SetJoystickRoute(true, 1);
    EXPECT_EQ(uart.Wire(1), &joy);
    EXPECT_EQ(uart.Wire(0), &esp);
    EXPECT_FALSE(pi.rtsFromZx);
}

TEST(NextUart_Test, TheJoystickConnectorHasNoReadyToReceivePinSoAFullFifoCannotHoldItsDeviceOff)
{
    NextUart uart;
    ScriptedPeer joy;
    joy.toZx.assign(NextUart::kAlmostFull + 8, 0xA5);
    uart.SetJoystickPeer(&joy);
    uart.SetJoystickRoute(true, 0);
    uart.Write(kFrame, NextUart::kDefaultFrame | NextUart::kFrameFlow, 0);
    uart.Advance((NextUart::kRxFifo + 10) * kFrame8N1);
    EXPECT_GE(uart.GetView(0).rxCount, NextUart::kAlmostFull + 1) << "a module would have been held at 510 by RTR";
}

TEST_F(NextUartMachine_Test, NR0x0BBitsSevenAndFiveRouteAUartToTheConnectorAndAResetLetsGo)
{
    _ports->Uart().SetJoystickPeer(&_echo);
    EXPECT_FALSE(_ports->Uart().JoystickRouted());
    _ports->Board().WriteNextReg(0x0B, 0x80);   // the connector's I/O mode on, UART mode (bit 5) off
    EXPECT_FALSE(_ports->Uart().JoystickRouted()) << "bit 5 chooses the UART mode";
    _ports->Board().WriteNextReg(0x0B, 0xA0);   // UART mode, UART 0
    ASSERT_TRUE(_ports->Uart().JoystickRouted());
    EXPECT_EQ(_ports->Uart().JoystickChannel(), 0u);
    _ports->Board().WriteNextReg(0x0B, 0xA1);   // UART 1
    EXPECT_EQ(_ports->Uart().JoystickChannel(), 1u);
    // a byte out of UART 1 goes to the connector's device and comes back
    _ports->DecodePortOut(kSelect, kSelectPi, 0x8000);
    _ports->DecodePortOut(kTx, 0x6D, 0x8000);
    bool back = false;
    for (int frame = 0; frame < 4 && !back; frame++)
    {
        EmulatorTestHelper::RunFramesFast(_emulator, 1);
        back = (_ports->DecodePortIn(kTx, 0x8000) & NextUart::kStatusRxAvail) != 0;
    }
    ASSERT_TRUE(back);
    EXPECT_EQ(_ports->DecodePortIn(kRx, 0x8000), 0x6D);
    // NR #0B goes back to its reset value on a reset
    _ports->PerformReset(true);
    EXPECT_FALSE(_ports->Uart().JoystickRouted());
}

TEST(NextUartConfigured_Test, ComPortJoyPlugsADeviceIntoTheConnector)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero, [](CONFIG& config) {
        std::snprintf(config.network.comPortJoy, sizeof config.network.comPortJoy, "LOOPBACK");
    });
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    ASSERT_NE(ports->Uart().JoystickPeer(), nullptr) << "ComPortJoy= is the connector's device";
    EXPECT_EQ(ports->Uart().JoystickPeer(), context->pJoystickSerialPeer);
    EXPECT_STREQ(ports->Uart().JoystickPeer()->Kind(), "loopback");
    EmulatorTestHelper::CleanupEmulator(emulator);
}


// The per-instruction poll of the machine calls Look instead of Advance while the UART is quiet (NextUart::Quiet). Two UARTs get the
// same random traffic - bytes from the peers, bytes to send, frame changes, flow control, breaks, a cut line - and the same clock;
// one always advances, the other takes the quiet path when it may. Everything the machine and the peers can see is compared after
// every poll
TEST(NextUart_Test, QuietLookMatchesAdvanceOnEveryPoll)
{
    struct Side
    {
        NextUart uart;
        ScriptedPeer peer[2];
        std::vector<unsigned> interrupts;
        unsigned looks = 0;
    };
    Side reference, quiet;
    for (Side* side : {&reference, &quiet})
    {
        side->uart.SetPeer(0, &side->peer[0]);
        side->uart.SetPeer(1, &side->peer[1]);
        side->uart.onInterrupt = [side](unsigned channel, bool tx) { side->interrupts.push_back(channel * 2 + (tx ? 1 : 0)); };
    }
    uint32_t seed = 0x0A47B10Cu;
    auto next = [&]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    uint64_t now = 0;
    uint8_t enable = 0x77;
    for (unsigned step = 0; step < 40000; step++)
    {
        now += 32 + next() % 160;
        const unsigned action = next() % 3000;
        const unsigned channel = next() & 1;
        const uint8_t value = static_cast<uint8_t>(next());
        const unsigned burst = 1 + next() % 5;
        const bool settle = step % 1500 == 1499;  // now and then everything goes back to a working line, so the UART falls quiet again
        for (Side* side : {&reference, &quiet})
        {
            NextUart& uart = side->uart;
            if (settle)
            {
                for (unsigned c = 0; c < 2; c++)
                {
                    uart.Write(kSelect, static_cast<uint8_t>(c ? kSelectPi : 0), now);
                    uart.Write(kFrame, NextUart::kDefaultFrame, now);
                    side->peer[c].breakHeld = false;
                    side->peer[c].cts = true;
                }
                uart.SetLineConnected(1, true);
            }
            else if (action < 20)
                for (unsigned i = 0; i < burst; i++)
                    side->peer[channel].toZx.push_back(static_cast<uint8_t>(value + i));  // the peer has something to say
            else if (action < 30)
                uart.Write(kTx, value, now);
            else if (action < 55)
                (void)uart.Read(kRx, now);
            else if (action < 65)
                (void)uart.Read(kTx, now);  // the status: clears the sticky errors
            else if (action < 70)
                uart.Write(kSelect, static_cast<uint8_t>(channel ? kSelectPi : 0), now);
            else if (action < 74)
                uart.Write(kFrame, static_cast<uint8_t>((value & 0x3F) | (value % 7 == 0 ? NextUart::kFrameReset : 0) |
                                                        (value % 5 == 0 ? NextUart::kFrameBreak : 0)), now);
            else if (action < 77)
                uart.Write(kFrame, NextUart::kDefaultFrame, now);
            else if (action < 80)
                side->peer[channel].breakHeld = !side->peer[channel].breakHeld;
            else if (action < 82)
                side->peer[channel].breakHeld = false;
            else if (action < 84)
                side->peer[channel].cts = (value & 1) != 0;
            else if (action < 86)
                uart.SetLineConnected(1, (value & 1) != 0);
            else if (action == 86)
                uart.Write(kRx, static_cast<uint8_t>(0x20 + (value & 0x5F)), now);  // the prescaler's low bits: a faster or slower line
        }
        if (action == 87)
            enable = static_cast<uint8_t>(next() & 0x77);
        // the machine's poll
        reference.uart.Advance(now);
        if (quiet.uart.Quiet())
        {
            quiet.uart.Look(now);
            quiet.looks++;
        }
        else
            quiet.uart.Advance(now);
        reference.uart.PollInterrupts(enable);
        quiet.uart.PollInterrupts(enable);
        for (unsigned c = 0; c < 2; c++)
        {
            const NextUart::View a = reference.uart.GetView(c), b = quiet.uart.GetView(c);
            ASSERT_EQ(b.status, a.status) << "step " << step << " channel " << c;
            ASSERT_EQ(b.rxCount, a.rxCount) << "step " << step << " channel " << c;
            ASSERT_EQ(b.txCount, a.txCount) << "step " << step << " channel " << c;
            ASSERT_EQ(b.txBusy, a.txBusy) << "step " << step << " channel " << c;
            ASSERT_EQ(b.rxBusy, a.rxBusy) << "step " << step << " channel " << c;
            ASSERT_EQ(b.breakIn, a.breakIn) << "step " << step << " channel " << c;
            ASSERT_EQ(b.rxLineLow, a.rxLineLow) << "step " << step << " channel " << c;
            ASSERT_EQ(b.rtsAsserted, a.rtsAsserted) << "step " << step << " channel " << c;
            ASSERT_EQ(b.sent, a.sent) << "step " << step << " channel " << c;
            ASSERT_EQ(b.received, a.received) << "step " << step << " channel " << c;
            ASSERT_EQ(b.lost, a.lost) << "step " << step << " channel " << c;
            ASSERT_EQ(quiet.peer[c].toZx.size(), reference.peer[c].toZx.size()) << "step " << step << " channel " << c;
            ASSERT_EQ(quiet.peer[c].sent.size(), reference.peer[c].sent.size()) << "step " << step << " channel " << c;
            ASSERT_EQ(quiet.peer[c].rtsFromZx, reference.peer[c].rtsFromZx) << "step " << step << " channel " << c;
        }
        ASSERT_EQ(quiet.interrupts.size(), reference.interrupts.size()) << "step " << step;
    }
    EXPECT_EQ(quiet.interrupts, reference.interrupts);
    for (unsigned c = 0; c < 2; c++)
        EXPECT_EQ(quiet.peer[c].sent, reference.peer[c].sent);
    EXPECT_GT(reference.uart.GetView(0).received, 100u) << "bytes arrived";
    EXPECT_GT(reference.uart.GetView(0).sent, 20u) << "bytes left";
    EXPECT_GT(quiet.looks, 10000u) << "the quiet path was taken";
    EXPECT_LT(quiet.looks, 39000u) << "and so was Advance";
}
