// NextDma (core/src/emulator/io/z80n/nextdma.h): the WR0-WR6 command sequencer, memory and I/O transfers with the three
// address modes, the zxnDMA / Z80-DMA counter difference, the read sequence, auto restart and the burst prescaler.
// Source: research-fpga-vhdl.md section 13 (device/dma.vhd).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "emulator/io/z80n/nextdma.h"

class NextDma_Test : public ::testing::Test
{
protected:
    NextDma _dma;
    std::array<uint8_t, 65536> _memory{};
    std::vector<std::pair<uint16_t, uint8_t>> _ioWrites;
    uint8_t _ioValue = 0x5A;

    void SetUp() override
    {
        _dma.Reset();
        NextDma::Bus bus;
        bus.readMemory = [this](uint16_t a) { return _memory[a]; };
        bus.writeMemory = [this](uint16_t a, uint8_t v) { _memory[a] = v; };
        bus.readIo = [this](uint16_t) { return _ioValue; };
        bus.writeIo = [this](uint16_t p, uint8_t v) { _ioWrites.push_back({p, v}); };
        _dma.SetBus(bus);
    }
    void Send(std::initializer_list<uint8_t> bytes, bool z80 = false)
    {
        for (uint8_t b : bytes)
            _dma.Write(b, z80);
    }
    /// WR0 (A->B, A address, length), WR1/WR2 (memory, increment), WR4 (mode), load, enable
    void MemoryCopy(uint16_t from, uint16_t to, uint16_t length, uint8_t wr4 = 0xAD /* continuous: 01 in 6:5, B address low and high follow */)
    {
        Send({0x7D, static_cast<uint8_t>(from & 0xFF), static_cast<uint8_t>(from >> 8), static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>(length >> 8)});
        Send({0x14, 0x10});  // WR1: port A memory, increment; WR2: port B memory, increment (0x10 = bits 5:4 = 01)
        Send({wr4, static_cast<uint8_t>(to & 0xFF), static_cast<uint8_t>(to >> 8)});
        Send({0xCF, 0x87});  // load, enable
    }
};

TEST_F(NextDma_Test, ContinuousMemoryCopyMovesTheBlockAndEndsWithTheStatusBit)
{
    for (unsigned i = 0; i < 16; i++)
        _memory[0x8000 + i] = static_cast<uint8_t>(0x10 + i);
    MemoryCopy(0x8000, 0x9000, 16);
    ASSERT_TRUE(_dma.Active());
    EXPECT_EQ(_dma.Run(256, 0), 16u);
    EXPECT_FALSE(_dma.Active());
    for (unsigned i = 0; i < 16; i++)
        EXPECT_EQ(_memory[0x9000 + i], 0x10 + i);
    EXPECT_EQ(_memory[0x9010], 0) << "exactly the block";
    Send({0xBF});  // read the status byte
    const uint8_t status = _dma.Read();
    EXPECT_EQ(status & 0x20, 0) << "end of block: bit 5 low";
    EXPECT_EQ(status & 1, 0) << "bit 0 (a byte moved) is never set by the core (the board reads 1A / 3A: zilogDMA.txt)";
}

TEST_F(NextDma_Test, FixedSourceFillsAndDecrementRunsBackwards)
{
    _memory[0x8000] = 0xAA;
    // WR1: port A fixed (bits 5:4 = 10) ; WR2: port B decrement (00)
    Send({0x7D, 0x00, 0x80, 0x04, 0x00});
    Send({0x24, 0x00});
    Send({0xAD, 0x04, 0x90});  // wr4: B address 0x9004
    Send({0xCF, 0x87});
    EXPECT_EQ(_dma.Run(256, 0), 4u);
    EXPECT_EQ(_memory[0x9004], 0xAA);
    EXPECT_EQ(_memory[0x9001], 0xAA);
    EXPECT_EQ(_memory[0x9000], 0) << "four bytes, going down from #9004";
}

TEST_F(NextDma_Test, ZxnCounterStartsAtZeroAndZ80CompatibleAtMinusOne)
{
    MemoryCopy(0x8000, 0x9000, 4);
    EXPECT_EQ(_dma.Counter(), 0);
    NextDma compat;
    compat.Write(0xCF, true);  // LOAD through port #0B
    EXPECT_EQ(compat.Counter(), 0xFFFF);
}

TEST_F(NextDma_Test, ReadSequenceFollowsTheReadMask)
{
    MemoryCopy(0x8000, 0x9000, 3);
    _dma.Run(256, 0);
    Send({0xBB, 0x06});  // read mask: counter low and high only
    EXPECT_EQ(_dma.Read(), 3);
    EXPECT_EQ(_dma.Read(), 0);
    EXPECT_EQ(_dma.Read(), 3) << "and round again";
}

TEST_F(NextDma_Test, IoPortAsDestination)
{
    _memory[0x8000] = 0x11;
    _memory[0x8001] = 0x22;
    Send({0x7D, 0x00, 0x80, 0x02, 0x00});
    Send({0x14});          // port A: memory, increment
    Send({0x28});          // port B: I/O (bit 3), fixed (bits 5:4 = 10)... 0x28 = 0010 1000
    Send({0xAD, 0x5B, 0x00});
    Send({0xCF, 0x87});
    EXPECT_EQ(_dma.Run(256, 0), 2u);
    ASSERT_EQ(_ioWrites.size(), 2u);
    EXPECT_EQ(_ioWrites[0], std::make_pair(uint16_t(0x005B), uint8_t(0x11)));
    EXPECT_EQ(_ioWrites[1], std::make_pair(uint16_t(0x005B), uint8_t(0x22)));
}

TEST_F(NextDma_Test, AutoRestartReloadsAndKeepsRunning)
{
    _memory[0x8000] = 1;
    MemoryCopy(0x8000, 0x9000, 1);
    Send({0xA2});  // WR5: bit 5 = auto restart
    EXPECT_TRUE(_dma.AutoRestart());
    EXPECT_EQ(_dma.Run(1, 0), 1u);
    EXPECT_TRUE(_dma.Active()) << "auto restart: the block is loaded again";
    EXPECT_EQ(_dma.Source(), 0x8000);
}

TEST_F(NextDma_Test, BurstModeMovesOneByteAndThePrescalerPacesTheNextOnes)
{
    for (unsigned i = 0; i < 4; i++)
        _memory[0x8000 + i] = static_cast<uint8_t>(i + 1);
    Send({0x7D, 0x00, 0x80, 0x04, 0x00});
    Send({0x14});
    Send({0x50, 0x20, 0x02});  // WR2: port B memory increment, a timing byte follows (bit 6); it says a prescaler follows (bit 5): 2
    Send({0xCD, 0x00, 0x90});  // WR4 burst (bits 6:5 = 10), B address
    Send({0xCF, 0x87});
    EXPECT_EQ(_dma.Run(256, 0), 1u) << "burst: one byte";
    EXPECT_TRUE(_dma.Waiting());
    EXPECT_EQ(_dma.Run(256, 10), 0u) << "the prescaler period (2 * 32 clocks) has not passed";
    EXPECT_EQ(_dma.Run(256, 64), 1u);
}

TEST_F(NextDma_Test, DisableAndResetStopTheTransfer)
{
    MemoryCopy(0x8000, 0x9000, 100);
    Send({0x83});
    EXPECT_FALSE(_dma.Active());
    Send({0x87});
    EXPECT_TRUE(_dma.Active());
    Send({0xC3});
    EXPECT_FALSE(_dma.Active());
}

// ---- the audit of 2026-10-09 against dma.vhd (core 3.02.03), jnext, ZEsarUX and MAME ----

// dma.vhd WRITE_4 checks the prescaler gate BEFORE the block counter: after the last byte the DMA still waits the whole period; the
// end-of-block flag, the stop and the auto-restart come after it (so the byte spacing survives an auto-restart seam)
TEST_F(NextDma_Test, TheLastBytesPrescalerPeriodIsWaitedBeforeTheBlockEnds)
{
    Send({0x7D, 0x00, 0x80, 0x02, 0x00});
    Send({0x14});
    Send({0x50, 0x20, 0x02});  // prescaler 2: 64 clocks of 28 MHz between bytes
    Send({0xCD, 0x00, 0x90});
    Send({0xCF, 0x87});
    EXPECT_EQ(_dma.Run(256, 0), 1u);
    EXPECT_EQ(_dma.Run(256, 64), 1u) << "the last byte";
    EXPECT_TRUE(_dma.Active()) << "its period is still to be waited";
    EXPECT_FALSE(_dma.EndOfBlock());
    EXPECT_TRUE(_dma.Waiting());
    EXPECT_EQ(_dma.Run(256, 127), 0u);
    EXPECT_TRUE(_dma.Active());
    EXPECT_EQ(_dma.Run(256, 128), 0u);
    EXPECT_FALSE(_dma.Active()) << "the period is over: the block ends";
    EXPECT_TRUE(_dma.EndOfBlock());
}

TEST_F(NextDma_Test, AutoRestartKeepsTheByteSpacingAcrossTheSeam)
{
    Send({0x7D, 0x00, 0x80, 0x01, 0x00});
    Send({0x14});
    Send({0x50, 0x20, 0x02});
    Send({0xCD, 0x00, 0x90});
    Send({0xA2});  // WR5: auto restart
    Send({0xCF, 0x87});
    EXPECT_EQ(_dma.Run(256, 0), 1u);
    EXPECT_EQ(_dma.Run(256, 1), 0u);
    EXPECT_EQ(_dma.Run(256, 63), 0u);
    EXPECT_EQ(_dma.Run(256, 64), 0u) << "the seam is crossed";
    EXPECT_EQ(_dma.Run(256, 64), 1u) << "the next pass's first byte";
}

// An auto-restart does not clear the end-of-block flag (only LOAD, CONTINUE, 0x8B and a reset do): the status reads 0x1A
TEST_F(NextDma_Test, AutoRestartLeavesTheEndOfBlockFlagSet)
{
    Send({0x7D, 0x00, 0x80, 0x01, 0x00});
    Send({0x14, 0x10});
    Send({0xAD, 0x00, 0x90});
    Send({0xA2});
    Send({0xCF, 0x87});
    EXPECT_EQ(_dma.Run(1, 0), 1u);
    Send({0xBF});
    EXPECT_EQ(_dma.Read(), 0x1B) << "the end-of-block bit (bit 5, active low) stays 0 = ended; bit 0: the running restart has moved a byte";
}

// Only the burst mode (WR4 bits 6:5 = 10) releases the bus while it waits; continuous mode keeps the CPU stopped through the wait
TEST_F(NextDma_Test, OnlyBurstModeReleasesTheBusInThePrescalerWait)
{
    Send({0x7D, 0x00, 0x80, 0x04, 0x00});
    Send({0x14});
    Send({0x50, 0x20, 0x02});
    Send({0xAD, 0x00, 0x90});  // continuous
    Send({0xCF, 0x87});
    EXPECT_EQ(_dma.Run(256, 0), 1u);
    EXPECT_TRUE(_dma.Waiting());
    EXPECT_TRUE(_dma.HoldsBus()) << "continuous: the CPU stays stopped";
    EXPECT_EQ(_dma.WaitEnd(), 64u);
    Send({0xCD});  // burst
    EXPECT_FALSE(_dma.HoldsBus());
}

// A reset (hard or soft) puts back what the reset block of dma.vhd names and keeps the programmed addresses, length and direction
TEST_F(NextDma_Test, ResetKeepsTheAddressesAndClearsTheRest)
{
    Send({0x7D, 0x34, 0x12, 0x20, 0x00});
    Send({0x14});
    Send({0x50, 0x20, 0x05});
    Send({0xCD, 0x78, 0x56});
    Send({0xA2});
    Send({0xBB, 0x02});
    Send({0xCF, 0x87});
    ASSERT_TRUE(_dma.Active());
    _dma.Reset();
    EXPECT_FALSE(_dma.Active());
    EXPECT_EQ(_dma.Prescaler(), 0);
    EXPECT_EQ(_dma.Mode(), 1);
    EXPECT_FALSE(_dma.AutoRestart());
    EXPECT_EQ(_dma.BlockLength(), 0x20) << "the block length is not reset";
    Send({0xCF});  // load: the addresses are still there
    EXPECT_EQ(_dma.Source(), 0x1234);
    EXPECT_EQ(_dma.Destination(), 0x5678);
    EXPECT_EQ(_dma.Read(), 0x3A) << "the read sequence is back at the status (default mask #7F), the end-of-block flag clear";
}

// Status bit 0 ("at least one byte moved") reads 1 while the DMA is not idle after a byte, 0 once it has finished (the board reads 1A / 3A)
TEST_F(NextDma_Test, StatusBitZeroIsSetBetweenTheBytesOfARunningBlock)
{
    Send({0x7D, 0x00, 0x80, 0x03, 0x00});
    Send({0x14});
    Send({0x50, 0x20, 0x02});
    Send({0xCD, 0x00, 0x90});
    Send({0xCF, 0x87});
    EXPECT_EQ(_dma.Run(256, 0), 1u);
    Send({0xBF});
    EXPECT_EQ(_dma.Read(), 0x3B);
    _dma.Run(256, 64);
    _dma.Run(256, 128);
    _dma.Run(256, 192);
    _dma.Run(256, 256);
    Send({0xBF});
    EXPECT_FALSE(_dma.Active());
    EXPECT_EQ(_dma.Read() & 1, 0);
}

// Any access of either port sets the zxn / Z80 mode latch: the auto-restart reload takes the mode of the LAST access
TEST_F(NextDma_Test, AReadThroughEitherPortSetsTheModeLatch)
{
    Send({0x7D, 0x00, 0x80, 0x01, 0x00}, true);  // programmed through #0B
    Send({0x14, 0x10}, true);
    Send({0xAD, 0x00, 0x90}, true);
    Send({0xA2, 0xCF, 0x87}, true);
    _dma.ReadAs(false);  // a read through #6B: the latch is zxn again
    _dma.Run(1, 0);      // the block ends, the auto-restart reloads
    EXPECT_EQ(_dma.Counter(), 0) << "zxn mode counts from 0, not from #FFFF";
}
