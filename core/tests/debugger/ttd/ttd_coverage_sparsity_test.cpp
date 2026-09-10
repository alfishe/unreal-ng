/// @file ttd_coverage_sparsity_test.cpp
/// @brief Guards the premise the reverse-search index design rests on.
///
/// The planned per-frame coverage index (see
/// docs/inprogress/2026-08-20-ttd-reverse-search-index/) is only affordable
/// because a single frame touches a tiny, sparse slice of the address space:
/// measured at ~318 executed PCs, ~75 written and ~749 read addresses per frame
/// on a real demo — a fraction of a percent of 64K. That is what lets a sparse
/// per-frame set cost ~100 bytes and stay independent of how much RAM the
/// machine has.
///
/// If that stops being true — an emulation change that fans writes out across
/// memory, or a hook that starts reporting far more than it should — the index
/// silently becomes unaffordable. This test fails loudly instead.
///
/// It also covers Z80::m1TraceHook itself, which has no other test: it is the
/// only per-instruction observation point in the emulator.

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace
{

/// A snapshot that is tracked in the repository, so the test does not depend on
/// anyone's local test corpus.
constexpr const char* kSnapshot = "loaders/sna/Dizzy Y.sna";
constexpr int kFrames = 120;

/// Per-frame coverage, collected the way the index would collect it.
struct FrameCoverage
{
    std::set<uint16_t> executed;
    std::set<uint16_t> written;
    std::set<uint16_t> read;
};

class TTD_CoverageSparsity_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Z80* _z80 = nullptr;
    std::vector<FrameCoverage> _frames;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);

        const std::string path = TestPathHelper::GetTestDataPath(kSnapshot);
        ASSERT_TRUE(_emulator->LoadSnapshot(path)) << "cannot load " << path;

        _z80 = _emulator->GetContext()->pCore->GetZ80();
        ASSERT_NE(_z80, nullptr);
    }

    void TearDown() override
    {
        if (_z80)
        {
            _z80->busTraceHook = nullptr;
            _z80->m1TraceHook = nullptr;
        }
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    void CollectFrames(int frames)
    {
        FrameCoverage current;
        _z80->busTraceHook = [&](char type, uint16_t addr, uint8_t)
        {
            if (type == 'R') current.read.insert(addr);
            else if (type == 'W') current.written.insert(addr);
        };
        _z80->m1TraceHook = [&](uint16_t pc) { current.executed.insert(pc); };

        _frames.reserve(frames);
        for (int i = 0; i < frames; ++i)
        {
            current = FrameCoverage{};
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            _frames.push_back(current);
        }

        _z80->busTraceHook = nullptr;
        _z80->m1TraceHook = nullptr;
    }

    double MeanSize(const std::set<uint16_t> FrameCoverage::*member) const
    {
        size_t total = 0;
        for (const FrameCoverage& f : _frames)
            total += (f.*member).size();
        return _frames.empty() ? 0.0 : static_cast<double>(total) / _frames.size();
    }
};

/// Address space is 64K; express the ceilings as a share of it so the intent
/// survives a change of snapshot. Generous versus the measured 0.5%/0.1%/1.1%:
/// the test guards against an order-of-magnitude regression, not against noise.
constexpr double kAddressSpace = 65536.0;
constexpr double kMaxExecutedShare = 0.10;
constexpr double kMaxWrittenShare  = 0.10;
constexpr double kMaxReadShare     = 0.15;

}  // namespace

/// The hook has to fire, and fire with plausible values — a hook wired to the
/// wrong place would leave every measurement above quietly meaningless.
TEST_F(TTD_CoverageSparsity_Test, M1TraceHookObservesExecutedInstructions)
{
    CollectFrames(kFrames);
    ASSERT_EQ(_frames.size(), static_cast<size_t>(kFrames));

    size_t framesWithoutExecution = 0;
    for (const FrameCoverage& f : _frames)
        if (f.executed.empty())
            ++framesWithoutExecution;

    EXPECT_EQ(framesWithoutExecution, 0u)
        << "frames executed no instructions at all — m1TraceHook is not on the "
        << "instruction-fetch path";
}

/// An opcode fetch is a memory read, so every PC the M1 hook reports must also
/// have been seen by the bus hook. This ties the two observation points
/// together: if m1TraceHook ever moved off the fetch path, this breaks.
TEST_F(TTD_CoverageSparsity_Test, ExecutedAddressesAreASubsetOfReadAddresses)
{
    CollectFrames(kFrames);

    size_t offenders = 0;
    uint16_t firstOffender = 0;
    for (const FrameCoverage& f : _frames)
    {
        for (uint16_t pc : f.executed)
        {
            if (f.read.count(pc) == 0)
            {
                if (offenders == 0)
                    firstOffender = pc;
                ++offenders;
            }
        }
    }

    EXPECT_EQ(offenders, 0u)
        << offenders << " executed address(es) were never read on the bus, "
        << "first at 0x" << std::hex << firstOffender;
}

/// The affordability premise: a frame touches a sparse slice of the address
/// space, so a per-frame sparse set stays around a hundred bytes.
TEST_F(TTD_CoverageSparsity_Test, PerFrameCoverageStaysSparse)
{
    CollectFrames(kFrames);

    const double executed = MeanSize(&FrameCoverage::executed);
    const double written  = MeanSize(&FrameCoverage::written);
    const double read     = MeanSize(&FrameCoverage::read);

    // Non-trivial workload: a frame that touches almost nothing would satisfy
    // the ceilings without demonstrating anything.
    ASSERT_GT(executed, 10.0) << "workload is too idle to be evidence of anything";

    EXPECT_LT(executed / kAddressSpace, kMaxExecutedShare)
        << "executed PCs per frame: " << executed
        << " — a per-frame coverage set is no longer cheap";
    EXPECT_LT(written / kAddressSpace, kMaxWrittenShare)
        << "written addresses per frame: " << written;
    EXPECT_LT(read / kAddressSpace, kMaxReadShare)
        << "read addresses per frame: " << read;
}

/// Detaching must actually detach. A hook left live in a release path would
/// cost a std::function call on every instruction.
TEST_F(TTD_CoverageSparsity_Test, HooksAreInertOnceCleared)
{
    CollectFrames(4);

    size_t firesAfterClear = 0;
    _z80->m1TraceHook = nullptr;
    _z80->busTraceHook = [&](char, uint16_t, uint8_t) { ++firesAfterClear; };
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    EXPECT_GT(firesAfterClear, 0u) << "bus hook stopped working after clearing the M1 hook";

    _z80->busTraceHook = nullptr;
    firesAfterClear = 0;
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    EXPECT_EQ(firesAfterClear, 0u) << "cleared bus hook still fired";
}
