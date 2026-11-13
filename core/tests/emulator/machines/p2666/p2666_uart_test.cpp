// Pentagon 2.666 LE UART (common/uart.vhd, hardware-reference.md §5.7): the board as built, timed in mainclk edges.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/platforms/p2666/p2666uart.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "pch.h"
#include "stdafx.h"

class P2666Uart_Test : public ::testing::Test
{
protected:
    P2666Uart _uart;
    LoopbackPeer _peer;
    uint64_t _edge = 0;
    bool _int = false;

    void SetUp() override
    {
        _uart.SetClock([this]() { return _edge; });
        _uart.SetIntListener([this](bool held) { _int = held; });
        _uart.Reset();
    }

    /// 8N1 at divisor 2 (115200: 495 x 2 mainclk a bit)
    void Line8N1()
    {
        _uart.Write(3, 0x80);
        _uart.Write(0, 0x02);
        _uart.Write(1, 0x00);
        _uart.Write(3, 0x03);
    }
};

/// T-C6: reset values; THRE is 0 until a byte went out (D-17); MSR reads DCD, DSR set
TEST_F(P2666Uart_Test, ResetValues)
{
    EXPECT_EQ(_uart.Read(5), 0x40) << "TEMT only: THRE is the empty-FIFO interrupt flag, 0 after reset";
    EXPECT_EQ(_uart.Read(2), 0x03) << "IIR: none (bits 3..1 = 001, bit 0 = no INT)";
    EXPECT_EQ(_uart.Read(6), 0xA0) << "MSR: DCD, DSR, CTS pin low (clear)";
    _uart.Write(3, 0x80);
    EXPECT_EQ(_uart.Read(0), 0x01) << "DLL";
    EXPECT_EQ(_uart.Read(1), 0x00) << "DLM";
    EXPECT_EQ(_uart.Baud(), 230303u) << "divisor 1: 114 MHz / 495";
}

/// T-C6: one OUT to the data register fills the TX FIFO (the strobe is a level, D-15); THRE comes when the FIFO
/// empties into the shifter; the bytes leave one frame (10 bits x 990 edges) apart
TEST_F(P2666Uart_Test, DataWriteFillsTheFifo)
{
    _uart.SetPeer(&_peer);
    Line8N1();
    _uart.Write(0, 0x41);
    EXPECT_EQ(_uart.TxFilled(), 15) << "16 copies, the shifter took the first";
    EXPECT_EQ(_uart.Read(5) & 0x60, 0x00);
    const uint64_t frame = 10u * 990u;
    _edge += frame;
    _uart.Sync();
    EXPECT_EQ(_peer.Pending(), 1u);
    _edge += 15 * frame;
    EXPECT_EQ(_uart.Read(5) & 0x60, 0x60) << "THRE and TEMT";
    EXPECT_EQ(_uart.bytesOut, 16u);
}

/// T-C6: loopback through the peer: one IN pops the whole RX FIFO and returns its last byte (D-15)
TEST_F(P2666Uart_Test, DataReadDrainsTheFifo)
{
    _uart.SetPeer(&_peer);
    Line8N1();
    _peer.Transmit(0x11);
    _peer.Transmit(0x22);
    _peer.Transmit(0x33);
    _edge += 3u * 10u * 992u;
    _uart.Sync();
    EXPECT_EQ(_uart.RxFilled(), 3);
    EXPECT_EQ(_uart.Read(5) & 0x01, 0x01) << "data ready";
    EXPECT_EQ(_uart.Read(0), 0x33);
    EXPECT_EQ(_uart.RxFilled(), 0);
    EXPECT_EQ(_uart.Read(5) & 0x01, 0x00);
    EXPECT_EQ(_uart.Read(0), 0x40) << "empty: the bus keeps what the LSR read left";
}

/// T-C6: the RX interrupt (IER bit 0) holds /INT, IIR reads #08 (the 16550's bits 3..1 reversed)
TEST_F(P2666Uart_Test, RxInterruptAndIir)
{
    _uart.SetPeer(&_peer);
    Line8N1();
    _uart.Write(1, 0x01);
    _peer.Transmit(0x5A);
    _edge += 10u * 992u;
    _uart.Sync();
    EXPECT_TRUE(_int);
    EXPECT_EQ(_uart.Read(2), 0x08);
    EXPECT_EQ(_uart.Read(0), 0x5A);
    EXPECT_FALSE(_int);

    _uart.Write(1, 0x02);
    _uart.Write(0, 0x00);
    _edge += 16u * 10u * 990u;
    _uart.Sync();
    EXPECT_TRUE(_int) << "THRE";
    EXPECT_EQ(_uart.Read(2), 0x04);
    EXPECT_FALSE(_int) << "the IIR read clears THRE";
    EXPECT_EQ(_uart.Read(5) & 0x20, 0x00);
}

/// T-C6: the FIFO reset bits stay until the next register write (D-16): bytes arriving meanwhile are lost
TEST_F(P2666Uart_Test, FcrResetBitsAreSticky)
{
    _uart.SetPeer(&_peer);
    Line8N1();
    _uart.Write(2, 0x03);
    _peer.Transmit(0x77);
    _edge += 10u * 992u;
    _uart.Sync();
    EXPECT_EQ(_uart.RxFilled(), 0);
    EXPECT_EQ(_uart.Read(2) & 0xC0, 0xC0) << "IIR bits 7..6 = FCR bit 0";
    _uart.Write(7, 0x00);
    _peer.Transmit(0x78);
    _edge += 10u * 992u;
    _uart.Sync();
    EXPECT_EQ(_uart.RxFilled(), 1);
}

/// T-C6: the decoder: #F8EF..#FFEF by A10..8 (SCR), the INT line on the CPU
TEST(P2666UartPorts_Test, PortsAndIntLine)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoderP2666*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    context->emulatorState.flags &= ~CF_TRDOS;

    decoder->DecodePortOut(0xFFEF, 0xA5, 0x8000);
    EXPECT_EQ(decoder->DecodePortIn(0xFFEF, 0x8000), 0xA5) << "SCR";
    EXPECT_EQ(decoder->DecodePortIn(0xFDEF, 0x8000), 0x40) << "LSR";
    decoder->DecodePortOut(0xF9EF, 0x02, 0x8000);  // IER: THRE
    decoder->DecodePortOut(0xF8EF, 0x00, 0x8000);
    Z80* z80 = context->pCore->GetZ80();
    EXPECT_FALSE(z80->DeviceIntAsserted());
    z80->t += 16u * 10u * 495u;  // 16 frames at divisor 1, in base T (32 edges each): plenty
    decoder->OnFrameEnd();
    EXPECT_TRUE(z80->DeviceIntAsserted()) << "THRE holds /INT";
    (void)decoder->DecodePortIn(0xFAEF, 0x8000);
    EXPECT_FALSE(z80->DeviceIntAsserted());
    EmulatorTestHelper::CleanupEmulator(emulator);
}
