// 16550 UART model (network adapters TDD §7.1): both flavors - the ZX-Evo
// AVR's emulation (BaseConf rs232.c) and a real 16550 (ZX-WiFi). Time is
// driven by hand: T-states of a 3.5 MHz machine.

#include <gtest/gtest.h>

#include <deque>
#include <vector>

#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/serial/uart16550.h"

namespace
{
/// A peer the test fills and reads
struct ScriptPeer : ISerialPeer
{
    std::deque<uint8_t> toZx;
    std::vector<uint8_t> fromZx;
    bool cts = true;
    int modemCalls = 0;
    bool rts = false;
    std::vector<SerialLine> lines;

    void OnLineSettings(const SerialLine& line) override { lines.push_back(line); }

    void Transmit(uint8_t byte) override { fromZx.push_back(byte); }
    bool HasByte() const override { return !toZx.empty(); }
    uint8_t TakeByte() override
    {
        const uint8_t b = toZx.front();
        toZx.pop_front();
        return b;
    }
    bool Cts() const override { return cts; }
    void OnModemLines(bool r, bool) override
    {
        rts = r;
        ++modemCalls;
    }
    const char* Kind() const override { return "script"; }
    size_t Pending() const override { return toZx.size(); }
};

constexpr uint8_t kRbr = Uart16550::kRbrThr, kIer = Uart16550::kIer, kFcr = Uart16550::kIirFcr,
                  kLcr = Uart16550::kLcr, kMcr = Uart16550::kMcr, kLsr = Uart16550::kLsr, kMsr = Uart16550::kMsr,
                  kScr = Uart16550::kScr;
}  // namespace

class Uart16550_Test : public ::testing::Test
{
protected:
    ScriptPeer peer;
    uint64_t now = 1000;

    Uart16550 Make(Uart16550::Flavor flavor)
    {
        Uart16550 uart(Uart16550::DefaultParams(flavor), 3500000);
        uart.SetPeer(&peer);
        return uart;
    }

    // The NedoOS init (esp-com.c uart_init): 115200 8N1, FIFOs reset, MCR #2F
    void InitLikeNedoOs(Uart16550& u)
    {
        u.Write(kFcr, 0x87, now);
        u.Write(kLcr, 0x83, now);
        u.Write(kRbr, 0x01, now);
        u.Write(kIer, 0x00, now);
        u.Write(kLcr, 0x03, now);
        u.Write(kIer, 0x00, now);
        u.Write(kMcr, 0x2F, now);
    }
};

TEST_F(Uart16550_Test, EvoResetValuesFromTheAvrFirmware)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    EXPECT_EQ(u.Read(kLsr, now), 0x60);
    EXPECT_EQ(u.Read(kMsr, now) & 0xE0, 0xA0) << "DCD and DSR read 1, RI 0";
    EXPECT_EQ(u.Read(kScr, now), 0xFF);
    EXPECT_EQ(u.Read(kFcr, now), 0x01) << "IIR is a constant #01";
    EXPECT_EQ(u.Read(kLcr, now), 0x00);
    EXPECT_EQ(u.Baud(), 115200u);
    EXPECT_EQ(u.FrameBits(), 11u) << "the USART starts 8N2 until LCR is written";
    u.Write(kLcr, 0x03, now);
    EXPECT_EQ(u.FrameBits(), 10u);
}

TEST_F(Uart16550_Test, ZxWifiResetValuesOfA16550)
{
    Uart16550 u = Make(Uart16550::Flavor::ZxWifi);
    EXPECT_EQ(u.Read(kLsr, now), 0x60);
    EXPECT_EQ(u.Read(kFcr, now), 0x01);
    EXPECT_EQ(u.Read(kScr, now), 0x00);
    u.Write(kFcr, 0x01, now);
    EXPECT_EQ(u.Read(kFcr, now), 0xC1) << "FIFOs enabled show in IIR bits 7:6";
}

TEST_F(Uart16550_Test, EvoBaudRatesAsTheAvrComputesThem)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    auto setDivisor = [&](uint8_t lo, uint8_t hi) {
        u.Write(kLcr, 0x83, now);
        u.Write(kRbr, lo, now);
        u.Write(kIer, hi, now);
        u.Write(kLcr, 0x03, now);
    };
    setDivisor(3, 0);
    EXPECT_EQ(u.Baud(), 38400u);
    setDivisor(0, 0);
    EXPECT_EQ(u.Baud(), 345600u) << "divisor 0: the firmware's \"256000\" computes to 345600";
    setDivisor(5, 0x80);
    EXPECT_EQ(u.Baud(), 115200u) << "DLM bit 7: the AVR's own UBRR (691200 / (5 + 1))";
}

TEST_F(Uart16550_Test, TransmitTakesACharacterTimePerByte)
{
    Uart16550 u = Make(Uart16550::Flavor::ZxWifi);
    InitLikeNedoOs(u);
    const uint64_t charT = u.CharacterT();
    EXPECT_EQ(charT, (10ull * 3500000 + 115199) / 115200) << "10 bits at 115200 in 3.5 MHz T-states";

    u.Write(kRbr, 'A', now);
    u.Write(kRbr, 'B', now);
    EXPECT_EQ(u.Read(kLsr, now) & 0x60, 0x00) << "a byte in the FIFO, one on the line";
    u.Advance(now + charT - 1);
    EXPECT_TRUE(peer.fromZx.empty());
    u.Advance(now + charT);
    ASSERT_EQ(peer.fromZx.size(), 1u);
    EXPECT_EQ(u.Read(kLsr, now + charT) & 0x60, 0x20) << "THRE: FIFO empty, TEMT: 'B' still shifting";
    u.Advance(now + 2 * charT);
    EXPECT_EQ(peer.fromZx, (std::vector<uint8_t>{'A', 'B'}));
    EXPECT_EQ(u.Read(kLsr, now + 2 * charT) & 0x60, 0x60);
}

TEST_F(Uart16550_Test, TheEvoPeerSendsOnlyWhileRtsIsAsserted)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    u.Write(kLcr, 0x03, now);
    const uint64_t charT = u.CharacterT();
    peer.toZx = {'x', 'y', 'z'};
    u.Advance(now + 10 * charT);
    EXPECT_EQ(u.Read(kLsr, now + 10 * charT) & 0x01, 0x00) << "RTS off: nothing comes";

    // NedoOS type 0: pulse RTS (MCR 2, MCR 0) while DR is 0 - one byte per pulse
    now += 10 * charT;
    u.Write(kMcr, 0x02, now);
    u.Write(kMcr, 0x00, now + 100);
    u.Advance(now + charT);
    EXPECT_EQ(u.Read(kLsr, now + charT) & 0x01, 0x01) << "the byte started under RTS arrives";
    EXPECT_EQ(u.Read(kRbr, now + charT), 'x');
    u.Advance(now + 5 * charT);
    EXPECT_EQ(u.Read(kLsr, now + 5 * charT) & 0x01, 0x00) << "no second byte after RTS went off";
    EXPECT_EQ(peer.toZx.size(), 2u);
}

TEST_F(Uart16550_Test, EvoOverrunIsStickyUntilAnFcrReset)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    u.Write(kLcr, 0x03, now);
    const uint64_t charT = u.CharacterT();
    for (int i = 0; i < 20; ++i)
        peer.toZx.push_back(static_cast<uint8_t>(i));
    u.Write(kMcr, 0x02, now);
    u.Advance(now + 20 * charT);
    uint8_t lsr = u.Read(kLsr, now + 20 * charT);
    EXPECT_EQ(lsr & 0x83, 0x83) << "DR, OE, and bit 7 = RX half full";
    EXPECT_EQ(u.GetView().rxCount, 16);
    EXPECT_EQ(u.Read(kLsr, now + 20 * charT) & 0x02, 0x02) << "reading LSR does not clear OE on the Evo";
    u.Write(kFcr, 0x03, now + 20 * charT);
    EXPECT_EQ(u.Read(kLsr, now + 20 * charT) & 0x83, 0x00) << "FCR RX reset: FIFO empty, OE clear";
    EXPECT_EQ(u.Read(kRbr, now + 20 * charT), 0x00) << "RBR of an empty FIFO reads #00 on the Evo";
}

TEST_F(Uart16550_Test, ZxWifiOverrunClearsOnLsrRead)
{
    Uart16550 u = Make(Uart16550::Flavor::ZxWifi);
    u.Write(kFcr, 0x01, now);
    u.Write(kLcr, 0x03, now);
    u.Write(kMcr, 0x02, now);   // RTS, no AFE
    const uint64_t charT = u.CharacterT();
    for (int i = 0; i < 20; ++i)
        peer.toZx.push_back(static_cast<uint8_t>(i));
    u.Advance(now);   // the peer's onReceive: the bytes are there from now on
    u.Advance(now + 20 * charT);
    EXPECT_EQ(u.Read(kLsr, now + 20 * charT) & 0x03, 0x03);
    EXPECT_EQ(u.Read(kLsr, now + 20 * charT) & 0x02, 0x00);
}

TEST_F(Uart16550_Test, ZxWifiAutoRtsStopsThePeerAtTheTriggerLevel)
{
    Uart16550 u = Make(Uart16550::Flavor::ZxWifi);
    InitLikeNedoOs(u);   // FCR #87: trigger 8; MCR #2F: AFE + RTS
    const uint64_t charT = u.CharacterT();
    for (int i = 0; i < 30; ++i)
        peer.toZx.push_back(static_cast<uint8_t>(i));
    u.Advance(now);   // the peer's onReceive
    u.Advance(now + 40 * charT);
    EXPECT_EQ(u.GetView().rxCount, 8) << "RTS drops at the trigger level: the peer starts no further byte";
    EXPECT_EQ(u.GetView().overruns, 0u);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(u.Read(kRbr, now + 40 * charT), i);
    u.Advance(now + 60 * charT);
    EXPECT_GT(u.GetView().rxCount, 0) << "reading the FIFO lets the peer go on";
}

TEST_F(Uart16550_Test, ZxWifiAutoCtsHoldsTheTransmitter)
{
    Uart16550 u = Make(Uart16550::Flavor::ZxWifi);
    InitLikeNedoOs(u);
    const uint64_t charT = u.CharacterT();
    peer.cts = false;
    u.Write(kRbr, 'Q', now);
    u.Advance(now + 5 * charT);
    EXPECT_TRUE(peer.fromZx.empty()) << "AFE: the transmitter waits for CTS";
    peer.cts = true;
    u.Advance(now + 6 * charT);   // CTS seen here: the byte starts
    u.Advance(now + 7 * charT - 1);
    EXPECT_TRUE(peer.fromZx.empty());
    u.Advance(now + 7 * charT);
    EXPECT_EQ(peer.fromZx, (std::vector<uint8_t>{'Q'}));
}

TEST_F(Uart16550_Test, TheEvoTransmitterIgnoresCts)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    u.Write(kLcr, 0x03, now);
    peer.cts = false;
    u.Write(kRbr, 'Q', now);
    u.Advance(now + 2 * u.CharacterT());
    EXPECT_EQ(peer.fromZx.size(), 1u) << "software polls MSR.CTS itself (NedoOS type 0)";
    EXPECT_EQ(u.Read(kMsr, now) & 0x10, 0x00);
}

TEST_F(Uart16550_Test, EvoMcrHasNoAfeAndNoLoopback)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    u.Write(kMcr, 0xFF, now);
    EXPECT_EQ(u.Read(kMcr, now), 0x1F);
    u.Write(kLcr, 0x03, now);
    u.Write(kRbr, 'L', now);
    u.Advance(now + 2 * u.CharacterT());
    EXPECT_EQ(peer.fromZx.size(), 1u) << "MCR bit 4 does nothing on the Evo: the byte goes out";
}

TEST_F(Uart16550_Test, ZxWifiLoopbackReturnsTheByteAndTheModemLines)
{
    Uart16550 u = Make(Uart16550::Flavor::ZxWifi);
    u.Write(kFcr, 0x01, now);
    u.Write(kLcr, 0x03, now);
    u.Write(kMcr, 0x13, now);   // loop + RTS + DTR
    EXPECT_EQ(u.Read(kMsr, now) & 0xF0, 0x30) << "RTS -> CTS, DTR -> DSR";
    u.Write(kRbr, 0x5A, now);
    u.Advance(now + 2 * u.CharacterT());
    EXPECT_TRUE(peer.fromZx.empty());
    EXPECT_EQ(u.Read(kRbr, now + 2 * u.CharacterT()), 0x5A);
}

TEST_F(Uart16550_Test, EvoIerIsStoredButRaisesNothing)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    u.Write(kIer, 0xFF, now);
    EXPECT_EQ(u.Read(kIer, now), 0x0F);
    EXPECT_EQ(u.Read(kFcr, now), 0x01);
    EXPECT_FALSE(u.InterruptActive());
}

TEST_F(Uart16550_Test, ZxWifiThreInterruptThroughOut2)
{
    Uart16550 u = Make(Uart16550::Flavor::ZxWifi);
    u.Write(kMcr, 0x08, now);   // OUT2 gates the IRQ on PC-style cards
    u.Write(kIer, 0x02, now);
    EXPECT_EQ(u.Read(kFcr, now) & 0x0F, 0x02) << "THRE pending";
    EXPECT_EQ(u.Read(kFcr, now) & 0x0F, 0x01) << "reading IIR cleared it";
}

TEST_F(Uart16550_Test, CtsChangesSetTheDeltaBitUntilMsrIsRead)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    peer.cts = false;
    u.Advance(now);
    (void)u.Read(kMsr, now);
    peer.cts = true;
    EXPECT_EQ(u.Read(kMsr, now + 1) & 0x11, 0x11);
    EXPECT_EQ(u.Read(kMsr, now + 2) & 0x11, 0x10);
}

TEST_F(Uart16550_Test, StateRoundTripContinuesTheSameWay)
{
    Uart16550 a = Make(Uart16550::Flavor::EvoAvr);
    a.Write(kLcr, 0x03, now);
    a.Write(kMcr, 0x02, now);
    peer.toZx = {1, 2, 3, 4, 5};
    a.Write(kRbr, 0x77, now);
    const uint64_t charT = a.CharacterT();
    a.Advance(now + charT + charT / 2);   // one byte in, one in flight

    Uart16550::State state;
    a.SaveState(state);
    ScriptPeer peerB = peer;
    Uart16550 b(Uart16550::DefaultParams(Uart16550::Flavor::EvoAvr), 3500000);
    b.SetPeer(&peerB);
    b.LoadState(state);

    a.Advance(now + 6 * charT);
    b.Advance(now + 6 * charT);
    EXPECT_EQ(a.GetView().rxCount, b.GetView().rxCount);
    EXPECT_EQ(a.GetView().bytesOut, b.GetView().bytesOut);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(a.Read(kRbr, now + 6 * charT), b.Read(kRbr, now + 6 * charT));
}

TEST_F(Uart16550_Test, TheLineFormatReachesThePeer)
{
    Uart16550 u = Make(Uart16550::Flavor::ZxWifi);
    ASSERT_FALSE(peer.lines.empty()) << "the reset line is announced";
    u.Write(kLcr, 0x80 | 0x1A, now);   // DLAB, 7 data bits, parity even (EPS | PEN)
    u.Write(kRbr, 12, now);            // divisor 12 = 9600
    u.Write(kIer, 0, now);
    u.Write(kLcr, 0x1A | 0x04, now);   // 7E2
    const SerialLine& l = peer.lines.back();
    EXPECT_EQ(l.baud, 9600u);
    EXPECT_EQ(l.dataBits, 7);
    EXPECT_EQ(l.parity, 'E');
    EXPECT_EQ(l.stopBits, 2);
    u.Write(kLcr, 0x08 | 0x20 | 0x03, now);   // stick parity, EPS 0: mark
    EXPECT_EQ(peer.lines.back().parity, 'M');
}

TEST_F(Uart16550_Test, TheEvoLineStarts8N2AndHasNoStickParity)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    EXPECT_EQ(u.Line().stopBits, 2);
    EXPECT_EQ(u.Line().dataBits, 8);
    u.Write(kLcr, 0x08 | 0x20 | 0x03, now);
    EXPECT_EQ(u.Line().parity, 'O') << "the AVR maps PEN alone to odd, stick parity is ignored";
    EXPECT_EQ(u.Line().stopBits, 1);
}

TEST_F(Uart16550_Test, AClockThatRestartsKeepsTheLineMoving)
{
    Uart16550 u = Make(Uart16550::Flavor::EvoAvr);
    u.Write(kLcr, 0x03, 5000000);
    const uint64_t charT = u.CharacterT();
    u.Write(kRbr, 'A', 5000000);
    u.Advance(5000000 + charT / 2);   // half the byte is out
    // A machine reset: the counter starts again from 0
    u.Advance(100);
    EXPECT_TRUE(peer.fromZx.empty());
    u.Advance(100 + charT / 2 + 1);
    EXPECT_EQ(peer.fromZx, (std::vector<uint8_t>{'A'})) << "the rest of the byte, not the whole uptime";
}
