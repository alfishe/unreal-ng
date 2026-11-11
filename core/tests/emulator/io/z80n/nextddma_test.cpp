// zxnDDMA (core 3.03.00, docs/inprogress/2026-10-07-zx-next/design-zxnddma.md): the blitter commands of NextDma on port #6B:
// physical 22-bit addresses, 2D rectangles with strides, transparent colour, fill, the Layer 2 X,Y destination.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "emulator/io/z80n/nextdma.h"

class NextDdma_Test : public ::testing::Test
{
protected:
    NextDma _dma;
    std::vector<uint8_t> _sram = std::vector<uint8_t>(0x400000, 0);
    std::vector<uint8_t> _z80 = std::vector<uint8_t>(0x10000, 0);
    uint8_t _bank = 9, _resolution = 0;

    void SetUp() override
    {
        _dma.Reset();
        _dma.SetExtended(true);
        NextDma::Bus bus;
        bus.readMemory = [this](uint16_t a) { return _z80[a]; };
        bus.writeMemory = [this](uint16_t a, uint8_t v) { _z80[a] = v; };
        bus.readPhysical = [this](uint32_t a) { return _sram[a]; };
        bus.writePhysical = [this](uint32_t a, uint8_t v) { _sram[a] = v; };
        bus.layer2 = [this](bool, uint8_t& bank, uint8_t& res) { bank = _bank; res = _resolution; };
        _dma.SetBus(bus);
    }
    void Send(std::initializer_list<uint8_t> bytes, bool z80 = false)
    {
        for (uint8_t b : bytes)
            _dma.Write(b, z80);
    }
    /// A to B, memory both, increment; A = from, B = to (low 16 bits), length
    void Program(uint16_t from, uint16_t to, uint16_t length)
    {
        Send({0x7D, static_cast<uint8_t>(from), static_cast<uint8_t>(from >> 8), static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8)});
        Send({0x14, 0x10});
        Send({0xAD, static_cast<uint8_t>(to), static_cast<uint8_t>(to >> 8)});
    }
    void Go()
    {
        Send({0xCF, 0x87});
        while (_dma.Active())
            _dma.Run(4096, 0);
    }
};

TEST_F(NextDdma_Test, PhysicalCopyUsesTheExtendedAddressBytes)
{
    for (unsigned i = 0; i < 8; i++)
        _sram[0x142000 + i] = static_cast<uint8_t>(0x30 + i);
    Program(0x2000, 0x8000, 8);
    Send({0xD7, 0x14, 0xDB, 0x2A, 0xDF, 0x01});
    Go();
    for (unsigned i = 0; i < 8; i++)
        EXPECT_EQ(_sram[0x2A8000 + i], 0x30 + i);
    EXPECT_EQ(_z80[0x8000], 0) << "the Z80 memory is not touched";
}

TEST_F(NextDdma_Test, PhysicalAddressCarriesThroughTheExtendedByte)
{
    _sram[0x0FFFF] = 0x11;
    _sram[0x10000] = 0x22;
    Program(0xFFFF, 0x0100, 2);
    Send({0xDF, 0x01});
    Go();
    EXPECT_EQ(_sram[0x100], 0x11);
    EXPECT_EQ(_sram[0x101], 0x22) << "the source ran on into the next 64K";
}

TEST_F(NextDdma_Test, WithoutPhysicalModeTheAddressWrapsIn64K)
{
    _z80[0xFFFF] = 0x11;
    _z80[0x0000] = 0x22;
    Program(0xFFFF, 0x0100, 2);
    Send({0xD7, 0x05});
    Go();
    EXPECT_EQ(_z80[0x100], 0x11);
    EXPECT_EQ(_z80[0x101], 0x22);
}

TEST_F(NextDdma_Test, TwoDimensionalCopyUsesRowLengthRowsAndStrides)
{
    // 4x3 rectangle, source rows 16 apart, destination rows 32 apart
    for (unsigned r = 0; r < 3; r++)
        for (unsigned c = 0; c < 4; c++)
            _z80[0x8000 + r * 16 + c] = static_cast<uint8_t>(r * 16 + c + 1);
    Program(0x8000, 0xA000, 999);  // the block length is ignored in 2D
    Send({0xDF, 0x02, 0xE3, 4, 0, 0xE7, 3, 0, 0xEB, 16, 0, 0xEF, 32, 0});
    Go();
    for (unsigned r = 0; r < 3; r++)
        for (unsigned c = 0; c < 4; c++)
            EXPECT_EQ(_z80[0xA000 + r * 32 + c], r * 16 + c + 1) << r << "," << c;
    EXPECT_EQ(_z80[0xA004], 0) << "outside the row";
    EXPECT_EQ(_z80[0xA000 + 3 * 32], 0) << "past the last row";
}

TEST_F(NextDdma_Test, TransparentValueIsNotWritten)
{
    for (unsigned i = 0; i < 4; i++)
    {
        _z80[0x8000 + i] = i == 1 ? 0xE3 : static_cast<uint8_t>(0x40 + i);
        _z80[0x9000 + i] = 0xEE;
    }
    Program(0x8000, 0x9000, 4);
    Send({0xDF, 0x04, 0xF3, 0xE3});
    Go();
    EXPECT_EQ(_z80[0x9000], 0x40);
    EXPECT_EQ(_z80[0x9001], 0xEE) << "kept";
    EXPECT_EQ(_z80[0x9002], 0x42);
}

TEST_F(NextDdma_Test, FillWritesTheValueWithoutReadingAndIsFaster)
{
    unsigned reads = 0, advance = 0;
    NextDma::Bus bus;
    bus.readMemory = [&](uint16_t a) { reads++; return _z80[a]; };
    bus.writeMemory = [this](uint16_t a, uint8_t v) { _z80[a] = v; };
    bus.advance = [&](unsigned n) { advance += n; };
    _dma.SetBus(bus);
    Program(0x8000, 0x9000, 5);
    Send({0xDF, 0x08, 0xF7, 0x77});
    Go();
    for (unsigned i = 0; i < 5; i++)
        EXPECT_EQ(_z80[0x9000 + i], 0x77);
    EXPECT_EQ(reads, 0u);
    EXPECT_EQ(advance, 5u * 4) << "one write cycle set per byte: 5 x 4 clocks";
}

TEST_F(NextDdma_Test, Layer2XyDestination256x192IsRowMajor)
{
    _bank = 9;
    _resolution = 0;
    _sram[0x8000] = 0xAB;
    Program(0x8000, 0, 1);
    Send({0xDF, 0x11, 0x05, 0x00, 0x07});  // X = 5, Y = 7
    Go();
    EXPECT_EQ(_sram[0x40000 + 9 * 0x4000 + 7 * 256 + 5], 0xAB);
}

TEST_F(NextDdma_Test, Layer2XyDestination320x256IsColumnMajorAndRowsStepByX)
{
    _bank = 9;
    _resolution = 1;
    _sram[0x8000] = 1;
    _sram[0x8001] = 2;
    _sram[0x8002] = 3;
    _sram[0x8003] = 4;
    Program(0x8000, 0, 0);
    Send({0xDF, 0x13, 0x0A, 0x00, 0x03, 0xE3, 2, 0, 0xE7, 2, 0, 0xEB, 2, 0});  // X = 10, Y = 3, 2x2
    Go();
    const uint32_t base = 0x40000 + 9 * 0x4000 + 10 * 256 + 3;
    EXPECT_EQ(_sram[base], 1);
    EXPECT_EQ(_sram[base + 1], 2) << "the next byte of a row: Y + 1";
    EXPECT_EQ(_sram[base + 256], 3) << "the next row: X + 1";
    EXPECT_EQ(_sram[base + 257], 4);
}

TEST_F(NextDdma_Test, ResetClearsTheExtensionButKeepsTheProgrammedAddresses)
{
    Send({0xDF, 0x0F, 0xD7, 0x05, 0xE3, 9, 0});
    EXPECT_TRUE(_dma.Physical());
    EXPECT_TRUE(_dma.TwoD());
    Send({0xC3});
    EXPECT_FALSE(_dma.Physical());
    EXPECT_FALSE(_dma.TwoD());
}

TEST_F(NextDdma_Test, PortBAndAnExtensionlessCoreTakeTheFollowBytesAsRegisters)
{
    // on port #0B the command is ignored; "#01" after it is then a WR0 byte (direction B->A, no follow bytes), not a mode
    Send({0xDF, 0x01}, true);
    EXPECT_FALSE(_dma.Physical());
    _dma.SetExtended(false);
    Send({0xDF, 0x01});
    EXPECT_FALSE(_dma.Physical());
}
