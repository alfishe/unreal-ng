// The NextPi supervisor peer and the Next's side of UART 1 (docs/inprogress/2026-10-07-zx-next/research-pi-accelerator.md): the prompt,
// the tty echo, .PIVER's command, the NBN transfers in both directions, the UART's gating by NR #A0 (zxnext.vhd 2333) and the GPIO
// registers' masks and pin read-back
#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>

#include "emulator/io/serial/comportspec.h"
#include "emulator/io/serial/nextpipeer.h"
#include "emulator/io/z80n/nextuart.h"

namespace
{
void Send(NextPiPeer& pi, const std::string& s)
{
    for (char c : s)
        pi.Transmit(static_cast<uint8_t>(c));
}
std::string Drain(NextPiPeer& pi)
{
    std::string s;
    while (pi.HasByte())
        s.push_back(static_cast<char>(pi.TakeByte()));
    return s;
}
}  // namespace

TEST(NextPiPeer_Test, ANewlineGivesThePromptAndTheLineIsEchoed)
{
    NextPiPeer pi;
    Send(pi, "\n");
    EXPECT_EQ(Drain(pi), "\r\nSUP> ");
    Send(pi, "nextpi-admin_version\n");
    EXPECT_EQ(Drain(pi), "nextpi-admin_version\r\n1.93D\r\nSUP> ");
    Send(pi, "frobnicate\n");
    EXPECT_EQ(Drain(pi), "frobnicate\r\nbash: frobnicate: command not found\r\nSUP> ");
    Send(pi, "abc\x03");
    EXPECT_EQ(Drain(pi), "abc^C\r\nSUP> ");
}

// The script nextpi-file_receive of NextPi 1.93D (opt/nextpi/bin, Python 2): OK, the header, a second OK, every block with its sum
// (modulo the -nbn argument) answered '!' or '?', and a final block of `remainder` bytes that has its sum byte even when it is empty
TEST(NextPiPeer_Test, FileReceiveTakesNbnBlocksWithTheirSums)
{
    NextPiPeer pi;
    Send(pi, "nextpi-file_receive -nbn 256\n");
    EXPECT_EQ(Drain(pi), "nextpi-file_receive -nbn 256\r\nOK\n");
    const uint32_t size = 16384 + 3;
    const uint8_t header[10] = {0, 0, uint8_t(size >> 8), uint8_t(size), 0, 0, 0, 1, 0, 3};
    for (uint8_t b : header)
        pi.Transmit(b);
    Send(pi, "x.bin");
    pi.Transmit(0);
    EXPECT_EQ(Drain(pi), "OK\n") << "the header is valid";
    uint8_t sum = 0;
    for (unsigned i = 0; i < 16384; i++)
    {
        const uint8_t b = static_cast<uint8_t>(i * 7);
        pi.Transmit(b);
        sum = static_cast<uint8_t>(sum + b);
    }
    pi.Transmit(static_cast<uint8_t>(sum + 1));  // a wrong sum: '?'
    EXPECT_EQ(Drain(pi), "?");
    for (unsigned i = 0; i < 16384; i++)
        pi.Transmit(static_cast<uint8_t>(i * 7));
    pi.Transmit(sum);
    EXPECT_EQ(Drain(pi), "!");
    for (uint8_t b : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(6)})
        pi.Transmit(b);
    EXPECT_EQ(Drain(pi), "!SUP> ");
    ASSERT_EQ(pi.Files().count("/ram/x.bin"), 1u) << "dirs[1] is /ram/";
    EXPECT_EQ(pi.Files().at("/ram/x.bin").size(), size);
    EXPECT_EQ(pi.Files().at("/ram/x.bin")[16384 + 2], 3);
}

TEST(NextPiPeer_Test, AFileThatIsWholeBlocksStillEndsWithAnEmptyBlockAndItsSum)
{
    NextPiPeer pi;
    Send(pi, "nextpi-file_receive -nbn 256\n");
    Drain(pi);
    const uint8_t header[10] = {0, 0, 0x40, 0, 0, 0, 0, 1, 0, 0};  // 16384 bytes, one block, remainder 0
    for (uint8_t b : header)
        pi.Transmit(b);
    Send(pi, "w");
    pi.Transmit(0);
    Drain(pi);
    for (unsigned i = 0; i < 16384; i++)
        pi.Transmit(1);
    pi.Transmit(static_cast<uint8_t>(16384 % 256));  // 0
    EXPECT_EQ(Drain(pi), "!");
    pi.Transmit(0);  // the empty block's sum
    EXPECT_EQ(Drain(pi), "!SUP> ");
    EXPECT_EQ(pi.Files().at("/ram/w").size(), 16384u);
}

// nextpi-file_transmit: OK, a partition byte (0 "/", 1 "/ram/", 2 "/NextPi/"), the name and 0; size (4), blocks (4), remainder (2) or four
// zero bytes; one byte from the Next; each block with its sum, '!' to go on, a final remainder block even when empty
TEST(NextPiPeer_Test, FileTransmitSendsTheHeaderAndTheBlocksOnRequest)
{
    NextPiPeer pi;
    pi.PutFile("/ram/a.txt", {'h', 'e', 'l', 'l', 'o'});
    Send(pi, "nextpi-file_transmit -nbn\n");
    EXPECT_EQ(Drain(pi), "nextpi-file_transmit -nbn\r\nOK\n");
    pi.Transmit(1);  // /ram/
    Send(pi, "a.txt");
    pi.Transmit(0);
    const std::string header = Drain(pi);
    ASSERT_EQ(header.size(), 10u);
    EXPECT_EQ(static_cast<uint8_t>(header[3]), 5) << "the size";
    EXPECT_EQ(static_cast<uint8_t>(header[7]), 0) << "no whole block";
    EXPECT_EQ(static_cast<uint8_t>(header[9]), 5) << "the remainder";
    pi.Transmit('!');  // the Next's check of the header: the first block follows
    const std::string block = Drain(pi);
    ASSERT_EQ(block.size(), 6u);
    EXPECT_EQ(block.substr(0, 5), "hello");
    EXPECT_EQ(static_cast<uint8_t>(block[5]), static_cast<uint8_t>('h' + 'e' + 'l' + 'l' + 'o'));
    pi.Transmit('!');
    EXPECT_EQ(Drain(pi), "SUP> ");
    // a missing file: four zero bytes
    Send(pi, "nextpi-file_transmit -nbn\n");
    Drain(pi);
    pi.Transmit(0);
    Send(pi, "nope");
    pi.Transmit(0);
    EXPECT_EQ(Drain(pi), std::string(4, '\0') + "SUP> ");
}

TEST(NextPiPeer_Test, TheSpecAcceptsNextPi)
{
    ComPortSpec spec;
    std::string error;
    ASSERT_TRUE(ComPortSpec::Parse("NextPi", spec, error)) << error;
    EXPECT_EQ(spec.kind, ComPortSpec::Kind::NextPi);
    EXPECT_EQ(spec.ToString(), "NEXTPI");
}

TEST(NextUartLine_Test, ADisconnectedLineLosesWhatThePeerSendsAndTheTransmitter)
{
    NextPiPeer pi;
    NextUart uart;
    uart.SetPeer(1, &pi);
    uart.SetLineConnected(1, false);
    Send(pi, "\n");
    uart.Advance(1000000);
    EXPECT_EQ(pi.Pending(), 0u) << "taken and lost: the receiver idles";
    EXPECT_GT(uart.BytesLost(1), 0u);
    uart.SetLineConnected(1, true);
    EXPECT_TRUE(uart.LineConnected(1));
}

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextPiMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    PortDecoder_Next* _ports = nullptr;
    NextPiPeer _pi;
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _ports = dynamic_cast<PortDecoder_Next*>(_emulator->GetContext()->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_ports)
            _ports->Uart().SetPeer(1, nullptr);  // the peer is the fixture's: gone before the emulator
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Nr(uint8_t reg, uint8_t value) { _ports->Board().WriteNextReg(reg, value); }
    uint8_t Rd(uint8_t reg) { return _ports->Board().Read(reg); }
};

TEST_F(NextPiMachine_Test, TheGpioRegistersKeepTheirMasksAndResetValues)
{
    _ports->Board().Reset(true);
    EXPECT_EQ(_ports->Board().Stored(0x98), 0xFF) << "the output latch resets to #FF";
    EXPECT_EQ(Rd(0x98), 0x0C) << "a read is the pin: the outputs are disabled, so what the header gives (low; GPIO 2 / 3 the pulled-up I2C pair)";
    // a read is the pin: disabled outputs show the input (0), enabled ones the written value
    Nr(0x90, 0xFF);
    EXPECT_EQ(Rd(0x90), 0xFC) << "bits 1:0 of GPIO 0-7 are not outputs";
    Nr(0x93, 0xFF);
    EXPECT_EQ(Rd(0x93), 0x0F);
    Nr(0x98, 0xA5);
    EXPECT_EQ(Rd(0x98), 0xA4) << "enabled pins 7:2 show the latch, pins 1:0 stay inputs (low)";
    Nr(0x9B, 0xFF);
    EXPECT_EQ(Rd(0x9B), 0x0F);
    _ports->Board().Reset(false);
    EXPECT_EQ(Rd(0x90), 0x00) << "any reset clears the enables";
}

TEST_F(NextPiMachine_Test, Uart1ReachesThePiOnlyWithNrA0Bits4And5)
{
    _ports->Uart().SetPeer(1, &_pi);
    Nr(0xA0, 0x00);
    _ports->DecodePortOut(0x153B, 0x40, 0);  // select UART 1
    _ports->DecodePortOut(0x133B, '\n', 0);
    _ports->DecodePortIn(0x133B, 0);
    EXPECT_FALSE(_ports->Uart().LineConnected(1));
    Nr(0xA0, 0x30);
    _ports->DecodePortIn(0x133B, 0);  // any access re-syncs the line
    EXPECT_TRUE(_ports->Uart().LineConnected(1));
    Nr(0xA0, 0x10);                   // the "hat" orientation: no Pi on those pins
    _ports->DecodePortIn(0x133B, 0);
    EXPECT_FALSE(_ports->Uart().LineConnected(1));
}

// zxnext.vhd 2292-2392, 6007-6015, 2317: the header's pins under NR #A0 / #A2, the I2S sample NR #2C-#2E read, the EAR threshold, the I2C
// mirror
TEST_F(NextPiMachine_Test, ThePeripheralsOwnTheirPinsAndTheI2sIsReadBack)
{
    NextBoard& board = _ports->Board();
    // UART 1 in the Pi orientation: TX on GPIO15 (driven, idle high), RX on GPIO14 (idle high)
    Nr(0xA0, 0x30);
    uint32_t pins = board.GpioPins();
    EXPECT_NE(pins & (1u << 15), 0u);
    EXPECT_NE(pins & (1u << 14), 0u);
    Nr(0xA0, 0x00);
    // the I2S off: silence #200 -> NR #2C reads #80, NR #2D the low two bits (0) in bits 7:6
    EXPECT_EQ(Rd(0x2C), 0x80);
    EXPECT_EQ(Rd(0x2D), 0x00);
    board.SetI2sSample(0x3FF, 0x000);
    EXPECT_EQ(Rd(0x2C), 0x80) << "off: still silence";
    Nr(0xA2, 0xC0);  // I2S on, both channels, not muted
    EXPECT_EQ(Rd(0x2C), 0xFF);
    EXPECT_EQ(Rd(0x2D), 0xC0) << "the low bits of the sample NR #2C returned last (#3FF: 11)";
    EXPECT_EQ(Rd(0x2E), 0x00);
    Nr(0xA2, 0xC8);  // left muted
    EXPECT_EQ(Rd(0x2C), 0x80);
    Nr(0xA2, 0x80);  // left only: the right takes the left's sample
    EXPECT_EQ(Rd(0x2E), 0xFF);
    // the EAR mode: the samples leave the mix and set / clear the Schmitt trigger on their top two bits
    Nr(0xA2, 0xC1);
    EXPECT_EQ(Rd(0x2C), 0x80) << "EAR mode: the mix input is silence";
    EXPECT_TRUE(board.PiEar()) << "#3FF | #000 on the top bits = 11 sets it";
    board.SetI2sSample(0x200, 0x100);
    EXPECT_TRUE(board.PiEar()) << "10 | 01 = 11 keeps it";
    board.SetI2sSample(0x0FF, 0x0FF);
    EXPECT_FALSE(board.PiEar()) << "00 clears it";
    board.SetI2sSample(0x200, 0x000);
    EXPECT_FALSE(board.PiEar()) << "10 is between the thresholds: it holds";
}

TEST_F(NextPiMachine_Test, ThePiI2cPinsAreAndedIntoTheRtcReadWhenNrA0Bit3IsOn)
{
    NextBoard& board = _ports->Board();
    // nothing holds the pair: released, the read is unchanged
    Nr(0xA0, 0x08);
    EXPECT_EQ(_ports->DecodePortIn(0x113B, 0) & 1, 1);
    // a peer pulling SDA (GPIO 2) low
    board.SetGpioInput([](unsigned bank) -> uint8_t { return bank == 0 ? 0x08 : 0; });
    EXPECT_EQ(_ports->DecodePortIn(0x113B, 0) & 1, 0) << "SDA held low by the Pi";
    EXPECT_EQ(_ports->DecodePortIn(0x103B, 0) & 1, 1) << "SCL is released";
    Nr(0xA0, 0x00);
    EXPECT_EQ(_ports->DecodePortIn(0x113B, 0) & 1, 1) << "the mirror is off";
}

#include "emulator/io/serial/esp/espmodule.h"

// zxnext_top bus_rst_n_io: NR #02 bit 7 holds the expansion bus /RESET net, the ESP's reset is on it ("reset to the expansion bus and the esp wifi",
// registers.txt); a hard reset pulses it, a soft one only with the expansion bus enabled (NR #80 bit 7)
TEST_F(NextPiMachine_Test, TheEspIsResetByNrA02Bit7AndTheResets)
{
    auto* esp = dynamic_cast<EspModule*>(_ports->Uart().Peer(0));
    ASSERT_NE(esp, nullptr) << "the shipped ESP on UART 0";
    const unsigned before = esp->HardwareResets();
    Nr(0x02, 0x80);
    EXPECT_EQ(esp->HardwareResets(), before + 1) << "held: the chip stopped";
    Nr(0x02, 0x00);
    EXPECT_EQ(esp->HardwareResets(), before + 1) << "the release boots it, no second reset";
    _ports->PerformReset(false);
    EXPECT_EQ(esp->HardwareResets(), before + 1) << "a soft reset with the expansion bus off leaves it";
    _ports->PerformReset(true);
    EXPECT_EQ(esp->HardwareResets(), before + 2) << "a hard reset pulses the net";
    Nr(0x80, 0x88);  // enabled now (bit 7) and after a reset (bit 3: a reset copies the low nibble up)
    _ports->PerformReset(false);
    EXPECT_EQ(esp->HardwareResets(), before + 3) << "a soft reset with the expansion bus on";
}
