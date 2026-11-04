/// @file accesstrace_test.cpp
/// @brief The CPU access trace of a window of a recording (accesstrace.h): a small program runs under a TTD
/// recording, a window of it is traced on an analysis instance, and the file is read back: the header and the counts,
/// the start memory, each instruction's fetches are its own bytes, the writes and port writes with the instruction
/// that made them, the window's bounds, the cap, and the start memory plus the writes give the memory the next
/// window starts from.

#include "pch.h"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analysis/accesstrace.h"
#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/analyzers/analysis/replaywindow.h"
#include "debugger/assembler/z80textassembler.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

using namespace analysis;

namespace
{

/// Fills 32 bytes of the screen with B (32 down to 1) and writes B & 7 to the border, every pass
constexpr const char* kProgram = R"(
start:  ld hl,$4000
        ld b,32
fill:   ld (hl),b
        inc hl
        ld a,b
        and 7
        out ($fe),a
        djnz fill
        ld a,(count)
        inc a
        ld (count),a
        jr start
count:  db 0
)";

struct TracedInstruction
{
    uint32_t frame = 0;
    uint32_t t = 0;
    uint16_t pc = 0;
    uint16_t regs[7] = {};
    uint8_t flags = 0;
};

struct Cycle
{
    char tag = 0;
    uint16_t address = 0;
    uint8_t value = 0;
    uint16_t dt = 0;
    size_t instruction = 0;  ///< the index of the instruction record before it
};

struct TraceFile
{
    bool ok = false;
    uint64_t fromFrame = 0, toFrame = 0, instructions = 0, cycles = 0;
    uint32_t fromT = 0, toT = 0, unitsPerT = 0, frameT = 0;
    bool truncated = false;
    uint16_t pages[4] = {};
    std::vector<uint8_t> memory;
    std::vector<TracedInstruction> code;
    std::vector<Cycle> bus;
};

uint16_t U16(const std::vector<uint8_t>& b, size_t at)
{
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}
uint32_t U32(const std::vector<uint8_t>& b, size_t at)
{
    return U16(b, at) | (static_cast<uint32_t>(U16(b, at + 2)) << 16);
}
uint64_t U64(const std::vector<uint8_t>& b, size_t at)
{
    return U32(b, at) | (static_cast<uint64_t>(U32(b, at + 4)) << 32);
}

TraceFile ReadTrace(const std::string& path)
{
    TraceFile f;
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() < 64 + 8 + 0x10000 || std::memcmp(b.data(), "UATR", 4) != 0 || U16(b, 4) != 1 || U16(b, 6) != 64)
        return f;
    f.fromFrame = U64(b, 8);
    f.fromT = U32(b, 16);
    f.unitsPerT = U32(b, 20);
    f.toFrame = U64(b, 24);
    f.toT = U32(b, 32);
    f.frameT = U32(b, 36);
    f.instructions = U64(b, 40);
    f.cycles = U64(b, 48);
    f.truncated = b[56] != 0;
    size_t at = 64;
    for (int i = 0; i < 4; ++i, at += 2)
        f.pages[i] = U16(b, at);
    f.memory.assign(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at + 0x10000));
    at += 0x10000;
    while (at < b.size())
    {
        const char tag = static_cast<char>(b[at]);
        if (tag == 'X')
        {
            if (at + 26 > b.size())
                return f;
            TracedInstruction x;
            x.frame = U32(b, at + 1);
            x.t = U32(b, at + 5);
            x.pc = U16(b, at + 9);
            for (int i = 0; i < 7; ++i)
                x.regs[i] = U16(b, at + 11 + 2 * static_cast<size_t>(i));
            x.flags = b[at + 25];
            f.code.push_back(x);
            at += 26;
        }
        else
        {
            if (at + 6 > b.size() || f.code.empty())
                return f;
            f.bus.push_back(Cycle{tag, U16(b, at + 1), b[at + 3], U16(b, at + 4), f.code.size() - 1});
            at += 6;
        }
    }
    f.ok = true;
    return f;
}

}  // namespace

class AccessTrace_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    AsmResult _program;
    TraceBundle _bundle;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    void Record(unsigned frames)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _emulator->GetFeatureManager()->setFeature(Features::kSoundGeneration, false);
        if (_context->pSoundManager)
            _context->pSoundManager->mute();
        _context->pMemory->UpdateFeatureCache();
        _program = Z80TextAssembler().Assemble(kProgram, 0x8000);
        ASSERT_TRUE(_program.ok) << _program.error.line << ": " << _program.error.message;
        for (const AsmSourceLine& line : _program.lines)
            for (size_t i = 0; i < line.bytes.size(); ++i)
                _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(line.address + i), line.bytes[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0x9F00;
        z80->iff1 = 0;
        z80->iff2 = 0;
        ttd::TimeTravelController* ttd = _context->pTimeTravelController;
        ASSERT_NE(ttd, nullptr);
        ASSERT_TRUE(ttd->StartRecording()) << ttd->LastStartError();
        _emulator->RunNFrames(frames);
        std::string error;
        ASSERT_TRUE(TraceBundle::Capture(*_emulator, _bundle, error)) << error;
    }

    uint16_t Symbol(const std::string& name) const
    {
        const auto it = _program.symbols.find(name);
        EXPECT_NE(it, _program.symbols.end()) << name;
        return it != _program.symbols.end() ? static_cast<uint16_t>(it->second) : 0;
    }

    TraceFile Trace(AccessTraceOptions options, AccessTraceSummary& summary, const std::string& name)
    {
        options.path = TestPathHelper::GetUniqueTestScratchPath(name);
        std::string error;
        EXPECT_TRUE(ReplayAccessTrace(_bundle, options, summary, error)) << error;
        return ReadTrace(options.path);
    }
};

TEST_F(AccessTrace_Test, EveryAccessWithItsInstruction)
{
    Record(4);
    ASSERT_FALSE(HasFatalFailure());
    const uint64_t first = _bundle.firstFrame;
    AccessTraceOptions options;
    options.fromFrame = first + 1;
    options.fromTInFrame = 1000;
    options.toFrame = first + 2;
    options.toTInFrame = 500;
    AccessTraceSummary summary;
    const TraceFile f = Trace(options, summary, "pass.uatr");
    ASSERT_TRUE(f.ok);
    EXPECT_EQ(f.fromFrame, first + 1);
    EXPECT_EQ(f.fromT, 1000u);
    EXPECT_EQ(f.toFrame, first + 2);
    EXPECT_EQ(f.toT, 500u);
    EXPECT_EQ(f.unitsPerT, 1u);
    EXPECT_EQ(f.frameT, 69888u);
    EXPECT_FALSE(f.truncated);
    EXPECT_EQ(f.instructions, f.code.size());
    EXPECT_EQ(f.cycles, f.bus.size());
    EXPECT_EQ(summary.instructions, f.code.size());
    EXPECT_EQ(summary.fetches + summary.reads + summary.writes + summary.ports, summary.cycles);
    // The window is 69888 - 1000 + 500 T; the loop's instructions take 4 to 13 T (more under contention)
    const size_t windowT = 69888 - 1000 + 500;
    ASSERT_GT(f.code.size(), windowT / 16) << "a frame and a bit of the loop";
    ASSERT_LT(f.code.size(), windowT / 4);

    // The window's bounds: the first instruction at or after from, the last before to
    EXPECT_TRUE(f.code.front().frame == first + 1 && f.code.front().t >= 1000);
    EXPECT_TRUE(f.code.front().t < 1100) << "the first instruction starts right at the window";
    const TracedInstruction& last = f.code.back();
    EXPECT_TRUE(last.frame == first + 1 || (last.frame == first + 2 && last.t < 500));

    // Each instruction's fetches are its own bytes, from its PC up; the program's bytes never change
    std::vector<size_t> fetched(f.code.size(), 0);
    std::vector<uint8_t> memory = f.memory;
    for (const Cycle& c : f.bus)
    {
        const TracedInstruction& x = f.code[c.instruction];
        if (c.tag == 'F')
        {
            EXPECT_EQ(c.address, static_cast<uint16_t>(x.pc + fetched[c.instruction])) << "pc " << x.pc;
            EXPECT_EQ(c.value, memory[c.address]);
            ++fetched[c.instruction];
        }
        else if (c.tag == 'R')
            EXPECT_EQ(c.value, memory[c.address]) << "a read sees the memory the trace rebuilt";
        else if (c.tag == 'W')
            memory[c.address] = c.value;
    }
    for (size_t i = 0; i < f.code.size(); ++i)
        ASSERT_GE(fetched[i], 1u) << "instruction " << i << " at " << f.code[i].pc;

    // The writes: LD (HL),B writes B at HL; OUT ($FE),A writes B & 7 to the port; the counter by LD (count),A
    const uint16_t fill = Symbol("fill");
    const uint16_t count = Symbol("count");
    size_t screenWrites = 0, portWrites = 0, countWrites = 0;
    for (const Cycle& c : f.bus)
    {
        const TracedInstruction& x = f.code[c.instruction];
        if (c.tag == 'W' && x.pc == fill)
        {
            const uint16_t bc = x.regs[2], hl = x.regs[4];  // sp, af, bc, de, hl, ix, iy
            EXPECT_EQ(c.address, hl);
            EXPECT_EQ(c.value, bc >> 8);
            EXPECT_EQ(c.address - 0x4000, 32 - c.value) << "B counts down from 32 as HL climbs from $4000";
            EXPECT_GE(c.dt, 4u);
            ++screenWrites;
        }
        else if (c.tag == 'O')
        {
            EXPECT_EQ(c.address & 0xFF, 0xFE);
            EXPECT_EQ(c.value, (f.code[c.instruction].regs[1] >> 8) & 7);
            ++portWrites;
        }
        else if (c.tag == 'W' && c.address == count)
            ++countWrites;
    }
    EXPECT_GT(screenWrites, 500u);
    EXPECT_NEAR(static_cast<double>(portWrites), static_cast<double>(screenWrites), 1.0)
        << "one OUT a byte filled, give or take the window's edges";
    EXPECT_GT(countWrites, 10u);

    // The start memory plus the writes is the memory the next window starts from
    AccessTraceOptions next;
    next.fromFrame = first + 2;
    next.fromTInFrame = 500;
    next.toFrame = first + 2;
    next.toTInFrame = 600;
    AccessTraceSummary nextSummary;
    const TraceFile g = Trace(next, nextSummary, "next.uatr");
    ASSERT_TRUE(g.ok);
    size_t differ = 0;
    for (uint32_t a = 0x4000; a < 0x10000; ++a)
        differ += memory[a] != g.memory[a];
    EXPECT_EQ(differ, 0u);
}

TEST_F(AccessTrace_Test, CapAndRefusals)
{
    Record(3);
    ASSERT_FALSE(HasFatalFailure());
    AccessTraceOptions options;
    options.fromFrame = _bundle.firstFrame + 1;
    options.maxCycles = 100;
    AccessTraceSummary summary;
    const TraceFile f = Trace(options, summary, "capped.uatr");
    ASSERT_TRUE(f.ok);
    EXPECT_TRUE(f.truncated);
    EXPECT_TRUE(summary.truncated);
    EXPECT_EQ(f.bus.size(), 100u);

    std::string error;
    AccessTraceOptions outside;
    outside.fromFrame = _bundle.endFrame + 10;
    outside.path = TestPathHelper::GetUniqueTestScratchPath("outside.uatr");
    EXPECT_FALSE(ReplayAccessTrace(_bundle, outside, summary, error));
    EXPECT_NE(error.find("no moment"), std::string::npos) << error;

    // Through the action every surface shares: a path is required, a reversed window is refused
    const Reply noPath = Execute(*_emulator, "trace", {{"from_frame", "1"}});
    EXPECT_EQ(noPath.error, Error::BadRequest);
    const Reply reversed = Execute(*_emulator, "trace", {{"path", outside.path}, {"from_frame", "5"}, {"to_frame", "3"}});
    EXPECT_EQ(reversed.error, Error::BadRequest);
    const Reply ok = Execute(*_emulator, "trace",
                             {{"path", outside.path}, {"from_frame", std::to_string(_bundle.firstFrame + 1)}});
    ASSERT_TRUE(ok.Ok()) << ok.message;
    EXPECT_NE(ok.text.find("instructions"), std::string::npos) << ok.text;
    EXPECT_TRUE(ReadTrace(outside.path).ok);
}

/// The shared replay (replaywindow.h): a start before the first keyframe starts there, the hooks hear the run and
/// are off after it, a Stop() from a hook ends the run at the frame boundary, the chunk callback sees each boundary
TEST_F(AccessTrace_Test, ReplayWindowRunsHooksStopsAndCleansUp)
{
    Record(4);
    ASSERT_FALSE(HasFatalFailure());
    const uint64_t first = _bundle.firstFrame;
    std::string error;

    ReplayWindow window;
    ASSERT_TRUE(window.Open(_bundle, "replay-window-test", ttd::TTDTimePoint{0, 0}, error)) << error;
    EXPECT_EQ(window.Start().frame, window.First().frame) << "nothing exists before the first keyframe";
    EXPECT_EQ(window.Start().frame, first);
    EXPECT_EQ(window.FrameT(), 69888u);

    uint64_t instructions = 0, buses = 0;
    std::vector<uint64_t> boundaries;
    ReplayHooks hooks;
    hooks.instruction = [&](uint16_t pc) {
        ++instructions;
        EXPECT_TRUE(pc >= 0x8000 && pc < 0x8100) << "the program's code only, interrupts are off";
    };
    hooks.bus = [&](char, uint16_t, uint8_t) { ++buses; };
    hooks.chunk = [&](uint64_t frame) {
        boundaries.push_back(frame);
        return true;
    };
    EXPECT_EQ(window.RunTo(first + 2, 1, hooks), ReplayWindow::Run::Finished);
    EXPECT_EQ(window.State().frame_counter, first + 2);
    EXPECT_EQ(boundaries, (std::vector<uint64_t>{first + 1, first + 2}));
    EXPECT_GT(instructions, 2 * 69888u / 16);
    EXPECT_GT(buses, instructions) << "every instruction fetches at least its opcode";
    EXPECT_FALSE(window.Cpu()->m1TraceHook) << "the hooks are the run's only";
    EXPECT_FALSE(window.Cpu()->busTraceHook);

    // A stop from a hook lands at the next frame boundary
    instructions = 0;
    hooks.chunk = nullptr;
    hooks.instruction = [&](uint16_t) {
        if (++instructions == 100)
            window.Stop();
    };
    EXPECT_EQ(window.RunTo(first + 4, 1, hooks), ReplayWindow::Run::Stopped);
    EXPECT_EQ(window.State().frame_counter, first + 3);
    EXPECT_FALSE(window.Cpu()->m1TraceHook);

    // The chunk callback's false is a stop too (a cancelled build)
    hooks.instruction = nullptr;
    hooks.chunk = [](uint64_t) { return false; };
    EXPECT_EQ(window.RunTo(first + 4, 1, hooks), ReplayWindow::Run::Stopped);

    // A start past the recording's end is not sought: the consumer's window is empty
    ReplayWindow late;
    ASSERT_TRUE(late.Open(_bundle, "replay-window-late", ttd::TTDTimePoint{first + 1000, 0}, error)) << error;
    EXPECT_EQ(late.Start().frame, first + 1000);
    EXPECT_FALSE(late.Start().frame < late.SessionEnd().frame);
}
