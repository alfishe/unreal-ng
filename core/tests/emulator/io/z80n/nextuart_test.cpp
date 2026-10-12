// The Next's UARTs (docs/inprogress/2026-10-07-zx-next: D1). Source: zxnext.vhd, serial/uart.vhd, uart_rx.vhd, uart_tx.vhd, fifop.vhd

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/z80n/nextinterrupts.h"
#include "emulator/io/z80n/nextuart.h"
#include "emulator/ports/models/portdecoder_next.h"

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
    const char* Kind() const override { return "scripted"; }
    size_t Pending() const override { return toZx.size(); }
    SerialLine settings;
    bool rtsFromZx = true;
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
    EXPECT_EQ(peer.settings.baud, 28000000u / NextUart::kDefaultPrescaler);
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
    LoopbackPeer echo;
    _ports->Uart().SetPeer(0, &echo);
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
