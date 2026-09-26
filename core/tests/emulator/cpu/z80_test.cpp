/// @file Z80 instruction-start bookkeeping (Z80::m1_cycle /
/// Z80::RecordInstructionStart).
///
/// The instruction-start work - m1_pc (the address every memory access of the
/// instruction is attributed to: memory tracker, TTD write journal and probes,
/// calltrace), the M1 trace hook (TTD per-frame instruction capture),
/// execution coverage and the TTD execute probe - runs once per instruction,
/// at its first byte. The M1s inside an instruction (the byte after CB, ED,
/// DD, FD, DDCB) must not run it again. A redundant DD/FD is an instruction of
/// its own, so the prefix after it does start one.

#include "stdafx.h"
#include "pch.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

#include <set>
#include <utility>
#include <vector>

class InstructionStart_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _z80 = _emulator->GetContext()->pCore->GetZ80();
        _memory = _emulator->GetContext()->pMemory;
    }

    void TearDown() override
    {
        if (_z80)
            _z80->m1TraceHook = nullptr;
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

TEST_F(InstructionStart_Test, OncePerInstructionAtItsFirstByte)
{
    // Every prefix form, each at a known address
    struct Instr
    {
        uint16_t start;
        const char* text;
        bool writesMemory;
    };
    const std::vector<uint8_t> code = {
        0x00,                          // 8000 NOP
        0xDD, 0x77, 0x05,              // 8001 LD (IX+5),A
        0xCB, 0xC6,                    // 8004 SET 0,(HL)
        0xDD, 0xCB, 0x05, 0xC6,        // 8006 SET 0,(IX+5)
        0xFD,                          // 800A FD (redundant: an instruction of its own)
        0xDD, 0x21, 0x34, 0x12,        // 800B LD IX,1234h
        0xED, 0x44,                    // 800F NEG
        0xDD, 0xED, 0x44,              // 8011 NEG (DD ignored)
        0xFD, 0x7E, 0x05,              // 8014 LD A,(IY+5)
    };
    const std::vector<Instr> instructions = {
        {0x8000, "NOP", false},          {0x8001, "LD (IX+5),A", true}, {0x8004, "SET 0,(HL)", true},
        {0x8006, "SET 0,(IX+5)", true},  {0x800A, "FD (redundant)", false}, {0x800B, "LD IX,nn", false},
        {0x800F, "NEG", false},          {0x8011, "DD NEG", false},     {0x8014, "LD A,(IY+5)", false},
    };
    for (size_t i = 0; i < code.size(); i++)
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);

    _z80->pc = 0x8000;
    _z80->sp = 0xA000;
    _z80->ix = 0x9000;
    _z80->iy = 0x9000;
    _z80->hl = 0x9100;
    _z80->iff1 = _z80->iff2 = 0;
    _z80->boundary = Z80_BOUNDARY_NONE;

    std::vector<std::pair<uint16_t, uint32_t>> starts;  // (pc, t) per hook call
    _z80->m1TraceHook = [&](uint16_t pc) { starts.emplace_back(pc, static_cast<uint32_t>(_z80->t)); };

    std::set<uint32_t> boundaries{_z80->t};  // T at every step boundary
    for (const Instr& in : instructions)
    {
        _z80->Z80Step();
        boundaries.insert(_z80->t);
        if (in.writesMemory)
            EXPECT_EQ(_z80->m1_pc, in.start)
                << in.text << ": its memory accesses must be attributed to the instruction start";
    }
    EXPECT_EQ(_z80->pc, 0x8017u) << "program ran to its end";

    // One start per instruction, in order, at the first byte
    ASSERT_EQ(starts.size(), instructions.size()) << "hooks must fire once per instruction";
    for (size_t i = 0; i < instructions.size(); i++)
        EXPECT_EQ(starts[i].first, instructions[i].start) << instructions[i].text;

    // Each recorded start is a state TTD can seek to: a step boundary
    for (const auto& [pc, t] : starts)
        EXPECT_TRUE(boundaries.count(t)) << "start at " << std::hex << pc << " recorded mid-step (t=" << std::dec << t << ")";
}
