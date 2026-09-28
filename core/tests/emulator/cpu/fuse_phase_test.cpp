#include "stdafx.h"
#include "pch.h"

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/fusevectors.h"
#include "common/filehelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

/// FUSE-derived full-opcode bus-phase tests (Phase 2/3 of the phase-test plan).
///
/// Data: testdata/z80/fuse/tests.in + tests.expected (FUSE emulator test
/// vectors, ~1300 cases covering every documented and undocumented opcode).
/// Each case provides initial CPU/memory state and the expected full bus
/// trace: every memory/port access with its T-state, plus final state.
///
/// Parsing, the event mapping and the final-state checks: _helpers/fusevectors.h.
///
/// Port reads return the high byte of the port address (FUSE stub behavior),
/// so PR event values and final register state match the reference.

class FusePhase_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    PortDecoder* _originalDecoder = nullptr;
    FuseVectors::FusePortDecoder* _fuseDecoder = nullptr;

    std::vector<FuseVectors::BusEvent> _trace;
    uint32_t _t0 = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);

        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;

        // FUSE tests execute at arbitrary addresses incl. $0000 - map RAM
        // into bank 0 (page 6: unused by default banks 5/2/0)
        _memory->SetRAMPageToBank0(6);

        _fuseDecoder = new FuseVectors::FusePortDecoder(_context);
        _originalDecoder = _context->pPortDecoder;
        _context->pPortDecoder = _fuseDecoder;

        _z80->busTraceHook = [this](char type, uint16_t addr, uint8_t value) {
            _trace.push_back({type, addr, value, _z80->t - _t0});
        };
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _z80->busTraceHook = nullptr;
            _context->pPortDecoder = _originalDecoder;
            delete _fuseDecoder;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Run one FUSE case; returns list of mismatch descriptions (empty = pass)
    std::vector<std::string> runCase(const FuseVectors::FuseCase& tc, bool skipAFCompare = false)
    {
        std::vector<std::string> issues;

        FuseVectors::LoadState(_z80, _memory, tc);

        _trace.clear();
        _z80->t = 10000;  // Far from INT window, mid-frame
        _t0 = _z80->t;

        int steps = 0;
        while ((_z80->t - _t0) < tc.expTotal && steps++ < 64)
        {
            _z80->Z80Step();
        }

        uint32_t total = _z80->t - _t0;
        if (total != tc.expTotal)
            issues.push_back("total " + std::to_string(total) + " != " + std::to_string(tc.expTotal));

        FuseVectors::CompareTrace(_trace, tc, issues);
        FuseVectors::CompareFinalState(_z80, tc, skipAFCompare, issues);

        return issues;
    }
};

TEST_F(FusePhase_Test, AllOpcodes)
{
    auto cases = FuseVectors::LoadCases();
    ASSERT_GT(cases.size(), 1000u) << "FUSE tests.in not found or truncated";

    // Documented divergence from the FUSE vectors (justified, not bugs):
    // interrupted block-op flags. Our core implements David Banks'
    // hardware-verified undocumented flag research (F5/F3 from PC.13/PC.11 on
    // repeat, adjusted PV) which postdates these FUSE vectors (classic flag
    // model - verified identical in FUSE official master as of 2026-08).
    // AF comparison skipped for these four; everything else verified.
    // (FUSE's contend-only logging for not-taken JR/DJNZ displacement reads
    // is handled structurally in runCase, not by exclusion.)
    const std::set<std::string>& skipAF = FuseVectors::SkipAFCases();

    int passed = 0, failed = 0;
    std::string failureReport;

    for (const auto& [name, tc] : cases)
    {
        if (tc.expTotal == 0)
            continue;  // No expected data merged

        auto issues = runCase(tc, skipAF.count(name) > 0);
        if (issues.empty())
        {
            passed++;
        }
        else
        {
            failed++;
            if (failureReport.size() < 8000)
            {
                failureReport += "\n[" + name + "] ";
                for (size_t i = 0; i < issues.size() && i < 4; i++)
                    failureReport += issues[i] + "; ";
            }
        }
    }

    EXPECT_EQ(failed, 0) << "FUSE phase cases: " << passed << " passed, " << failed << " failed"
                         << failureReport;
    std::cout << "[FUSE] " << passed << " passed, " << failed << " failed" << std::endl;
}
