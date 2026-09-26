/// @file ttdcheckpoint_boundary_test.cpp
/// @brief The instruction-boundary state survives a TTD checkpoint and decides
///        the next interrupt acceptance after a restore.
///
/// Every Z80BoundaryEnum value is reached by executing real instructions on a
/// Pentagon, captured with CaptureCpuState, clobbered, restored with
/// RestoreCpuState, and then the interrupt it governs is offered: the outcome
/// must follow the restored state. The control run restores a checkpoint with
/// the boundary byte zeroed (what a checkpoint without it would carry) and
/// must see the opposite outcome - the state is load-bearing, not decorative.

#include "stdafx.h"
#include "pch.h"
#include "_helpers/emulatortesthelper.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

#include <functional>
#include <string>

class TTDBoundaryRestore_Test : public ::testing::Test
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
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    void load(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _memory->DirectWriteToZ80Memory(addr++, b);
    }

    /// Clean CPU at $8000 with interrupts enabled, IM1, stack at $A000
    void reset()
    {
        _z80->im = 1;
        _z80->iff1 = _z80->iff2 = 1;
        _z80->sp = 0xA000;
        _z80->pc = 0x8000;
        _z80->t = 1000;
        _z80->boundary = Z80_BOUNDARY_NONE;
    }

    /// Offer INT (and any requested NMI) at the current boundary
    bool offer() { return _z80->ProcessInterrupts(false, 0, 0xFFFFFFFFu); }

    struct Scenario
    {
        std::string name;
        Z80BoundaryEnum boundary;          // state the setup must reach
        std::function<void()> setup;       // executes real instructions
        std::function<bool()> outcome;     // the acceptance question
        bool expected;                     // answer with the state restored
    };

    /// Reach the boundary, capture, run the outcome from a restore of the
    /// capture (optionally with the boundary byte zeroed)
    bool outcomeAfterRestore(const Scenario& s, bool stripBoundary)
    {
        reset();
        s.setup();
        EXPECT_EQ(_z80->boundary, s.boundary) << s.name << ": setup did not reach the state";
        ttd::TTDCpuState cp = ttd::CaptureCpuState(*static_cast<const Z80State*>(_z80));
        EXPECT_EQ(cp.boundary, s.boundary) << s.name << ": capture";
        if (stripBoundary)
            cp.boundary = Z80_BOUNDARY_NONE;

        // Clobber the live state the restore must bring back
        _z80->boundary = Z80_BOUNDARY_NMI_ACK;
        _z80->iff1 = _z80->iff2 = 0;
        ttd::RestoreCpuState(cp, _z80);
        return s.outcome();
    }
};

TEST_F(TTDBoundaryRestore_Test, EveryBoundaryStateDecidesAcceptanceAfterRestore)
{
    const std::vector<Scenario> scenarios = {
        {"EI shadow", Z80_BOUNDARY_INT_SHADOW,
         [&] {
             _z80->iff1 = _z80->iff2 = 0;
             load(0x8000, {0xFB});  // EI
             _z80->Z80Step();
         },
         [&] { return offer(); }, false},

        {"RETN that set IFF1", Z80_BOUNDARY_INT_SHADOW,
         [&] {
             _z80->iff1 = 0;  // inside an NMI handler
             _z80->iff2 = 1;
             load(0xA000, {0x00, 0x90});  // return address $9000
             load(0x8000, {0xED, 0x45});  // RETN
             _z80->Z80Step();
         },
         [&] { return offer(); }, false},

        {"LD A,I then INT clears P/V", Z80_BOUNDARY_LD_A_IR,
         [&] {
             load(0x8000, {0xED, 0x57});  // LD A,I
             _z80->Z80Step();
         },
         [&] { return offer() && (_z80->f & 0x04) == 0; }, true},

        {"pending DD prefix", Z80_BOUNDARY_PREFIX_DD,
         [&] {
             load(0x8000, {0xFD, 0xDD, 0x00});  // FD (redundant); DD NOP
             _z80->Z80Step();
         },
         [&] { return offer(); }, false},

        {"NMI just acknowledged", Z80_BOUNDARY_NMI_ACK,
         [&] {
             _z80->RequestNonMaskedInterrupt();
             offer();
         },
         [&] {
             _z80->RequestNonMaskedInterrupt();
             _z80->iff1 = 1;  // keep INT out of the question: only the NMI rule decides
             _z80->int_pending = false;
             return _z80->ProcessInterrupts(false, 0xFFFFFFF0u, 0xFFFFFFFFu);
         },
         false},
    };

    for (const Scenario& s : scenarios)
    {
        EXPECT_EQ(outcomeAfterRestore(s, /*stripBoundary=*/false), s.expected) << s.name << ": restored";
        EXPECT_NE(outcomeAfterRestore(s, /*stripBoundary=*/true), s.expected)
            << s.name << ": without the boundary byte the outcome must differ";
    }
}
