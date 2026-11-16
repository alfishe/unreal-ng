/// @file trdos_3d13_drive_test.cpp
/// @brief TR-DOS #3D13 called from user code before any TR-DOS command touched a drive (R-01).
///
/// TR-DOS 5.04T's cold start writes #FF to the Beta 128 system register and to its copy at #5D16 (ROM #2FAF:
/// LD A,#FF : OUT (#FF),A : LD (#5D16),A) - drive select bits 11, drive D - and leaves #5CF6 / #5D19 at 0 (A).
/// The read function (C=5) does not select a drive: it reads from the one selected, so right after the cold start a
/// read goes to drive D: the ROM's behaviour; programs select the drive first (C=1, A = drive), as TR-DOS' own commands
/// do (`*"A:"`). With no disk there the emulated WD1793 ends Read Sector at once with Not Ready (status #80, READY = a
/// disk in the drive, as in Unreal Speccy) and the call returns without data and without a TR-DOS error. MAME wires
/// the Beta 128's RDY to HLD instead (ready without a disk: the read would wait for an ID that never comes).

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/trdostesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/memory/memory.h"

namespace
{
constexpr uint16_t kCode = 25000;
constexpr uint16_t kBuffer = 0x7000;

class Trdos3D13Drive_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::unique_ptr<TRDOSTestHelper> _trdos;
    uint16_t _lastPc = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        std::string error;
        const std::string disk = (TestPathHelper::FindProjectRoot() / "testdata/loaders/trd/atarin.trd").string();
        ASSERT_TRUE(_emulator->LoadDisk(disk, 0, &error)) << error;
        _trdos = std::make_unique<TRDOSTestHelper>(_emulator);
    }
    void TearDown() override
    {
        _trdos.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint8_t Peek(uint16_t address) { return _emulator->GetMemory()->DirectReadFromZ80Memory(address); }
    void Poke(uint16_t address, uint8_t value) { _emulator->GetMemory()->DirectWriteToZ80Memory(address, value); }

    std::string Variables()
    {
        char text[160];
        std::snprintf(text, sizeof(text), "pc=%04X #5CF6=%02X #5D16=%02X #5D19=%02X #5D0F=%02X system register=%02X",
                      _lastPc, Peek(0x5CF6), Peek(0x5D16), Peek(0x5D19), Peek(0x5D0F),
                      _context->pBetaDisk->getBeta128Register());
        return text;
    }

    /// User code at 25000 (stack below it): 48 BASIC ROM paged, then #3D13 C=5 - one sector, track 0, sector 9
    /// (E=8) to #7000 - and a loop. True when the call returned to the loop
    bool CallReadSector(bool selectDriveA)
    {
        std::vector<uint8_t> code = {
            0x31, 0x9F, 0x61,        // LD SP,24991
            0x01, 0xFD, 0x7F,        // LD BC,#7FFD
            0x3E, 0x10,              // LD A,#10 (48 BASIC ROM)
            0xED, 0x79,              // OUT (C),A
        };
        if (selectDriveA)
            code.insert(code.end(), {
                0xAF,                // XOR A (drive A)
                0x0E, 0x01,          // LD C,1 (select drive)
                0xCD, 0x13, 0x3D,    // CALL #3D13
            });
        code.insert(code.end(), {
            0x21, 0x00, 0x70,        // LD HL,#7000
            0x11, 0x08, 0x00,        // LD DE,#0008 (track 0, sector 9)
            0x01, 0x05, 0x01,        // LD BC,#0105 (one sector, read)
            0xCD, 0x13, 0x3D,        // CALL #3D13
            0x18, 0xFE,              // JR $
        });
        for (size_t i = 0; i < code.size(); i++)
            Poke(static_cast<uint16_t>(kCode + i), code[i]);
        for (uint16_t i = 0; i < 256; i++)
            Poke(static_cast<uint16_t>(kBuffer + i), 0);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = kCode;
        const uint16_t loop = static_cast<uint16_t>(kCode + code.size() - 2);
        // The checks fall on frame ends, where the 48 ROM's interrupt handler runs: the loop is seen at its fetch
        bool reached = false;
        z80->m1TraceHook = [&reached, loop](uint16_t pc) { reached = reached || pc == loop; };
        const bool returned = _trdos->runUntil([&] { return reached; }, 10 * 3'500'000, 5);
        z80->m1TraceHook = nullptr;
        _lastPc = z80->pc;
        return returned;
    }
};
}  // namespace

/// TR-DOS cold-started by the disk autostart, with RETURN instead of RUN "boot" (no command touched a drive), back in
/// BASIC: the cold start selected drive D, and a read through #3D13 goes there - Not Ready, no data (R-01's status 128)
TEST_F(Trdos3D13Drive_Test, ReadSectorRightAfterTheColdStartGoesToTheDriveTheRomSelectedD)
{
    _trdos->startCommand("RETURN");
    _trdos->runFrames(100);
    EXPECT_EQ(_context->pBetaDisk->getBeta128Register(), 0xFF) << Variables();
    EXPECT_EQ(Peek(0x5D16), 0xFF) << "the ROM's copy of the system register";
    EXPECT_EQ(Peek(0x5CF6), 0) << "current drive A in the variables all the same";

    EXPECT_TRUE(CallReadSector(false)) << "the WD1793 refuses the read at once: the call returns " << Variables();
    EXPECT_EQ(_context->pBetaDisk->getBeta128Register() & 0x03, 3) << Variables();
    EXPECT_EQ(static_cast<const WD1793&>(*_context->pBetaDisk).getStatusRegister(), 0x80) << "Not Ready: no disk in D";
    EXPECT_EQ(Peek(kBuffer + 0xE7), 0) << "nothing read";
}

/// The same with the drive selected first (C=1, A=0): the select probes drive A (~30 frames), then the read goes there
/// and returns its sector
TEST_F(Trdos3D13Drive_Test, ReadSectorAfterSelectingDriveAGoesToDriveA)
{
    _trdos->startCommand("RETURN");
    _trdos->runFrames(100);

    EXPECT_TRUE(CallReadSector(true)) << Variables();
    EXPECT_EQ(_context->pBetaDisk->getBeta128Register() & 0x03, 0) << Variables();
    EXPECT_EQ(Peek(kBuffer + 0xE7), 0x10) << "sector 9 of the disk in A (the TR-DOS id byte)";
}
