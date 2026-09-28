#include "stdafx.h"
#include "pch.h"

#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/fusevectors.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"
#include "emulator/video/ulacontention.h"
#include "base/featuremanager.h"

/// The contended memory interfaces (memorycontended.cpp) against every FUSE opcode vector.
///
/// The +2A/+3 all-RAM layouts make the whole address space one kind of memory: layout 0 (pages 0-3) is not
/// contended anywhere, layout 1 (pages 4-7) is contended everywhere, #0000 included - so the FUSE vectors,
/// which put code and data at any address, run unchanged in either. The gate array contends MREQ cycles only
/// (no internal cycles, no ports), which makes its expected timing a pure function of the memory accesses:
///
///   1. Layout 0 at a paper T-state: nothing may wait - the trace, total and final state equal FUSE's
///      (negative control: contention off by placement, not by machine).
///   2. Layout 1, the opcode's accesses starting at each cell offset of the pattern and across a line end:
///      the prediction is the layout-0 trace with every memory access delayed by the gate array's wait at
///      the T-state its cycle starts (independent oracle below: raster geometry + the published pattern,
///      not UlaContention), accumulated. Every event time, the total and the final state must match.
///   3. Layout 1 with the 'contention' switch off: FUSE's trace again.
namespace
{
constexpr uint8_t kGateArrayPattern[8] = { 1, 0, 7, 6, 5, 4, 3, 2 };
constexpr uint32_t kIntToFirstContended = 14361;  // 128K / +2A / +3 contention onset, T after the INT
constexpr uint32_t kTStatesPerLine = 228;
constexpr uint32_t kContendedPerLine = 129;  // the gate array's 1 T hold after the last cell (hardware: Rak +3 / +2A)
constexpr uint32_t kPaperLines = 192;

/// Cycle-start T-states tried: every cell offset at the start of paper line 0, and a start 6 T before the
/// end of line 3's contended span so multi-access opcodes cross the closing 1 T hold into the border
const std::vector<uint32_t>& StartOffsets()
{
    static const std::vector<uint32_t> offsets = { 0, 1, 2, 3, 4, 5, 6, 7, 3 * kTStatesPerLine + kContendedPerLine - 6 };
    return offsets;
}
}  // namespace

class MemoryContendedFuse_Test : public ::testing::Test
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
    uint32_t _firstContendedT = 0;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PLUS3", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        _context->pScreen->InitFrame();

        UlaContention* ula = _context->pUlaContention;
        ASSERT_TRUE(ula->IsGateArray());
        _firstContendedT = _context->config.intstart + 1 + kIntToFirstContended;
        const ContentionRaster& raster = ula->GetRaster();
        ASSERT_EQ(_firstContendedT, raster.screenAreaStart + raster.screenLineAreaStart - 5)
            << "the oracle's onset and the raster disagree";
        ASSERT_EQ(raster.tstatesPerLine, kTStatesPerLine);

        _context->emulatorState.p7FFD &= static_cast<uint8_t>(~0x20);  // 48 BASIC locks paging; unlock
        _originalDecoder = _context->pPortDecoder;
        _fuseDecoder = new FuseVectors::FusePortDecoder(_context);
        _context->pPortDecoder = _fuseDecoder;

        _z80->busTraceHook = [this](char type, uint16_t addr, uint8_t value) {
            _trace.push_back({ type, addr, value, _z80->t - _t0 });
        };
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _z80->busTraceHook = nullptr;
            _context->pPortDecoder = _originalDecoder;
            delete _fuseDecoder;
            _context->pFeatureManager->setFeature(Features::kContention, true);
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }

    /// +2A/+3 all-RAM layout through the machine's own port. The machine's decoder is put back for the write:
    /// Memory::UpdateZ80Banks asks the context's decoder for the model's bank layout
    void Layout(uint8_t layout)
    {
        _context->pPortDecoder = _originalDecoder;
        _originalDecoder->DecodePortOut(0x1FFD, static_cast<uint8_t>(0x01 | (layout << 1)), 0x8000);
        _context->pPortDecoder = _fuseDecoder;
    }

    /// Independent oracle: the gate array's wait for a memory cycle starting at T-state `t`
    uint8_t OracleWait(uint32_t t) const
    {
        if (t < _firstContendedT)
            return 0;
        const uint32_t rel = t - _firstContendedT;
        if (rel / kTStatesPerLine >= kPaperLines)
            return 0;
        const uint32_t x = rel % kTStatesPerLine;
        return x < kContendedPerLine ? kGateArrayPattern[x % 8] : 0;
    }

    /// Run `steps` instructions of a loaded case from T-state t0 (steps = 0: until FUSE's total is reached);
    /// returns the steps taken
    int Run(const FuseVectors::FuseCase& tc, uint32_t t0, int steps)
    {
        FuseVectors::LoadState(_z80, _memory, tc);
        _trace.clear();
        _z80->t = t0;
        _t0 = t0;

        int taken = 0;
        if (steps > 0)
        {
            for (; taken < steps; taken++)
                _z80->Z80Step();
        }
        else
        {
            while ((_z80->t - _t0) < tc.expTotal && taken++ < 64)
                _z80->Z80Step();
        }
        return taken;
    }
};

TEST_F(MemoryContendedFuse_Test, NothingWaitsInUncontendedPages)
{
    auto cases = FuseVectors::LoadCases();
    ASSERT_GT(cases.size(), 1000u) << "FUSE vectors not found";
    Layout(0);
    ASSERT_FALSE(_memory->IsBank0ROM()) << "all-RAM layout expected";
    ASSERT_FALSE(_context->pCore->IsSlotContended(0));
    ASSERT_EQ(_z80->MemIf, _z80->FastContendedMemIf) << "the machine is contended, only the pages are not";

    int failed = 0;
    std::string report;
    for (const auto& [name, tc] : cases)
    {
        if (tc.expTotal == 0)
            continue;
        Run(tc, _firstContendedT, 0);  // in the paper: only the placement keeps the waits away
        std::vector<std::string> issues;
        if (_z80->t - _t0 != tc.expTotal)
            issues.push_back("total " + std::to_string(_z80->t - _t0) + " != " + std::to_string(tc.expTotal));
        FuseVectors::CompareTrace(_trace, tc, issues);
        FuseVectors::CompareFinalState(_z80, tc, FuseVectors::SkipAFCases().count(name) > 0, issues);
        if (!issues.empty())
        {
            failed++;
            if (report.size() < 4000)
                report += "\n[" + name + "] " + issues[0];
        }
    }
    EXPECT_EQ(failed, 0) << report;
}

TEST_F(MemoryContendedFuse_Test, EveryMemoryCycleWaitsTheGateArrayPattern)
{
    auto cases = FuseVectors::LoadCases();
    ASSERT_GT(cases.size(), 1000u) << "FUSE vectors not found";

    int checked = 0;
    int failed = 0;
    uint64_t waited = 0;
    std::string report;
    for (const auto& [name, tc] : cases)
    {
        if (tc.expTotal == 0)
            continue;

        // Reference: the same machine and T-states, nothing contended (verified against FUSE above)
        Layout(0);
        const int steps = Run(tc, _firstContendedT, 0);
        const std::vector<FuseVectors::BusEvent> reference = _trace;
        const uint32_t referenceTotal = _z80->t - _t0;

        Layout(1);
        ASSERT_TRUE(_context->pCore->IsSlotContended(0));
        for (uint32_t offset : StartOffsets())
        {
            const uint32_t t0 = _firstContendedT + offset;

            // Prediction: every memory cycle waits the oracle's amount at its (already delayed) start
            std::vector<FuseVectors::BusEvent> predicted;
            uint32_t delay = 0;
            for (FuseVectors::BusEvent ev : reference)
            {
                if (ev.type == 'R' || ev.type == 'W')
                    delay += OracleWait(t0 + ev.tOffset - 3 + delay);  // R/W fire at cycle start + 3
                ev.tOffset += delay;
                predicted.push_back(ev);
            }

            Run(tc, t0, steps);
            std::vector<std::string> issues;
            if (_z80->t - _t0 != referenceTotal + delay)
                issues.push_back("total " + std::to_string(_z80->t - _t0) + " != " + std::to_string(referenceTotal + delay));
            FuseVectors::CompareEvents(_trace, predicted, issues);
            FuseVectors::CompareFinalState(_z80, tc, FuseVectors::SkipAFCases().count(name) > 0, issues);
            checked++;
            waited += delay;
            if (!issues.empty())
            {
                failed++;
                if (report.size() < 4000)
                    report += "\n[" + name + " +" + std::to_string(offset) + "] " + issues[0];
            }
        }
    }

    EXPECT_EQ(failed, 0) << checked << " runs" << report;
    EXPECT_GT(waited, 0u) << "the contended runs never waited: the layout did not take effect";
}

TEST_F(MemoryContendedFuse_Test, SwitchOffRunsTheContendedLayoutUncontended)
{
    auto cases = FuseVectors::LoadCases();
    ASSERT_GT(cases.size(), 1000u) << "FUSE vectors not found";
    Layout(1);
    ASSERT_TRUE(_context->pFeatureManager->setFeature(Features::kContention, false));
    ASSERT_EQ(_z80->MemIf, _z80->FastMemIf);

    int failed = 0;
    std::string report;
    for (const auto& [name, tc] : cases)
    {
        if (tc.expTotal == 0)
            continue;
        Run(tc, _firstContendedT, 0);
        std::vector<std::string> issues;
        if (_z80->t - _t0 != tc.expTotal)
            issues.push_back("total " + std::to_string(_z80->t - _t0) + " != " + std::to_string(tc.expTotal));
        FuseVectors::CompareTrace(_trace, tc, issues);
        if (!issues.empty())
        {
            failed++;
            if (report.size() < 4000)
                report += "\n[" + name + "] " + issues[0];
        }
    }
    EXPECT_EQ(failed, 0) << report;
}

/// The debug path with contention: a TTD recording of code running from contended RAM replays bit-exact.
/// The program (DI; LD HL,#5000; loop: INC (HL); LD A,(HL); JR loop at #6000) counts in contended RAM with
/// interrupts off, so the counter and PC at every point depend on every fetch and data wait. Frame starts are
/// restored from checkpoints; mid-frame seeks re-execute in replay mode, which must stay contended
TEST(MemoryContendedDebugPath_Test, TtdReplaysCodeInContendedRamBitExact)
{
    auto makeMachine = [](bool contention) {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        if (!emulator)
            return emulator;
        FeatureManager* fm = emulator->GetFeatureManager();
        fm->setFeature(Features::kContention, contention);
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        EmulatorContext* context = emulator->GetContext();
        context->pMemory->UpdateFeatureCache();
        const uint8_t program[] = { 0xF3, 0x21, 0x00, 0x50, 0x34, 0x7E, 0x18, 0xFC };
        for (size_t i = 0; i < sizeof(program); i++)
            context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x6000 + i), program[i]);
        context->pMemory->DirectWriteToZ80Memory(0x5000, 0);
        context->pCore->GetZ80()->pc = 0x6000;
        return emulator;
    };

    constexpr size_t kFrames = 30;

    // Machine state at a frame boundary: CPU position and all RAM the CPU sees above the ROM
    auto stateHash = [](EmulatorContext* context) {
        Z80* z80 = context->pCore->GetZ80();
        uint64_t h = 1469598103934665603ull;
        auto add = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        add(z80->pc);
        add(z80->hl);
        add(z80->af);
        add(z80->t);
        for (uint32_t addr = 0x4000; addr < 0x10000; addr++)
            add(context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(addr)));
        return h;
    };

    Emulator* contended = makeMachine(true);
    ASSERT_NE(contended, nullptr);
    EmulatorContext* context = contended->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
    ASSERT_NE(ttd, nullptr);

    ASSERT_TRUE(ttd->StartRecording());
    EXPECT_EQ(z80->MemIf, z80->DbgContendedMemIf) << "recorded on the contended debug path";
    std::vector<std::pair<uint64_t, uint64_t>> live;  // (frame counter, state hash) at each frame start
    for (size_t i = 0; i < kFrames; i++)
    {
        contended->RunFrame(true);
        live.push_back({ context->emulatorState.frame_counter, stateHash(context) });
    }
    ttd->StopRecording();
    const uint8_t contendedCount = context->pMemory->DirectReadFromZ80Memory(0x5000);

    // Frame starts: restored from checkpoints
    for (size_t i = 0; i < kFrames; i += 7)
    {
        SCOPED_TRACE("frame " + std::to_string(live[i].first));
        ASSERT_TRUE(ttd->SeekTo(ttd::TTDTimePoint{ live[i].first, 0 }));
        EXPECT_EQ(stateHash(context), live[i].second);
    }

    // Mid-frame, in the paper: the seek re-executes from the frame's checkpoint in replay mode. The same
    // stretch stepped on the live path from the restored frame start must end in the same state
    const uint32_t midFrame = context->config.intstart + 1 + 14335 + 40 * 224;
    for (size_t i = 3; i < kFrames; i += 9)
    {
        SCOPED_TRACE("frame " + std::to_string(live[i].first) + " mid");
        ASSERT_TRUE(ttd->SeekTo(ttd::TTDTimePoint{ live[i].first, midFrame }));
        const uint64_t replayed = stateHash(context);
        const uint32_t replayedT = z80->t;

        ASSERT_TRUE(ttd->SeekTo(ttd::TTDTimePoint{ live[i].first, 0 }));
        while (z80->t < replayedT)
            z80->Z80Step();
        EXPECT_EQ(z80->t, replayedT);
        EXPECT_EQ(stateHash(context), replayed);
    }
    EmulatorTestHelper::CleanupEmulator(contended);

    // The same run uncontended ends elsewhere: the replay above really depended on the waits
    Emulator* free = makeMachine(false);
    ASSERT_NE(free, nullptr);
    for (size_t i = 0; i < kFrames; i++)
        free->RunFrame(true);
    EXPECT_NE(free->GetContext()->pMemory->DirectReadFromZ80Memory(0x5000), contendedCount);
    EmulatorTestHelper::CleanupEmulator(free);
}
