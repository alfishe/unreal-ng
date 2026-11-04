#include <gtest/gtest.h>

#include "_helpers/testwaithelper.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/watch/framewatch.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

/// Frame watches (docs/inprogress/2026-11-03-run-until-watches, plan.md W1-W7): run_until on a counter program (one
/// increment a frame), the screen rules, the ROM-font text, the refusals, and an armed watch on a running 48K ROM
class FrameWatch_Test : public ::testing::Test
{
protected:
    static constexpr uint16_t kOrg = 0x8000;
    static constexpr uint16_t kIsr = 0xFDFD;  // the IM 2 handler: I = #FE, the table at #FE00 all #FD

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    void Create()
    {
        if (_emulator)  // a second machine in one test: the first goes
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("framewatch", "48K", LoggerLevel::LogNone);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _emulator->EnableTurboMode();
    }

    /// di; ld a,#FE; ld i,a; im 2; ei; loop: halt; ld hl,<counter>; inc (hl); jr loop (one increment a frame)
    void Counter(uint16_t counter)
    {
        ASSERT_NO_FATAL_FAILURE(Create());
        Memory* memory = _context->pMemory;
        const std::vector<uint8_t> code = {0xF3, 0x3E, 0xFE, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x21,
                                           static_cast<uint8_t>(counter & 0xFF), static_cast<uint8_t>(counter >> 8),
                                           0x34, 0x18, 0xF9};
        for (size_t i = 0; i < code.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kOrg + i), code[i]);
        memory->DirectWriteToZ80Memory(counter, 0x00);
        for (uint16_t i = 0; i <= 0x100; ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xFE00 + i), 0xFD);
        const uint8_t isr[] = {0xFB, 0xED, 0x4D};  // ei; reti
        for (size_t i = 0; i < sizeof(isr); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kIsr + i), isr[i]);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = kOrg;
        z80->sp = 0xBF00;
        z80->iff1 = z80->iff2 = 0;
        z80->halted = 0;
    }

    framewatch::RunResult Run(std::vector<std::pair<std::string, std::string>> options)
    {
        framewatch::Spec spec;
        unsigned maxFrames = 0;
        bool breakpoints = false;
        std::string error;
        framewatch::RunResult result;
        if (!framewatch::RunOptions(options, spec, maxFrames, breakpoints, error))
        {
            result.stop = "error";
            result.error = error;
            return result;
        }
        return framewatch::RunUntil(_context, spec, maxFrames, breakpoints);
    }

    uint8_t Peek(uint16_t address) const { return _context->pMemory->DirectReadFromZ80Memory(address); }
};

/// W1: a frame-end condition holds after exactly the frames it needs; the run stops at a frame boundary
TEST_F(FrameWatch_Test, ConditionStopsAtTheFirstFrameEndWhereItHolds)
{
    ASSERT_NO_FATAL_FAILURE(Counter(0xA000));
    // Settle into the loop: the first frame ends inside the setup, the counter starts counting at the next INT
    _emulator->RunNFrames(2);
    const uint8_t start = Peek(0xA000);
    const framewatch::RunResult r = Run({{"if", "M(#A000) >= " + std::to_string(start + 10) + "d"}, {"max_frames", "100"}});
    ASSERT_EQ(r.stop, "met") << r.error;
    EXPECT_TRUE(r.met);
    EXPECT_EQ(r.frames, 10u) << "one increment a frame";
    EXPECT_EQ(Peek(0xA000), start + 10);
    ASSERT_EQ(r.hit.reasons.size(), 1u);
    EXPECT_EQ(r.hit.reasons[0], "if");
    EXPECT_LT(_context->pCore->GetZ80()->t, 200u) << "paused at the next frame's start";
    EXPECT_NE(framewatch::RunResultText(r).find("met at frame"), std::string::npos) << framewatch::RunResultText(r);
}

/// W2: the budget ends a run whose rule never holds; x@prev sees every frame
TEST_F(FrameWatch_Test, BudgetAndPrevious)
{
    ASSERT_NO_FATAL_FAILURE(Counter(0xA000));
    _emulator->RunNFrames(2);
    framewatch::RunResult r = Run({{"if", "M(#A000) == 1"}, {"max_frames", "7"}});
    EXPECT_EQ(r.stop, "budget");
    EXPECT_FALSE(r.met);
    EXPECT_EQ(r.frames, 7u);
    EXPECT_NE(framewatch::RunResultText(r).find("not met in 7 frames"), std::string::npos);

    // The byte went up since the previous check: @prev takes its value at the start, so the first frame's increment
    // holds at its end
    r = Run({{"if", "M(#A000) > M(#A000)@prev"}, {"max_frames", "10"}});
    ASSERT_EQ(r.stop, "met") << r.error;
    EXPECT_EQ(r.frames, 1u) << "the start primes @prev, the first frame end sees the increment";
}

/// W3: the screen changing (a counter in the screen's first byte) and holding still (a counter outside it)
TEST_F(FrameWatch_Test, ScreenChangedAndStable)
{
    ASSERT_NO_FATAL_FAILURE(Counter(0x4000));
    _emulator->RunNFrames(2);
    framewatch::RunResult r = Run({{"screen", "changed"}, {"max_frames", "10"}});
    ASSERT_EQ(r.stop, "met") << r.error;
    EXPECT_EQ(r.frames, 1u);
    EXPECT_EQ(r.hit.reasons[0], "screen");
    r = Run({{"screen", "stable"}, {"stable_frames", "5"}, {"max_frames", "20"}});
    EXPECT_EQ(r.stop, "budget") << "the counter changes the screen every frame";

    ASSERT_NO_FATAL_FAILURE(Counter(0xA000));
    _emulator->RunNFrames(2);
    r = Run({{"screen", "stable"}, {"stable_frames", "5"}, {"max_frames", "20"}});
    ASSERT_EQ(r.stop, "met") << r.error;
    EXPECT_EQ(r.frames, 5u);
    EXPECT_EQ(r.hit.stableFor, 5u);
}

/// W3b: the booted 48K ROM's copyright screen holds still although its interrupt moves FRAMES and the stack in
/// page 5 every frame: the rule hashes the displayed 6,912 bytes, not the page
TEST_F(FrameWatch_Test, StableUnderTheRomInterrupt)
{
    ASSERT_NO_FATAL_FAILURE(Create());
    _emulator->RunNFrames(150);  // booted: the copyright line, nothing moves on screen
    const uint8_t frames = Peek(0x5C78);
    const framewatch::RunResult r = Run({{"screen", "stable"}, {"stable_frames", "10"}, {"max_frames", "30"}});
    ASSERT_EQ(r.stop, "met") << r.error;
    EXPECT_EQ(r.frames, 10u);
    EXPECT_NE(Peek(0x5C78), frames) << "FRAMES moved meanwhile";
}

/// W4: the ROM-font text on screen: found with its row, absent until the budget; all needs every rule
TEST_F(FrameWatch_Test, TextOnScreenAndMatchAll)
{
    ASSERT_NO_FATAL_FAILURE(Counter(0xA000));
    Memory* memory = _context->pMemory;
    for (uint16_t a = 0x4000; a < 0x5800; ++a)  // a blank screen: power-on RAM reads as '?' cells
        memory->DirectWriteToZ80Memory(a, 0x00);
    for (uint16_t a = 0x5800; a < 0x5B00; ++a)
        memory->DirectWriteToZ80Memory(a, 0x38);
    // "HI" in the ROM font (#3D00 + (c - 32) * 8) at row 1, columns 3-4: pixel line n of cell (1, c) is at
    // #4000 + n * 256 + 32 + c
    const char* text = "HI";
    for (int i = 0; i < 2; i++)
        for (int line = 0; line < 8; line++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x4000 + line * 256 + 32 + 3 + i),
                                           memory->DirectReadFromZ80Memory(
                                               static_cast<uint16_t>(0x3D00 + (text[i] - 32) * 8 + line)));
    framewatch::RunResult r = Run({{"text", "HI"}, {"max_frames", "5"}});
    ASSERT_EQ(r.stop, "met") << r.error;
    EXPECT_EQ(r.frames, 1u);
    EXPECT_EQ(r.hit.textLine, "HI");
    r = Run({{"text", "HELLO"}, {"max_frames", "3"}});
    EXPECT_EQ(r.stop, "budget");
    r = Run({{"text", "HI"}, {"if", "M(#A000) == 250"}, {"match", "all"}, {"max_frames", "3"}});
    EXPECT_EQ(r.stop, "budget") << "the text alone is not enough with match all";
}

/// W5: the refusals: no rule, an unknown option, a watch option, a condition that does not compile, no game event
TEST_F(FrameWatch_Test, Refusals)
{
    ASSERT_NO_FATAL_FAILURE(Counter(0xA000));
    EXPECT_NE(Run({{"max_frames", "5"}}).error.find("nothing to wait for"), std::string::npos);
    EXPECT_NE(Run({{"colour", "red"}}).error.find("unknown option 'colour'"), std::string::npos);
    EXPECT_NE(Run({{"text", "A"}, {"then", "pause"}}).error.find("armed watch"), std::string::npos);
    EXPECT_NE(Run({{"text", "A"}, {"max_frames", "0"}}).error.find("max_frames"), std::string::npos);
    const framewatch::RunResult bad = Run({{"if", "NOSUCH == 1"}});
    EXPECT_EQ(bad.stop, "error");
    EXPECT_NE(bad.error.find("if:"), std::string::npos) << bad.error;
    EXPECT_EQ(bad.frames, 0u) << "a refused spec runs nothing";
    EXPECT_NE(Run({{"event", "died"}}).error.find("no game event 'died'"), std::string::npos);
    EXPECT_NE(Run({{"screen", "blue"}}).error.find("screen:"), std::string::npos);
}

/// W6: the verbs every plane calls: add, list, remove; a hit in a direct run is recorded and a once watch goes
TEST_F(FrameWatch_Test, ArmedWatchVerbsAndAHitInADirectRun)
{
    ASSERT_NO_FATAL_FAILURE(Counter(0xA000));
    _emulator->RunNFrames(2);
    const uint8_t start = Peek(0xA000);
    framewatch::Outcome added =
        framewatch::AddWatch(_context, {{"if", "M(#A000) >= " + std::to_string(start + 3) + "d"}, {"label", "three"}});
    ASSERT_EQ(added.status, 201) << added.text;
    const framewatch::Outcome kept = framewatch::AddWatch(_context, {{"text", "NEVER"}, {"once", "false"}});
    ASSERT_EQ(kept.status, 201) << kept.text;
    EXPECT_EQ(framewatch::AddWatch(_context, {{"then", "stop"}, {"text", "A"}}).status, 400);
    framewatch::Outcome list = framewatch::ListWatches(_context);
    EXPECT_NE(list.text.find("2 watch(es)"), std::string::npos) << list.text;
    EXPECT_NE(list.text.find("three"), std::string::npos) << list.text;

    // A direct run checks the watches at its frame ends too, and is not paused by them (it ends paused itself)
    _emulator->RunNFrames(6);
    list = framewatch::ListWatches(_context);
    EXPECT_NE(list.text.find("1 watch(es)"), std::string::npos) << "the once watch went with its hit: " << list.text;
    framewatch::WatchInfo last;
    EXPECT_FALSE(_context->pDebugManager->GetFrameWatches()->LastPause(last)) << "a direct run is not paused by a watch";

    EXPECT_EQ(framewatch::RemoveWatch(_context, "99").status, 404);
    const framewatch::Outcome removed = framewatch::RemoveWatch(_context, "all");
    EXPECT_EQ(removed.status, 200);
    EXPECT_EQ(removed.text, "1 watch(es) removed");
    EXPECT_FALSE(_context->pDebugManager->GetFrameWatches()->Armed()) << "off again: nothing checked per frame";
}

/// W7: an armed watch pauses the running machine at its hit's frame end, and the stop names the watch
TEST_F(FrameWatch_Test, ArmedWatchPausesTheRunningMachine)
{
    ASSERT_NO_FATAL_FAILURE(Create());
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsRunning(); }));
    // The 48K ROM counts FRAMES (#5C78) at every interrupt once it has booted (power-on RAM before that: == not >=)
    const framewatch::Outcome added = framewatch::AddWatch(_context, {{"if", "W(#5C78) == 60"}, {"label", "boot"}});
    ASSERT_EQ(added.status, 201) << added.text;
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsPaused(); })) << "the watch pauses the machine";
    EXPECT_EQ(_emulator->LastStop().reason, Emulator::DebugStop::Reason::Watch);
    framewatch::WatchInfo last;
    ASSERT_TRUE(_context->pDebugManager->GetFrameWatches()->LastPause(last));
    EXPECT_EQ(last.spec.label, "boot");
    ASSERT_EQ(last.lastHit.reasons.size(), 1u);
    EXPECT_EQ(last.lastHit.reasons[0], "if");
    const uint16_t frames = static_cast<uint16_t>(Peek(0x5C78) | (Peek(0x5C79) << 8));
    EXPECT_EQ(frames, 60u) << "paused at that frame end, not frames later";
    EXPECT_FALSE(_context->pDebugManager->GetFrameWatches()->Armed()) << "a once watch goes with its hit";
}
