// Pentagon 2.666 LE Beta Disk emulator on the RAM disk (common/betadisk.vhd, hardware-reference.md §5.6): the board
// as built, timed in mainclk edges.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/memory.h"
#include "emulator/platforms/p2666/p2666betadisk.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "pch.h"
#include "stdafx.h"

namespace
{
constexpr uint8_t kSide0 = 0x1C;  // system: drive A, out of reset, motor, side 0 (bit 4 = 1)
constexpr uint8_t kSide1 = 0x0C;  // side 1 (bit 4 = 0)

uint8_t Pattern(size_t index) { return static_cast<uint8_t>(index * 13 + (index >> 8) * 7 + 1); }
}  // namespace

class P2666BetaDisk_Test : public ::testing::Test
{
protected:
    // Pages #40..#7F of the board's SRAM
    std::vector<uint8_t> _ram = std::vector<uint8_t>(64u * 16384u);
    P2666BetaDisk _beta;
    uint64_t _edge = 1000;

    void SetUp() override
    {
        _beta.SetMemory(this, [](void* owner, uint8_t page) -> uint8_t* {
            auto* self = static_cast<P2666BetaDisk_Test*>(owner);
            return page >= 0x40 && page < 0x80 ? self->_ram.data() + (page - 0x40) * 16384u : nullptr;
        });
        std::vector<uint8_t> trd(80u * 2u * 16u * 256u);
        for (size_t i = 0; i < trd.size(); i++)
            trd[i] = Pattern(i);
        ASSERT_EQ(_beta.LoadTrd(trd.data(), trd.size()), 80u * 2u * 16u);
        _beta.PowerOn(_edge);
        _beta.WriteSystem(kSide0, _edge);
    }

    uint8_t Status() { return _beta.ReadRegister(0, _edge); }
    void Command(uint8_t value) { _beta.WriteRegister(0, value, _edge); }
    void Wait(uint64_t edges) { _edge += edges; }
    /// The pins now (the system port read runs the controller to the edge, no side effect)
    bool Drq() { return (_beta.ReadSystem(_edge) & 0x40) != 0; }
    bool Intrq() { return (_beta.ReadSystem(_edge) & 0x80) != 0; }
    void Seek(uint8_t track)
    {
        _beta.WriteRegister(3, track, _edge);
        Command(0x18);
        Wait(P2666BetaDisk::kTypeIEdges);
        ASSERT_TRUE(Intrq());
    }
};

/// T-B1: the image layout: cylinder 0 at page #76, a cylinder pair per page, side = NOT system bit 4 at A12
TEST_F(P2666BetaDisk_Test, ImageLayoutIsTheBoardsPages)
{
    EXPECT_EQ(_beta.ImageByte(0, 0, 1, 0), _ram.data() + (0x76 - 0x40) * 16384u);
    EXPECT_EQ(_beta.ImageByte(1, 0, 1, 0), _ram.data() + (0x76 - 0x40) * 16384u + 0x2000);
    EXPECT_EQ(_beta.ImageByte(2, 1, 3, 5), _ram.data() + (0x75 - 0x40) * 16384u + 0x1000 + 0x200 + 5);
    EXPECT_EQ(_beta.ImageByte(79, 1, 16, 255), _ram.data() + (0x4F - 0x40) * 16384u + 0x3FFF);
    EXPECT_EQ(*_beta.ImageByte(3, 1, 2, 4), Pattern(((3 * 2 + 1) * 16 + 1) * 256 + 4));
}

/// T-B2: read sector: a DRQ 127 edges after the command and after each byte taken, INTRQ after the last one
TEST_F(P2666BetaDisk_Test, ReadSectorTimesEveryByte)
{
    Seek(5);
    EXPECT_EQ(_beta.Position(), 5);
    EXPECT_EQ(_beta.Track(), 5);
    _beta.WriteRegister(2, 3, _edge);
    Command(0x80);
    EXPECT_EQ(Status() & 0x01, 0x01) << "busy";
    Wait(P2666BetaDisk::kByteEdges - 1);
    EXPECT_FALSE(Drq()) << "no DRQ before 127 edges";
    Wait(1);
    EXPECT_EQ(_beta.ReadSystem(_edge), 0x7F) << "DRQ, no INTRQ";
    const size_t base = ((5 * 2 + 0) * 16 + 2) * 256;
    for (unsigned i = 0; i < 256; i++)
    {
        ASSERT_TRUE(_beta.ReadSystem(_edge) & 0x40) << i;
        ASSERT_EQ(_beta.ReadRegister(3, _edge), Pattern(base + i)) << i;
        ASSERT_FALSE(Drq()) << "the read takes DRQ down";
        Wait(P2666BetaDisk::kByteEdges);
    }
    EXPECT_EQ(_beta.ReadSystem(_edge), 0xBF) << "INTRQ";
    EXPECT_EQ(Status(), 0x00) << "done: not busy, no DRQ, no type I bits";
    EXPECT_FALSE(Intrq()) << "the status read clears INTRQ";
}

/// T-B2: a byte not taken holds the controller (no lost data): DRQ stays until the CPU reads
TEST_F(P2666BetaDisk_Test, ReadWaitsForTheCpu)
{
    _beta.WriteSystem(kSide1, _edge);
    _beta.WriteRegister(2, 16, _edge);
    Command(0x80);
    Wait(100000);
    EXPECT_EQ(Status() & 0x03, 0x03) << "busy, DRQ";
    EXPECT_EQ(_beta.ReadRegister(3, _edge), Pattern((1 * 16 + 15) * 256));
}

/// T-B2: multiple read runs to sector 16 of the track, then INTRQ; the next sector's first DRQ comes 2 x 127 edges
/// after the last byte (read_delay runs out once more, then reloads: betadisk.vhd:432-439)
TEST_F(P2666BetaDisk_Test, MultipleReadStopsAfterSector16)
{
    _beta.WriteRegister(2, 15, _edge);
    Command(0x90);
    for (unsigned i = 0; i < 512; i++)
    {
        Wait(P2666BetaDisk::kByteEdges);
        if (i == 256)
        {
            EXPECT_FALSE(_beta.ReadSystem(_edge) & 0x40) << "sector change";
            Wait(P2666BetaDisk::kByteEdges);
        }
        _beta.ReadSystem(_edge);
        ASSERT_TRUE(Drq()) << i;
        ASSERT_EQ(_beta.ReadRegister(3, _edge), Pattern(14 * 256 + i)) << i;
    }
    Wait(P2666BetaDisk::kByteEdges);
    _beta.ReadSystem(_edge);
    EXPECT_TRUE(Intrq());
}

/// T-B3: write sector stores the bytes into the RAM disk
TEST_F(P2666BetaDisk_Test, WriteSectorStoresIntoRam)
{
    Seek(2);
    _beta.WriteSystem(kSide1, _edge);
    _beta.WriteRegister(2, 9, _edge);
    Command(0xA0);
    for (unsigned i = 0; i < 256; i++)
    {
        Wait(P2666BetaDisk::kByteEdges);
        ASSERT_TRUE(Drq()) << i;
        _beta.WriteRegister(3, static_cast<uint8_t>(0xFF - i), _edge);
    }
    Wait(P2666BetaDisk::kByteEdges);
    EXPECT_TRUE(Intrq());
    for (unsigned i = 0; i < 256; i++)
        ASSERT_EQ(*_beta.ImageByte(2, 1, 9, static_cast<uint8_t>(i)), 0xFF - i) << i;
    EXPECT_EQ(*_beta.ImageByte(2, 1, 10, 0), Pattern((2 * 2 + 1) * 16 * 256 + 9 * 256)) << "the next sector stays";
}

/// T-B4: read address: track position, side (system bit 4 as it is), sector register, #FF, #F0, #E0
TEST_F(P2666BetaDisk_Test, ReadAddressBytes)
{
    Seek(7);
    _beta.WriteRegister(2, 4, _edge);
    Command(0xC0);
    const uint8_t expected[6] = {7, 1, 4, 0xFF, 0xF0, 0xE0};
    for (unsigned i = 0; i < 6; i++)
    {
        Wait(P2666BetaDisk::kByteEdges);
        ASSERT_TRUE(Drq()) << i;
        ASSERT_EQ(_beta.ReadRegister(3, _edge), expected[i]) << i;
    }
    EXPECT_TRUE(Intrq()) << "INTRQ with the last byte";
    EXPECT_EQ(Status() & 0x01, 0x00);
}

/// T-B5: type I: busy and the type I bits for 127 edges (restore 0x3D40000), track 0 only while it runs
TEST_F(P2666BetaDisk_Test, TypeICommandsAndStatus)
{
    Seek(3);
    Command(0x58);  // step in, update
    EXPECT_EQ(_beta.Track(), 4);
    EXPECT_EQ(Status() & 0x21, 0x21) << "type I, busy";
    Wait(P2666BetaDisk::kTypeIEdges);
    EXPECT_TRUE(Intrq());
    EXPECT_EQ(Status() & 0x21, 0x00) << "done: bit 5 goes with the command";

    Command(0x00);  // restore
    Wait(P2666BetaDisk::kTypeIEdges);
    EXPECT_EQ(Status() & 0x25, 0x21) << "restore still running at 127 edges, head not yet on track 0";
    Wait(P2666BetaDisk::kRestoreEdges);
    EXPECT_TRUE(Intrq());
    EXPECT_EQ(_beta.Position(), 0);
    EXPECT_EQ(_beta.Track(), 0);
    EXPECT_EQ(Status() & 0x04, 0x00) << "D-14: track 0 is gone once the restore ended";
}

/// T-B5: drive B..D: commands and register writes are lost, reads still answer
TEST_F(P2666BetaDisk_Test, OnlyDriveATakesCommands)
{
    _beta.WriteSystem(kSide0 | 0x01, _edge);
    _beta.WriteRegister(1, 0x22, _edge);
    Command(0x80);
    Wait(1000);
    EXPECT_EQ(_beta.Track(), 0);
    EXPECT_FALSE(Drq());
    EXPECT_EQ(Status() & 0x01, 0x00) << "not busy";
}

/// T-B5: force interrupt with I3: INTRQ at once, the read stops
TEST_F(P2666BetaDisk_Test, ForceInterruptStopsTheRead)
{
    Command(0x80);
    Wait(P2666BetaDisk::kByteEdges);
    EXPECT_TRUE(Drq());
    Command(0xD8);
    EXPECT_TRUE(Intrq());
    EXPECT_FALSE(Drq());
    EXPECT_EQ(Status() & 0x01, 0x00);
}

/// T-B6: the decoder: RAM disk by default, DOS ports #1F..#7F / #FF, the image in pages #4E..#76
TEST(P2666BetaDiskPorts_Test, DosPortsReachTheRamDisk)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("P2666", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoderP2666*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    P2666BetaDisk* beta = decoder->GetRamDisk();
    ASSERT_NE(beta, nullptr);
    context->emulatorState.flags |= CF_TRDOS;

    uint8_t* sector = beta->ImageByte(0, 0, 1, 0);
    ASSERT_EQ(sector, context->pMemory->RAMPageAddress(0x76));
    for (unsigned i = 0; i < 256; i++)
        sector[i] = static_cast<uint8_t>(i ^ 0x5A);

    decoder->DecodePortOut(0x00FF, kSide0, 0x3D00);
    decoder->DecodePortOut(0x003F, 0x00, 0x3D00);
    decoder->DecodePortOut(0x005F, 0x01, 0x3D00);
    EXPECT_EQ(decoder->DecodePortIn(0x005F, 0x3D00), 0x01);
    EXPECT_EQ(decoder->DecodePortIn(0x005D, 0x3D00), 0x01) << "any odd port by A7..5 reads";
    decoder->DecodePortOut(0x001F, 0x80, 0x3D00);

    Z80* z80 = context->pCore->GetZ80();
    const uint8_t multiplier = std::max<uint8_t>(1, context->emulatorState.current_z80_frequency_multiplier);
    for (unsigned i = 0; i < 4; i++)
    {
        z80->t += 8u * multiplier;  // 8 base T = 256 edges: past the byte's 127
        ASSERT_EQ(decoder->DecodePortIn(0x00FF, 0x3D00) & 0xC0, 0x40) << i;
        ASSERT_EQ(decoder->DecodePortIn(0x007F, 0x3D00), i ^ 0x5A) << i;
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}
