#include <gtest/gtest.h>

#include <cstring>

#include "emulator/io/serial/serialpeer.h"
#include "emulator/io/serial/usart8251.h"

/// @brief The 8251 / KR580VV51A in asynchronous mode, clocked like the ZX Profi v5's: 1.5 MHz / 156 (the 8253's
///        counter 0, ROM BIOS Plus's 9600 baud) with baud factor x1 - one character of 10 bits is 3640 base T
class Usart8251_Test : public ::testing::Test
{
protected:
    Usart8251 usart{3500000};
    uint64_t now = 1000;
    uint32_t divisor = 156;

    static constexpr uint64_t kCharT = 3640;   // 20 half bits x 156 x 3.5 MHz / (2 x 1.5 MHz)

    void SetUp() override
    {
        usart.SetClockSource([this]() { return Usart8251::ClockRate{1500000, divisor}; });
        usart.Reset(now);
    }

    void Control(uint8_t value) { usart.Write(Usart8251::kControl, value, now); }
    void Data(uint8_t value) { usart.Write(Usart8251::kData, value, now); }
    uint8_t Status() { return usart.Read(Usart8251::kControl, now); }
    uint8_t ReadData() { return usart.Read(Usart8251::kData, now); }
    void Wait(uint64_t t)
    {
        now += t;
        usart.Advance(now);
    }
    /// ROM BIOS Plus's setup: 8N1 x1, then TxEN + DTR + RxE + RTS
    void Setup8N1()
    {
        Control(0x4D);
        Control(0x27);
    }
};

/// @brief The internal reset sequence 3 x #00, #40 brings the chip to the mode word from the mode state (#00 is a
///        synchronous mode word with two sync characters) and from the command state alike
TEST_F(Usart8251_Test, InternalResetSequenceFromEitherState)
{
    EXPECT_EQ(usart.GetState().expect, static_cast<uint8_t>(Usart8251::Expect::Mode));
    for (uint8_t v : {0x00, 0x00, 0x00, 0x40})
        Control(v);
    EXPECT_EQ(usart.GetState().expect, static_cast<uint8_t>(Usart8251::Expect::Mode)) << "from the mode state";

    Setup8N1();
    EXPECT_EQ(usart.GetState().expect, static_cast<uint8_t>(Usart8251::Expect::Command));
    for (uint8_t v : {0x00, 0x00, 0x00, 0x40})
        Control(v);
    EXPECT_EQ(usart.GetState().expect, static_cast<uint8_t>(Usart8251::Expect::Mode)) << "from the command state";
    EXPECT_EQ(usart.GetState().command, 0) << "DTR / RTS / TxEN / RxE off";

    // ROM BIOS Plus 0.41h1 resets with 4 x #01 then #40: mode #01, then commands #01 x 3, then IR
    for (uint8_t v : {0x01, 0x01, 0x01, 0x01, 0x40})
        Control(v);
    EXPECT_EQ(usart.GetState().expect, static_cast<uint8_t>(Usart8251::Expect::Mode));
}

/// @brief The mode word: baud factor, character length, parity, stop bits
TEST_F(Usart8251_Test, ModeWordSetsTheLine)
{
    Control(0x4D);   // x1, 8 bits, no parity, 1 stop bit
    EXPECT_EQ(usart.BaudFactor(), 1u);
    EXPECT_EQ(usart.FrameBits(), 10u);
    EXPECT_EQ(usart.Baud(), 9615u);
    EXPECT_EQ(usart.CharacterT(), kCharT);

    Control(0x40);   // IR
    Control(0xFE);   // x16, 8 bits, even parity, 2 stop bits
    EXPECT_EQ(usart.BaudFactor(), 16u);
    EXPECT_EQ(usart.FrameBits(), 12u);
    const SerialLine line = usart.Line();
    EXPECT_EQ(line.dataBits, 8);
    EXPECT_EQ(line.parity, 'E');
    EXPECT_EQ(line.stopBits, 2);
    EXPECT_EQ(line.baud, 1500000u / (156u * 16u) + 1u) << "rounded";

    Control(0x40);
    Control(0x93);   // x64, 5 bits, odd parity, 1.5 stop bits
    EXPECT_EQ(usart.DataBits(), 5);
    EXPECT_EQ(usart.StopHalfBits(), 3);
    EXPECT_EQ(usart.Line().parity, 'O');
    // 2 x (1 + 5 + 1) + 3 = 17 half bits x 64 x 156 x 3.5 MHz / 3 MHz
    EXPECT_EQ(usart.CharacterT(), (17ull * 64 * 156 * 3500000 + 1500000) / 3000000);
}

/// @brief After a reset: TxRDY and TxEMPTY, nothing else (no peer: DSR inactive)
TEST_F(Usart8251_Test, StatusAfterReset)
{
    EXPECT_EQ(Status(), Usart8251::kTxRdy | Usart8251::kTxEmpty);
    Setup8N1();
    EXPECT_EQ(Status(), Usart8251::kTxRdy | Usart8251::kTxEmpty);
}

/// @brief Nothing on the connector: CTS reads inactive, so a byte stays in the buffer (TxRDY 0)
TEST_F(Usart8251_Test, NoPeerHoldsTheTransmitter)
{
    Setup8N1();
    Data(0x41);
    Wait(kCharT * 4);
    EXPECT_EQ(Status() & (Usart8251::kTxRdy | Usart8251::kTxEmpty), 0);
    EXPECT_EQ(usart.GetState().bytesOut, 0u);
}

/// @brief A loopback round trip at 9600 baud: the byte leaves after one character time, comes back one later
TEST_F(Usart8251_Test, LoopbackRoundTripAt9600)
{
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    Setup8N1();
    Data(0x5A);
    EXPECT_EQ(Status() & (Usart8251::kTxRdy | Usart8251::kTxEmpty), Usart8251::kTxRdy)
        << "the byte moved to the shifter: the buffer is free, the transmitter is not empty";

    Wait(kCharT - 1);
    EXPECT_EQ(usart.GetState().bytesOut, 0u);
    Wait(1);
    EXPECT_EQ(usart.GetState().bytesOut, 1u) << "the stop bit left";
    EXPECT_NE(Status() & Usart8251::kTxEmpty, 0);
    EXPECT_EQ(Status() & Usart8251::kRxRdy, 0) << "the echo is on the RX line";

    Wait(kCharT - 1);
    EXPECT_EQ(Status() & Usart8251::kRxRdy, 0);
    Wait(1);
    EXPECT_NE(Status() & Usart8251::kRxRdy, 0);
    EXPECT_EQ(ReadData(), 0x5A);
    EXPECT_EQ(Status() & Usart8251::kRxRdy, 0);
    EXPECT_EQ(usart.GetState().bytesIn, 1u);
}

/// @brief Back to back: a second byte written while the first is on the line follows it at once
TEST_F(Usart8251_Test, BackToBackTransmit)
{
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    Setup8N1();
    Data(0x01);
    Data(0x02);
    EXPECT_EQ(Status() & Usart8251::kTxRdy, 0) << "the buffer holds the second byte";
    Wait(kCharT);
    EXPECT_NE(Status() & Usart8251::kTxRdy, 0);
    Wait(kCharT);
    EXPECT_EQ(usart.GetState().bytesOut, 2u);
    // The first echo is complete; the second is on the line
    EXPECT_EQ(ReadData(), 0x01);
    Wait(kCharT);
    EXPECT_EQ(ReadData(), 0x02);
}

/// @brief A byte that is not read before the next arrives: OE, the new byte replaces it; ER clears the flag
TEST_F(Usart8251_Test, OverrunAndErrorReset)
{
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    Setup8N1();
    Data(0x11);
    Data(0x22);
    Wait(kCharT * 3);
    EXPECT_NE(Status() & Usart8251::kOe, 0);
    EXPECT_EQ(ReadData(), 0x22);
    EXPECT_EQ(usart.GetState().overruns, 1u);
    Control(0x27 | Usart8251::kEr);
    EXPECT_EQ(Status() & Usart8251::kOe, 0);
    EXPECT_EQ(usart.GetState().command, 0x27) << "ER is an action, not kept";
}

/// @brief RTS off: the peer waits with its byte; RTS on lets it start
TEST_F(Usart8251_Test, RtsOffHoldsThePeer)
{
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    Control(0x4D);
    Control(0x07);   // TxEN, DTR, RxE - no RTS
    Data(0x33);
    Wait(kCharT * 3);
    EXPECT_EQ(peer.Pending(), 1u);
    EXPECT_EQ(Status() & Usart8251::kRxRdy, 0);
    Control(0x27);
    Wait(kCharT);
    EXPECT_EQ(ReadData(), 0x33);
}

/// @brief RxE off: characters on the line are lost
TEST_F(Usart8251_Test, ReceiverDisabledDropsBytes)
{
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    Control(0x4D);
    Control(0x23);   // TxEN, DTR, RTS - no RxE
    Data(0x44);
    Wait(kCharT * 3);
    EXPECT_EQ(Status() & Usart8251::kRxRdy, 0);
    EXPECT_EQ(peer.Pending(), 0u);
    EXPECT_EQ(usart.GetState().bytesIn, 0u);
}

/// @brief A loopback test plug: DTR drives DSR (status d7) and DCD, RTS drives CTS and RI (the Profi's TESTCOM.COM
///        plug, PLUSDOC comport.txt)
TEST_F(Usart8251_Test, PlugMirrorsTheModemLines)
{
    LoopbackPeer plug(true);
    usart.SetPeer(&plug);
    Control(0x4D);
    Control(0x00);
    EXPECT_EQ(Status() & Usart8251::kDsr, 0);
    EXPECT_FALSE(usart.CtsIn());
    EXPECT_FALSE(usart.RiIn());
    Control(Usart8251::kDtr);
    EXPECT_NE(Status() & Usart8251::kDsr, 0);
    EXPECT_TRUE(usart.DcdIn());
    Control(Usart8251::kRts);
    EXPECT_TRUE(usart.CtsIn());
    EXPECT_TRUE(usart.RiIn());
    EXPECT_FALSE(usart.DcdIn());
}

/// @brief Seven data bits: the top bit is not sent and not received
TEST_F(Usart8251_Test, SevenDataBits)
{
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    Control(0x49);   // x1, 7 bits, no parity, 1 stop bit
    Control(0x27);
    Data(0xC1);
    Wait(usart.CharacterT() * 2);
    EXPECT_EQ(ReadData(), 0x41);
}

/// @brief No clock (counter 0 not programmed): nothing moves
TEST_F(Usart8251_Test, NoClockNoTransfer)
{
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    divisor = 0;
    Setup8N1();
    Data(0x55);
    Wait(1000000);
    EXPECT_EQ(usart.GetState().bytesOut, 0u);
    EXPECT_EQ(usart.Baud(), 0u);
}

/// @brief Synchronous mode words are taken (with their sync characters) but move no bytes
TEST_F(Usart8251_Test, SynchronousModeIsTakenNotRun)
{
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    Control(0x8C);   // sync, 8 bits, one sync character (SCS)
    EXPECT_EQ(usart.GetState().expect, static_cast<uint8_t>(Usart8251::Expect::Sync1));
    Control(0x16);
    EXPECT_EQ(usart.GetState().expect, static_cast<uint8_t>(Usart8251::Expect::Command));
    Control(0x27);
    Data(0x55);
    Wait(1000000);
    EXPECT_EQ(usart.GetState().bytesOut, 0u);
}

/// @brief The state round-trips (the TTD blob) with a character on each line
TEST_F(Usart8251_Test, StateRoundTrip)
{
    LoopbackPeer peer;
    usart.SetPeer(&peer);
    Setup8N1();
    usart.SetBoardLatch(1);
    Data(0x10);
    Data(0x20);
    Wait(kCharT + 100);

    Usart8251 copy(3500000);
    copy.SetClockSource([this]() { return Usart8251::ClockRate{1500000, divisor}; });
    LoopbackPeer peerCopy;
    copy.SetPeer(&peerCopy);
    copy.SetState(usart.GetState());
    EXPECT_EQ(std::memcmp(&copy.GetState(), &usart.GetState(), sizeof(Usart8251::State)), 0);
    EXPECT_EQ(copy.BoardLatch(), 1);

    now += kCharT * 3;
    usart.Advance(now);
    copy.Advance(now);
    EXPECT_EQ(copy.GetState().bytesOut, usart.GetState().bytesOut);
    EXPECT_EQ(copy.GetState().rxData, usart.GetState().rxData);
}
