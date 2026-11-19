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

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/machinevariants.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "_helpers/emulatortesthelper.h"

// The variant NEXT-DDMA ("ZX Next 2048 DDMA") has the blitter commands on its DMA, the plain NEXT does not (core 3.02.03)
TEST(NextDdmaVariant_Test, OnlyTheDdmaVariantTakesTheCommands)
{
    for (const bool ddma : {true, false})
    {
        const MachineVariant* variant = ddma ? MachineVariants::Find("NEXT-DDMA") : nullptr;
        ASSERT_EQ(variant != nullptr, ddma);
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero,
                                                                        variant ? variant->apply : nullptr);
        ASSERT_NE(emulator, nullptr);
        auto* ports = dynamic_cast<PortDecoder_Next*>(emulator->GetContext()->pPortDecoder);
        ASSERT_NE(ports, nullptr);
        EXPECT_EQ(emulator->GetContext()->config.ramsize, 2048u);
        ports->DecodePortOut(0x6B, 0xDF, 0);
        ports->DecodePortOut(0x6B, 0x01, 0);
        EXPECT_EQ(ports->Dma().Physical(), ddma) << (ddma ? "NEXT-DDMA" : "NEXT");
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

// The SRAM read wait at 28 MHz is the 3.02.03 core's (zxnext.vhd 3171-3181), core 3.03 (the variant NEXT-DDMA) dropped it - measured on
// a board (2026-11-12): a NOP from the SRAM takes 5 clocks on 3.02.03 and 4 on 3.03, and a memory-to-memory DMA byte with both cycle
// lengths 2 takes 2 + 1 + 2 clocks on 3.02.03 and 2 + 2 on 3.03
TEST(NextDdmaVariant_Test, OnlyCore30203WaitsForTheSramAt28MHz)
{
    for (const bool ddma : {false, true})
    {
        const MachineVariant* variant = ddma ? MachineVariants::Find("NEXT-DDMA") : nullptr;
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero,
                                                                        variant ? variant->apply : nullptr);
        ASSERT_NE(emulator, nullptr);
        EmulatorContext* context = emulator->GetContext();
        auto* ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
        ASSERT_NE(ports, nullptr);
        Z80* z80 = context->pCore->GetZ80();
        auto out = [&](uint16_t port, uint8_t value) { ports->DecodePortOut(port, value, 0); };
        out(0x243B, 0x07);
        out(0x253B, 0x03);  // 28 MHz
        out(0x243B, 0x56);
        out(0x253B, 0x00);  // slot 6: bank 0 (SRAM)
        emulator->RunFrame(true);  // the speed applies at a frame start
        for (uint16_t i = 0; i < 64; i++)
            context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xC000 + i), 0x00);
        z80->pc = 0xC000;
        uint32_t t0 = z80->t;
        for (unsigned i = 0; i < 32; i++)
            z80->EngineStep();
        EXPECT_EQ(z80->t - t0, 32u * (ddma ? 4u : 5u)) << (ddma ? "3.03" : "3.02.03") << ": NOPs from the SRAM";

        // bank 0 #C000 -> bank 0 #C800, both ports memory counting up with cycle length 2, continuous; the NOPs at #C000 enable it
        auto copyTime = [&](uint16_t length) {
            for (uint8_t b : {uint8_t(0x83), uint8_t(0x7D), uint8_t(0x00), uint8_t(0xC0), uint8_t(length & 0xFF), uint8_t(length >> 8),
                              uint8_t(0x54), uint8_t(0x02), uint8_t(0x50), uint8_t(0x02), uint8_t(0xAD), uint8_t(0x00), uint8_t(0xC8),
                              uint8_t(0xCF), uint8_t(0x87)})
                out(0x6B, b);
            z80->pc = 0xC000;
            const uint32_t start = z80->t;
            z80->EngineStep();
            z80->EngineStep();
            return z80->t - start;
        };
        const uint32_t perByte = (copyTime(128) - copyTime(64)) / 64;
        EXPECT_EQ(perByte, ddma ? 4u : 5u) << (ddma ? "3.03" : "3.02.03") << ": a DMA byte, memory to memory, cycle length 2";
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}
