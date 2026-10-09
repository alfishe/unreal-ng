// 16550 UART model (network adapters TDD §7.1): both flavors - the ZX-Evo
// AVR's emulation (BaseConf rs232.c) and a real 16550 (ZX-WiFi). Time is
// driven by hand: T-states of a 3.5 MHz machine.

#include <gtest/gtest.h>

#include <cstring>
#include <deque>
#include <vector>

#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/serial/uart16550.h"
#include "emulator/memory/atm/evoavrwait.h"

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
    Uart16550 u = Make(Uart16550::Flavor::Chip16550);
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
    Uart16550 u = Make(Uart16550::Flavor::Chip16550);
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
    Uart16550 u = Make(Uart16550::Flavor::Chip16550);
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
    Uart16550 u = Make(Uart16550::Flavor::Chip16550);
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
    Uart16550 u = Make(Uart16550::Flavor::Chip16550);
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
    Uart16550 u = Make(Uart16550::Flavor::Chip16550);
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
    Uart16550 u = Make(Uart16550::Flavor::Chip16550);
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

/// The clocks are state only while a character is on a line: an idle port a peer clocks every frame saves the same
/// bytes at every time (TTD stores nothing for it per frame), and a port restored from that state continues as the
/// live one - sending across a clock restart
TEST_F(Uart16550_Test, AnIdlePortSavesTheSameStateAsTimePasses)
{
    Uart16550 a = Make(Uart16550::Flavor::EvoAvr);
    a.Write(kLcr, 0x03, now);
    a.Write(kMcr, 0x02, now);
    const uint64_t charT = a.CharacterT();
    a.Write(kRbr, 0x41, now);
    peer.toZx = {9};
    a.Advance(now + 2 * charT);   // the byte out, the peer's byte starts
    a.Advance(now + 4 * charT);   // and is in: the lines are idle
    ASSERT_EQ(a.GetView().rxCount, 1);

    Uart16550::State early{}, late{};   // zero-initialized: the padding compares too
    a.SaveState(early);
    a.Advance(now + 70000);
    a.SaveState(late);
    EXPECT_EQ(std::memcmp(&early, &late, sizeof(early)), 0) << "an idle port's state does not follow the clock";
    EXPECT_EQ(late.lastNow, 0u);

    ScriptPeer peerB;
    Uart16550 b(Uart16550::DefaultParams(Uart16550::Flavor::EvoAvr), 3500000);
    b.SetPeer(&peerB);
    b.LoadState(late);
    for (Uart16550* u : {&a, &b})
    {
        u->Advance(500);   // a machine reset: the counter starts again
        u->Write(kRbr, 0x43, 500);
        u->Advance(500 + charT / 2);
        Uart16550::State busy{};
        u->SaveState(busy);
        EXPECT_EQ(busy.lastNow, 500 + charT / 2) << "a character on the line: the clocks are saved";
        EXPECT_EQ(busy.txDoneAt, 500 + charT);
        u->Advance(500 + charT);
    }
    Uart16550::State liveState{}, restored{};
    a.SaveState(liveState);
    b.SaveState(restored);
    EXPECT_EQ(std::memcmp(&liveState, &restored, sizeof(liveState)), 0);
    EXPECT_EQ(peer.fromZx.back(), 0x43);
    EXPECT_EQ(peerB.fromZx, (std::vector<uint8_t>{0x43})) << "one character time from the restart, as the live port";
}

TEST_F(Uart16550_Test, TheLineFormatReachesThePeer)
{
    Uart16550 u = Make(Uart16550::Flavor::Chip16550);
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

// ZX-Evo AVR firmware presets ([EVO] Avr=; reference-evo-com-port.md §9)

namespace
{
Uart16550 MakeAvr(Uart16550::AvrFirmware firmware, ISerialPeer* peer)
{
    Uart16550 uart(Uart16550::EvoAvrParams(firmware), 3500000);
    uart.SetPeer(peer);
    return uart;
}

/// One access as ComPort::AddAccessWait makes it, without the CPU: the board AVR's wait (timed by the firmware the
/// UART emulates) around the UART's service, in AVR cycles; 0 for a real 16550
uint32_t AvrAccess(EvoAvrWait& wait, const Uart16550& u, uint8_t reg, bool read, uint64_t at)
{
    const Uart16550::Params& p = u.GetParams();
    wait.SetTiming(EvoAvrWait::Timing{p.avrClockHz, p.isrCycles, p.loopCycles, p.waitChecksPerLoop});
    const uint32_t service = u.ServiceCycles(reg, read);
    return service ? wait.Access(service, 0, at, 3500000) : 0;
}

/// Let `chars` characters arrive one character time after another; returns the time after them
uint64_t Receive(Uart16550& u, uint64_t from, int chars)
{
    const uint64_t charT = u.CharacterT();
    for (int i = 0; i <= chars; ++i)
        u.Advance(from + static_cast<uint64_t>(i) * charT);
    return from + static_cast<uint64_t>(chars) * charT;
}
}  // namespace

TEST_F(Uart16550_Test, AvrFirmwareNamesRoundTrip)
{
    for (int f = 0; f <= static_cast<int>(Uart16550::AvrFirmware::Ts2016Apr); ++f)
    {
        const auto firmware = static_cast<Uart16550::AvrFirmware>(f);
        Uart16550::AvrFirmware parsed = Uart16550::kLatestAvr;
        ASSERT_TRUE(Uart16550::ParseAvrFirmware(Uart16550::AvrFirmwareName(firmware), parsed));
        EXPECT_EQ(parsed, firmware);
    }
    Uart16550::AvrFirmware parsed = Uart16550::AvrFirmware::Base2010;
    EXPECT_TRUE(Uart16550::ParseAvrFirmware("baseconf", parsed));
    EXPECT_EQ(parsed, Uart16550::AvrFirmware::Base2023) << "the default: the newest NedoPC firmware";
    EXPECT_TRUE(Uart16550::ParseAvrFirmware("ts", parsed));
    EXPECT_EQ(parsed, Uart16550::AvrFirmware::Ts2016Apr);
    EXPECT_FALSE(Uart16550::ParseAvrFirmware("ts2099", parsed));
}

TEST_F(Uart16550_Test, The2010FirmwareIsARegisterFile)
{
    Uart16550 u = MakeAvr(Uart16550::AvrFirmware::Base2010, &peer);
    peer.toZx = {0x41};
    u.Write(kMcr, 0x02, now);
    u.Write(kRbr, 'A', now);
    u.Advance(now + 100000);
    EXPECT_TRUE(peer.fromZx.empty()) << "no transfer behind the registers";
    EXPECT_EQ(u.Read(kRbr, now + 100000), 0x00);
    EXPECT_EQ(peer.toZx.size(), 1u);

    u.Write(kLsr, 0x1E, now);
    EXPECT_EQ(u.Read(kLsr, now), 0x1E) << "LSR is writable";
    EXPECT_EQ(u.Read(kMsr, now), 0x00) << "MSR resets to 0";
    u.Write(kIer, 0x05, now);
    EXPECT_EQ(u.Read(kIer, now), 0x01) << "register 1 reads the IIR constant";
    u.Write(kFcr, 0x87, now);
    EXPECT_EQ(u.Read(kFcr, now), 0x87) << "register 2 reads the last FCR value";
    EXPECT_EQ(u.Read(kScr, now), 0xFF);
}

TEST_F(Uart16550_Test, BeforeApril2011TheDivisorIsNotReset)
{
    Uart16550 early = MakeAvr(Uart16550::AvrFirmware::Base2011Apr, &peer);
    EXPECT_EQ(early.Baud(), 345600u) << "DLL/DLM 0 after power-on: the divisor-0 rate";
    Uart16550 later = MakeAvr(Uart16550::AvrFirmware::Base2011May, &peer);
    EXPECT_EQ(later.Baud(), 115200u);
}

TEST_F(Uart16550_Test, DlmBit7IsTheAvrDivisorFromMay2011)
{
    for (const auto firmware : {Uart16550::AvrFirmware::Base2011Apr, Uart16550::AvrFirmware::Base2011May})
    {
        Uart16550 u = MakeAvr(firmware, &peer);
        u.Write(kLcr, 0x83, now);
        u.Write(kRbr, 0x05, now);
        u.Write(kIer, 0x80, now);
        u.Write(kLcr, 0x03, now);
        if (firmware == Uart16550::AvrFirmware::Base2011May)
            EXPECT_EQ(u.Baud(), 115200u) << "691200 / (5 + 1)";
        else
            EXPECT_NE(u.Baud(), 115200u) << "the divisor #8005 taken as 115200 / 32773";
    }
}

TEST_F(Uart16550_Test, RtsWasInvertedBeforeSeptember2011)
{
    Uart16550 early = MakeAvr(Uart16550::AvrFirmware::Base2011May, &peer);
    early.Write(kMcr, 0x02, now);
    EXPECT_FALSE(peer.rts) << "MCR bit 1 set drove PD5 high: RTS inactive";
    early.Write(kMcr, 0x00, now);
    EXPECT_TRUE(peer.rts);

    ScriptPeer other;
    Uart16550 fixed = MakeAvr(Uart16550::AvrFirmware::Base2011Sep, &other);
    fixed.Write(kMcr, 0x02, now);
    EXPECT_TRUE(other.rts);
}

TEST_F(Uart16550_Test, OverrunIsClearedByFcrOnlyFrom2013)
{
    for (const auto firmware : {Uart16550::AvrFirmware::Base2011Sep, Uart16550::AvrFirmware::Base2013})
    {
        ScriptPeer p;
        Uart16550 u = MakeAvr(firmware, &p);
        u.Write(kLcr, 0x03, now);
        u.Write(kMcr, 0x02, now);
        p.toZx.assign(17, 0x55);
        const uint64_t later = Receive(u, now, 17);
        ASSERT_NE(u.Read(kLsr, later) & Uart16550::kLsrOe, 0) << "16 bytes fit, the 17th overruns";
        u.Write(kFcr, 0x03, later);
        const bool cleared = (u.Read(kLsr, later) & Uart16550::kLsrOe) == 0;
        EXPECT_EQ(cleared, firmware == Uart16550::AvrFirmware::Base2013);
    }
}

TEST_F(Uart16550_Test, HalfFullBitOnlyFrom2023)
{
    for (const auto firmware : {Uart16550::AvrFirmware::Base2013, Uart16550::AvrFirmware::Base2023})
    {
        ScriptPeer p;
        Uart16550 u = MakeAvr(firmware, &p);
        u.Write(kLcr, 0x03, now);
        u.Write(kMcr, 0x02, now);
        p.toZx.assign(8, 0x55);
        const uint64_t later = Receive(u, now, 8);
        const bool hf = (u.Read(kLsr, later) & Uart16550::kLsrHalfFull) != 0;
        EXPECT_EQ(hf, firmware == Uart16550::AvrFirmware::Base2023);
    }
}

TEST_F(Uart16550_Test, TsFifoDepthsByRelease)
{
    struct Row
    {
        Uart16550::AvrFirmware firmware;
        int rx;
    };
    for (const Row row : {Row{Uart16550::AvrFirmware::Ts2013, 256}, Row{Uart16550::AvrFirmware::Ts2016Feb, 16},
                          Row{Uart16550::AvrFirmware::Ts2016Apr, 511}})
    {
        ScriptPeer p;
        Uart16550 u = MakeAvr(row.firmware, &p);
        u.Write(kLcr, 0x03, now);
        u.Write(kMcr, 0x02, now);
        p.toZx.assign(static_cast<size_t>(row.rx + 1), 0x55);
        const uint64_t later = Receive(u, now, row.rx + 1);
        EXPECT_EQ(u.GetView().rxCount, row.rx);
        EXPECT_NE(u.Read(kLsr, later) & Uart16550::kLsrOe, 0) << "one more byte than the ring holds";
    }
}

TEST_F(Uart16550_Test, Ts2016AprThreMeansRoomAndTemtIsTheUsartTxc)
{
    Uart16550 u = MakeAvr(Uart16550::AvrFirmware::Ts2016Apr, &peer);
    EXPECT_EQ(u.Read(kLsr, now), 0x20) << "TEMT = TXC: 0 after power-on";
    u.Write(kLcr, 0x03, now);
    u.Write(kRbr, 'A', now);
    u.Write(kRbr, 'B', now);
    EXPECT_EQ(u.Read(kLsr, now) & 0x20, 0x20) << "THRE: the TX ring has room";
    u.Advance(now + 3 * u.CharacterT());
    EXPECT_EQ(u.Read(kLsr, now + 3 * u.CharacterT()) & 0x60, 0x60) << "a byte went out: TXC set, and it stays";
    EXPECT_EQ(peer.fromZx, (std::vector<uint8_t>{'A', 'B'}));

    u.Write(kLcr, 0x83, now);
    u.Write(kRbr, 0x00, now);
    u.Write(kIer, 0x00, now);
    u.Write(kLcr, 0x03, now);
    EXPECT_EQ(u.Baud(), 230400u) << "TS 2016-04: divisor 0 = 230400";
}

TEST_F(Uart16550_Test, Ts2016AprPicksTheWaitUpAtTheNextTask)
{
    // TS-AVR main.c:414-431 (since 9a3b541b, 2016-03): waittask() after each of the 8 tasks, so a polling loop
    // right behind its release waits an eighth of a pass, not a whole one (the ZiFi plugins' INIR bursts)
    const Uart16550::Params p = Uart16550::EvoAvrParams(Uart16550::AvrFirmware::Ts2016Apr);
    EXPECT_EQ(p.waitChecksPerLoop, 8);
    Uart16550 u = MakeAvr(Uart16550::AvrFirmware::Ts2016Apr, &peer);
    EvoAvrWait wait;
    const uint32_t first = AvrAccess(wait, u, Uart16550::kLsr, true, 1000000);
    const uint64_t release = 1000000 + (static_cast<uint64_t>(first) * 3500000 + 11059199) / 11059200;
    const uint32_t polled = AvrAccess(wait, u, Uart16550::kLsr, true, release);
    const uint32_t task = p.loopCycles / 8;
    EXPECT_LE(polled, p.isrCycles + task + p.serviceRead);
    EXPECT_GE(polled, p.isrCycles + task + p.serviceRead - 4);
    EXPECT_EQ(Uart16550::EvoAvrParams(Uart16550::AvrFirmware::Base2023).waitChecksPerLoop, 1) << "BaseConf: once per pass";
}

TEST_F(Uart16550_Test, AvrWaitIsInterruptPlusLoopPhasePlusService)
{
    const Uart16550::Params p = Uart16550::EvoAvrParams(Uart16550::kLatestAvr);
    Uart16550 u = MakeAvr(Uart16550::kLatestAvr, &peer);
    EvoAvrWait wait;
    // Long after the last access: the loop is somewhere in its pass
    const uint32_t minimum = static_cast<uint32_t>(p.isrCycles) + p.serviceRead;
    const uint32_t wholePass = minimum + p.loopCycles;
    const uint32_t first = AvrAccess(wait, u, Uart16550::kLsr, true, 1000000);
    EXPECT_GT(first, minimum);
    EXPECT_LE(first, wholePass);
    // Right behind the release: the loop starts its pass again, a whole pass to wait
    const uint64_t release = 1000000 + (static_cast<uint64_t>(first) * 3500000 + 11059199) / 11059200;
    const uint32_t polled = AvrAccess(wait, u, Uart16550::kLsr, true, release);
    EXPECT_GE(polled, wholePass - 4);
    EXPECT_LE(polled, wholePass);
    // The services differ by access
    Uart16550 v = MakeAvr(Uart16550::kLatestAvr, &peer);
    EvoAvrWait waitV;
    const uint32_t write = AvrAccess(waitV, v, Uart16550::kScr, false, 2000000);
    Uart16550 w = MakeAvr(Uart16550::kLatestAvr, &peer);
    EvoAvrWait waitW;
    const uint32_t rbr = AvrAccess(waitW, w, Uart16550::kRbrThr, true, 2000000);
    EXPECT_EQ(rbr - write, static_cast<uint32_t>(p.serviceRbr - p.serviceWrite)) << "same phase, different service";

    Uart16550 chip = Make(Uart16550::Flavor::Chip16550);
    EvoAvrWait waitChip;
    EXPECT_EQ(AvrAccess(waitChip, chip, Uart16550::kLsr, true, now), 0u) << "a real 16550 holds nobody";
}

/// An RS-232 loopback test plug (ComPort=PLUG): bytes come back, the UART's own RTS drives CTS and DTR drives DSR
/// and DCD, RI stays open; plain LOOPBACK holds the inputs active and no peer at all reads them inactive
/// (SprinterSerial COM1 with nothing plugged in, owner decision 2026-10-04)
TEST(Uart16550Plug_Test, LoopbackPlugWiresRtsToCtsAndDtrToDsrDcd)
{
    constexpr uint64_t now = 1000;
    const uint8_t lines = Uart16550::kMsrCts | Uart16550::kMsrDsr | Uart16550::kMsrRi | Uart16550::kMsrDcd;

    LoopbackPeer plug(true);
    Uart16550 u(Uart16550::DefaultParams(Uart16550::Flavor::Chip16550), 3500000);
    u.SetPeer(&plug);
    EXPECT_STREQ(plug.Kind(), "plug");
    EXPECT_EQ(u.Read(kMsr, now) & lines, 0) << "RTS and DTR low: CTS, DSR, DCD inactive";

    u.Write(kMcr, Uart16550::kMcrRts, now);
    EXPECT_EQ(u.Read(kMsr, now) & lines, Uart16550::kMsrCts) << "RTS -> CTS";

    u.Write(kMcr, Uart16550::kMcrDtr, now);
    EXPECT_EQ(u.Read(kMsr, now) & lines, Uart16550::kMsrDsr | Uart16550::kMsrDcd) << "DTR -> DSR and DCD";

    u.Write(kMcr, Uart16550::kMcrRts | Uart16550::kMcrDtr | Uart16550::kMcrOut1, now);
    EXPECT_EQ(u.Read(kMsr, now) & lines, Uart16550::kMsrCts | Uart16550::kMsrDsr | Uart16550::kMsrDcd)
        << "OUT1 is not on the connector: RI stays open";

    // Auto-CTS (AFE): the transmitter follows the looped RTS (115200 8N1, FIFOs on)
    u.Write(kLcr, 0x83, now);
    u.Write(kRbr, 0x01, now);
    u.Write(kLcr, 0x03, now);
    u.Write(kFcr, 0x07, now);
    const uint64_t charT = u.CharacterT();
    u.Write(kMcr, Uart16550::kMcrAfe | Uart16550::kMcrDtr, now);
    u.Write(kRbr, 0x5A, now);
    u.Advance(now + 5 * charT);
    EXPECT_EQ(u.Read(kLsr, now + 5 * charT) & 0x41, 0x00) << "RTS low: the plug holds CTS low, nothing is sent";
    u.Write(kMcr, Uart16550::kMcrAfe | Uart16550::kMcrDtr | Uart16550::kMcrRts, now + 5 * charT);
    u.Advance(now + 6 * charT);    // CTS seen here: the byte starts
    u.Advance(now + 7 * charT);    // ...leaves the TX pin into the plug and starts back on RX
    u.Advance(now + 8 * charT);    // ...and arrives
    EXPECT_EQ(u.Read(kLsr, now + 8 * charT) & 0x01, 0x01) << "RTS high: the byte goes out and comes back";
    EXPECT_EQ(u.Read(kRbr, now + 8 * charT), 0x5A);

    LoopbackPeer echo;
    Uart16550 e(Uart16550::DefaultParams(Uart16550::Flavor::Chip16550), 3500000);
    e.SetPeer(&echo);
    EXPECT_EQ(e.Read(kMsr, now) & lines, Uart16550::kMsrCts | Uart16550::kMsrDsr | Uart16550::kMsrDcd)
        << "LOOPBACK holds CTS, DSR and DCD active whatever RTS and DTR do";

    Uart16550 open(Uart16550::DefaultParams(Uart16550::Flavor::Chip16550), 3500000);
    open.Write(kMcr, Uart16550::kMcrRts | Uart16550::kMcrDtr, now);
    EXPECT_EQ(open.Read(kMsr, now) & lines, 0) << "nothing plugged in: every input reads inactive";
}

