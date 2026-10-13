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

TEST(NextPiPeer_Test, FileReceiveTakesNbnBlocksWithTheirSums)
{
    NextPiPeer pi;
    Send(pi, "nextpi-file_receive -nbn 256\n");
    EXPECT_EQ(Drain(pi), "nextpi-file_receive -nbn 256\r\nOK\r\n");
    // 16384 + 3 bytes: size, blocks, remainder, the name
    const uint32_t size = 16384 + 3;
    const uint8_t header[10] = {0, 0, uint8_t(size >> 8), uint8_t(size), 0, 0, 0, 1, 0, 3};
    for (uint8_t b : header)
        pi.Transmit(b);
    Send(pi, "x.bin");
    pi.Transmit(0);
    uint8_t sum = 0;
    for (unsigned i = 0; i < 16384; i++)
    {
        const uint8_t b = static_cast<uint8_t>(i * 7);
        pi.Transmit(b);
        sum = static_cast<uint8_t>(sum + b);
    }
    pi.Transmit(static_cast<uint8_t>(sum + 1));  // a wrong sum: '?'
    EXPECT_EQ(Drain(pi), "?");
    sum = 0;
    for (unsigned i = 0; i < 16384; i++)
    {
        const uint8_t b = static_cast<uint8_t>(i * 7);
        pi.Transmit(b);
        sum = static_cast<uint8_t>(sum + b);
    }
    pi.Transmit(sum);
    EXPECT_EQ(Drain(pi), "!");
    for (uint8_t b : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(6)})
        pi.Transmit(b);
    EXPECT_EQ(Drain(pi), "!SUP> ");
    ASSERT_EQ(pi.Files().count("x.bin"), 1u);
    EXPECT_EQ(pi.Files().at("x.bin").size(), size);
    EXPECT_EQ(pi.Files().at("x.bin")[16384 + 2], 3);
}

TEST(NextPiPeer_Test, FileTransmitSendsTheHeaderAndTheBlocksOnRequest)
{
    NextPiPeer pi;
    pi.PutFile("/n/a.txt", {'h', 'e', 'l', 'l', 'o'});
    Send(pi, "nextpi-file_transmit -nbn\n");
    Drain(pi);
    pi.Transmit(0);
    Send(pi, "/n/a.txt");
    pi.Transmit(0);
    const std::string header = Drain(pi);
    ASSERT_EQ(header.size(), 10u);
    EXPECT_EQ(static_cast<uint8_t>(header[3]), 5) << "the size";
    EXPECT_EQ(static_cast<uint8_t>(header[9]), 5) << "the remainder";
    pi.Transmit('!');  // ready for the first block
    const std::string block = Drain(pi);
    ASSERT_EQ(block.size(), 6u);
    EXPECT_EQ(block.substr(0, 5), "hello");
    EXPECT_EQ(static_cast<uint8_t>(block[5]), static_cast<uint8_t>('h' + 'e' + 'l' + 'l' + 'o'));
    pi.Transmit('!');
    EXPECT_EQ(Drain(pi), "SUP> ");
    // a missing file: a zero header and the prompt
    Send(pi, "nextpi-file_transmit -nbn\n");
    Drain(pi);
    pi.Transmit(0);
    Send(pi, "nope");
    pi.Transmit(0);
    EXPECT_EQ(Drain(pi), std::string(10, '\0') + "SUP> ");
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
    EXPECT_EQ(Rd(0x98), 0x00) << "a read is the pin: the outputs are disabled, so what the header gives (low)";
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
