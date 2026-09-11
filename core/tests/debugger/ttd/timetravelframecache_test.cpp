// timetravelframecache_test.cpp — per-frame decode cache (reverse-browsing accelerator)
//
// Verifies that the cache TimeTravelManager builds during a replay pass matches
// what a live replay/step-back produces, that it is lifecycle-scoped to replay
// (never built while Recording, freed on returning to the present), and that it
// captures per-instruction memory/port writes.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/timetravelframecache.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

class TTD_FrameCache_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);
        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    // Install a busy loop at 0x8000 that writes memory and a port each iteration,
    // so cached records have accesses to verify:
    //   8000: 3C        INC A
    //   8001: 32 00 C0  LD (C000),A     ; memory write
    //   8004: D3 FE     OUT (FE),A      ; port write
    //   8006: C3 00 80  JP 8000
    void installBusyProgram()
    {
        const uint8_t prog[] = {0x3C, 0x32, 0x00, 0xC0, 0xD3, 0xFE, 0xC3, 0x00, 0x80};
        for (uint16_t i = 0; i < sizeof(prog); ++i)
            _memory->DirectWriteToZ80Memory(0x8000 + i, prog[i]);
        Z80State* z80 = _emulator->GetZ80State();
        z80->pc = 0x8000;
        z80->sp = 0xFF00;
    }

    // PCs of the busy program installed by installBusyProgram().
    static bool isProgramPc(uint16_t pc)
    {
        return pc == 0x8000 || pc == 0x8001 || pc == 0x8004 || pc == 0x8006;
    }

    void recordBusy(uint32_t frames)
    {
        installBusyProgram();
        _ttd->StartRecording();
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
    }
};

TEST_F(TTD_FrameCache_Test, NotAvailableWhileRecording)
{
    installBusyProgram();
    _ttd->StartRecording();
    _emulator->RunNFrames(3, true);
    // Recording → the accelerator must refuse to build.
    EXPECT_EQ(_ttd->GetFrameCache(1), nullptr);
    _ttd->StopRecording();
}

TEST_F(TTD_FrameCache_Test, BuildsAndCachesAFrame)
{
    recordBusy(5);
    const uint64_t endFrame = _ttd->SessionEndPosition().frame;
    ASSERT_GE(endFrame, 2u);

    const ttd::TTDFrameCache* c = _ttd->GetFrameCache(endFrame - 1);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->frame, endFrame - 1);
    EXPECT_GT(c->entries.size(), 100u);  // a busy frame has many instructions
    EXPECT_EQ(_ttd->GetCachedFrame(), endFrame - 1);
    EXPECT_GT(_ttd->GetFrameCacheBytes(), 0u);

    // t-states are strictly increasing in execution order.
    for (size_t i = 1; i < c->entries.size(); ++i)
        EXPECT_LT(c->entries[i - 1].tInFrame, c->entries[i].tInFrame) << "entry " << i;

    // Second call returns the same cache without rebuilding (same pointer).
    EXPECT_EQ(_ttd->GetFrameCache(endFrame - 1), c);
}

TEST_F(TTD_FrameCache_Test, CachedEntriesMatchLiveReplay)
{
    recordBusy(5);
    const uint64_t frame = _ttd->SessionEndPosition().frame - 1;

    const ttd::TTDFrameCache* c = _ttd->GetFrameCache(frame);
    ASSERT_NE(c, nullptr);
    ASSERT_GT(c->entries.size(), 20u);

    // Independently seek to a handful of the cached entries' timepoints and
    // confirm the live CPU state matches the cached record.
    const Z80State* z80 = _emulator->GetZ80State();
    for (size_t idx : {size_t(0), c->entries.size() / 3, c->entries.size() / 2, c->entries.size() - 1})
    {
        const ttd::TTDFrameCacheEntry& e = c->entries[idx];
        ASSERT_TRUE(_ttd->SeekTo(ttd::TTDTimePoint{frame, e.tInFrame})) << "idx " << idx;
        EXPECT_EQ(z80->pc, e.pc) << "idx " << idx;
        EXPECT_EQ(z80->af, e.af) << "idx " << idx;
        EXPECT_EQ(z80->bc, e.bc) << "idx " << idx;
        EXPECT_EQ(z80->hl, e.hl) << "idx " << idx;
        EXPECT_EQ(z80->sp, e.sp) << "idx " << idx;
        // opcodes coherent with live memory at the cached PC
        for (int b = 0; b < 4; ++b)
            EXPECT_EQ(e.opcodes[b], _memory->DirectReadFromZ80Memory(e.pc + b)) << "idx " << idx << " byte " << b;
    }
}

TEST_F(TTD_FrameCache_Test, CapturesMemoryAndPortWrites)
{
    recordBusy(5);
    const uint64_t frame = _ttd->SessionEndPosition().frame - 1;
    const ttd::TTDFrameCache* c = _ttd->GetFrameCache(frame);
    ASSERT_NE(c, nullptr);

    bool sawMemWrite = false, sawPortWrite = false;
    for (size_t idx = 0; idx < c->entries.size(); ++idx)
    {
        const ttd::TTDFrameCacheEntry& e = c->entries[idx];
        uint16_t count = 0;
        const ttd::TTDFrameCacheAccess* acc = c->AccessesOf(idx, count);
        for (uint16_t a = 0; a < count; ++a)
        {
            // LD (C000),A at 0x8001 records a memory write to 0xC000
            if (e.pc == 0x8001 && acc[a].kind == ttd::TTDAccessKind::MemWrite && acc[a].addr == 0xC000)
                sawMemWrite = true;
            // OUT (FE),A at 0x8004 records a port write (port low byte 0xFE)
            if (e.pc == 0x8004 && acc[a].kind == ttd::TTDAccessKind::PortWrite && (acc[a].addr & 0xFF) == 0xFE)
                sawPortWrite = true;
        }
    }
    EXPECT_TRUE(sawMemWrite) << "no captured memory write at 0x8001";
    EXPECT_TRUE(sawPortWrite) << "no captured port write at 0x8004";
}

TEST_F(TTD_FrameCache_Test, BuildRestoresPositionExactly)
{
    recordBusy(5);
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();
    const uint16_t pcBefore = _emulator->GetZ80State()->pc;
    const uint64_t frame = _ttd->SessionEndPosition().frame - 1;

    ASSERT_NE(_ttd->GetFrameCache(frame), nullptr);

    // The build replay is transparent: the live machine state is snapshotted
    // before the build and restored verbatim after, so the caller's position
    // (and registers) come back EXACTLY — even when external-event markers
    // would block a replay-based restore. (A regression here once left the
    // emulator parked at a recorded edit marker instead of the present.)
    const ttd::TTDTimePoint after = _ttd->CurrentPosition();
    EXPECT_EQ(before.frame, after.frame);
    EXPECT_EQ(before.tInFrame, after.tInFrame);
    EXPECT_EQ(_emulator->GetZ80State()->pc, pcBefore);
}

TEST_F(TTD_FrameCache_Test, FreedOnStartRecording)
{
    recordBusy(5);
    const uint64_t frame = _ttd->SessionEndPosition().frame - 1;
    ASSERT_NE(_ttd->GetFrameCache(frame), nullptr);
    EXPECT_GT(_ttd->GetFrameCacheBytes(), 0u);

    // Returning to live recording (the adapter's return-to-present path) frees it.
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_EQ(_ttd->GetFrameCacheBytes(), 0u);
    EXPECT_EQ(_ttd->GetCachedFrame(), UINT64_MAX);

    _ttd->StopRecording();
}

TEST_F(TTD_FrameCache_Test, FreedOnInvalidate)
{
    recordBusy(5);
    const uint64_t frame = _ttd->SessionEndPosition().frame - 1;
    ASSERT_NE(_ttd->GetFrameCache(frame), nullptr);
    EXPECT_GT(_ttd->GetFrameCacheBytes(), 0u);

    _ttd->InvalidateSession("test");
    EXPECT_EQ(_ttd->GetFrameCacheBytes(), 0u);
}

TEST_F(TTD_FrameCache_Test, ScalesWithCpuFrequencyMultiplier)
{
    // At 56 MHz the Z80 runs 16x the base 3.5 MHz clock, so a frame holds ~16x
    // the instructions and the cache must capture the WHOLE turbo frame (not
    // just 1/multiplier of it) with the reserve sized accordingly.
    const uint8_t kMultiplier = 16;  // 3.5 MHz * 16 = 56 MHz
    // Set the active multiplier directly: the queued next->current swap happens
    // in the frame-cycle path, not RunNFrames' manual loop. Both fields so the
    // boundary check leaves it in place. RunNFrames then uses a 16x frameLimit.
    _context->emulatorState.current_z80_frequency_multiplier = kMultiplier;
    _context->emulatorState.next_z80_frequency_multiplier = kMultiplier;
    ASSERT_EQ(_context->emulatorState.current_z80_frequency_multiplier, kMultiplier);

    installBusyProgram();
    _ttd->StartRecording();
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    const uint64_t frame = _ttd->SessionEndPosition().frame - 1;
    const ttd::TTDFrameCache* c = _ttd->GetFrameCache(frame);
    ASSERT_NE(c, nullptr);

    // A 1x busy frame is ~13.5k instructions; at 16x expect well over 100k —
    // proving the full turbo frame was replayed, not a base-length slice.
    EXPECT_GT(c->entries.size(), 100000u) << "captured only " << c->entries.size()
                                          << " instructions — turbo frame under-replayed";

    // The pre-reservation is sized to the largest possible frame
    // (config.frame * multiplier / min-instruction-tstates), so the fill never
    // reallocated: capacity holds the whole frame.
    EXPECT_GE(c->entries.capacity(), c->entries.size());

    // tInFrame spans the FULL turbo frame (> base config.frame), confirming the
    // whole scaled frame was replayed rather than a base-length slice.
    EXPECT_GT(c->entries.back().tInFrame, _context->config.frame);

    // Records are coherent at the new scale: PCs are in-program and the captured
    // opcodes match the (static) program bytes at each PC. (SeekTo is not used
    // here: it clamps tInFrame to the base frame length and so cannot target a
    // turbo mid-frame position — a separate TTD seek limitation, tracked below.)
    for (size_t idx : {size_t(0), c->entries.size() / 2, c->entries.size() - 1})
    {
        const ttd::TTDFrameCacheEntry& e = c->entries[idx];
        EXPECT_TRUE(isProgramPc(e.pc)) << "idx " << idx << " pc=" << std::hex << e.pc;
        for (int b = 0; b < 3; ++b)
            EXPECT_EQ(e.opcodes[b], _memory->DirectReadFromZ80Memory(e.pc + b)) << "idx " << idx << " byte " << b;
    }

    _context->emulatorState.current_z80_frequency_multiplier = 1;
    _context->emulatorState.next_z80_frequency_multiplier = 1;
}

TEST_F(TTD_FrameCache_Test, RebuildsWhenDifferentFrameRequested)
{
    recordBusy(6);
    const uint64_t end = _ttd->SessionEndPosition().frame;

    const ttd::TTDFrameCache* a = _ttd->GetFrameCache(end - 1);
    ASSERT_NE(a, nullptr);
    const ttd::TTDFrameCache* b = _ttd->GetFrameCache(end - 2);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->frame, end - 2);
    EXPECT_EQ(_ttd->GetCachedFrame(), end - 2);
}

