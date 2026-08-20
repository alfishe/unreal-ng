/// @file ttd_full_restore_test.cpp
/// @brief Does a checkpoint hold everything needed to rebuild the machine?
///
/// The existing suites cover the pieces: TTDChipsetStateTest round-trips the
/// struct, TTD_Format_V2_Test round-trips the file, TTD_Divergence_Corpus_Test
/// replays real snapshots. What none of them asked is whether a checkpoint is
/// SELF-CONTAINED - whether restoring it rebuilds state the guest wrote before
/// the session started, or before that particular checkpoint.
///
/// Two shipped defects motivated this file, both of which passed every existing
/// test:
///
///   1. Only a session's FIRST checkpoint captured RAM in full. Later key
///      frames re-interned dirty pages only and inherited refs for the rest, so
///      any timeline that lost its anchor (truncate + resume, a re-anchored
///      session) restored correct registers into empty memory.
///
///   2. The SNA loader painted the snapshot's border on screen but left pFE and
///      border_attr at their reset value, so every checkpoint of a black-
///      bordered snapshot recorded white.

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/ttd_dirty_tracker.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/video/screen.h"

namespace
{

constexpr size_t kPageSize = 0x4000;  // 16 KB

/// Fill a RAM page with a recognisable pattern. Deliberately not a constant
/// byte: a page of one repeated value would survive a "restored as zeros" bug
/// on any page whose marker happened to be zero.
void FillRamPage(Memory* memory, uint16_t page, uint8_t seed)
{
    uint8_t* p = memory->RAMPageAddress(page);
    ASSERT_NE(p, nullptr);
    for (size_t i = 0; i < kPageSize; ++i)
        p[i] = static_cast<uint8_t>(seed + (i * 7) + (i >> 8));
}

bool RamPageMatches(Memory* memory, uint16_t page, uint8_t seed)
{
    const uint8_t* p = memory->RAMPageAddress(page);
    if (p == nullptr)
        return false;
    for (size_t i = 0; i < kPageSize; ++i)
    {
        if (p[i] != static_cast<uint8_t>(seed + (i * 7) + (i >> 8)))
            return false;
    }
    return true;
}

class TTD_FullRestore_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;
    FeatureManager* _fm = nullptr;

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

        // Only the time-travel feature: enabling it must bring the master
        // debugmode flag, Memory's feature cache and the instrumented memory
        // interface with it.
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
            _emulator = nullptr;
        }
    }

    /// Advance one frame the way the run loop would: the key-frame decision is
    /// made against the emulator's frame counter, not the checkpoint index, so a
    /// test that only calls OnFrameBoundary() never leaves frame 0.
    void AdvanceFrame()
    {
        ++_context->emulatorState.frame_counter;
        _ttd->OnFrameBoundary();
    }

    /// Wipe a RAM page directly, bypassing the write hook, so the dirty bitmap
    /// does NOT learn about it. Models the state a restore has to overwrite.
    void WipeRamPageSilently(uint16_t page)
    {
        uint8_t* p = _memory->RAMPageAddress(page);
        ASSERT_NE(p, nullptr);
        std::fill(p, p + kPageSize, uint8_t{0});
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Memory completeness
// ---------------------------------------------------------------------------

/// Content written BEFORE recording started must be in the first checkpoint.
/// The dirty bitmap only knows about writes made after the hook went live, so a
/// capture driven by it alone would miss everything the guest had already
/// loaded - which is most of a real session's memory.
TEST_F(TTD_FullRestore_Test, FirstCheckpoint_CapturesMemoryWrittenBeforeRecording)
{
    FillRamPage(_memory, 1, 0x11);
    FillRamPage(_memory, 3, 0x33);

    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();
    ASSERT_GE(_ttd->GetCheckpointCount(), 1u);

    WipeRamPageSilently(1);
    WipeRamPageSilently(3);
    ASSERT_FALSE(RamPageMatches(_memory, 1, 0x11));

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(0));

    EXPECT_TRUE(RamPageMatches(_memory, 1, 0x11))
        << "page 1 was written before recording began and did not come back";
    EXPECT_TRUE(RamPageMatches(_memory, 3, 0x33))
        << "page 3 was written before recording began and did not come back";
}

/// A key frame must stand on its own. Before the fix it re-interned only the
/// pages dirtied since the previous checkpoint and inherited refs for the rest,
/// so a key frame was not a snapshot at all - it just looked like one.
TEST_F(TTD_FullRestore_Test, KeyFrame_IsSelfContained)
{
    FillRamPage(_memory, 2, 0x22);   // written before recording
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();

    // Advance until the engine emits its next key frame (one every
    // kKeyFrameInterval frames). Only page 4 changes on the way, so page 2 is
    // clean at that key frame - a dirty-driven capture would not carry it.
    size_t keyFrameIdx = 0;
    for (uint32_t i = 0; i < ttd::TimeTravelManager::kKeyFrameInterval + 2; ++i)
    {
        FillRamPage(_memory, 4, static_cast<uint8_t>(0x40 + (i & 0x0F)));
        _memory->GetTTDDirtyTracker()->MarkDirty(4);
        AdvanceFrame();

        const size_t last = _ttd->GetCheckpointCount() - 1;
        const ttd::TTDCheckpoint* c = _ttd->GetCheckpoint(last);
        if (last > 0 && c != nullptr && c->frameKind == ttd::TTDFrameKind::KeyFrame)
        {
            keyFrameIdx = last;
            break;
        }
    }
    ASSERT_GT(keyFrameIdx, 0u) << "engine produced no key frame after the anchor";
    const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(keyFrameIdx);
    ASSERT_NE(cp, nullptr);
    ASSERT_EQ(cp->frameKind, ttd::TTDFrameKind::KeyFrame);

    WipeRamPageSilently(2);
    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(keyFrameIdx));

    EXPECT_TRUE(RamPageMatches(_memory, 2, 0x22))
        << "a key frame did not carry a page that was clean since the anchor - "
        << "it is a delta wearing a key frame's label";
}

/// All-zero pages must stay cheap. The completeness fix would be a poor trade
/// if it interned 32 pages of nothing on every key frame.
TEST_F(TTD_FullRestore_Test, KeyFrame_EmptyPagesDoNotInflateTheStore)
{
    FillRamPage(_memory, 1, 0x55);
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();

    const size_t slotsAfterBaseline = _ttd->GetPageStore().GetUsedSlots();

    for (uint32_t i = 0; i < ttd::TimeTravelManager::kKeyFrameInterval + 2; ++i)
        AdvanceFrame();

    const size_t slotsAfterKeyFrame = _ttd->GetPageStore().GetUsedSlots();

    // The property under test is that identical content dedups by hash, so a
    // key frame costs a fraction of a full RAM image rather than a copy of it.
    // A full image is model_ram_pages * 4 slots; anything approaching that means
    // sharing broke. The bound is deliberately loose - the machine ticks between
    // captures and a few pages legitimately differ - but far below a full image.
    const size_t fullImageSlots = static_cast<size_t>(_ttd->GetModelRamPages()) * 4u;
    const size_t growth = slotsAfterKeyFrame - slotsAfterBaseline;
    EXPECT_LT(growth, fullImageSlots / 2u)
        << "key frame added " << growth << " slots of a " << fullImageSlots
        << "-slot image - unchanged pages were interned instead of shared";
}

// ---------------------------------------------------------------------------
// Chipset completeness
// ---------------------------------------------------------------------------

/// Border colour must survive capture and restore. The regression this guards
/// came from outside TTD: a loader that set the picture without the latch.
TEST_F(TTD_FullRestore_Test, Border_SurvivesCaptureAndRestore)
{
    // Black border, as a snapshot with a dark border would leave it.
    _context->emulatorState.pFE = 0x00;
    _context->emulatorState.border_attr = 0x00;

    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();

    const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(0);
    ASSERT_NE(cp, nullptr);
    EXPECT_EQ(cp->chipset.border_attr & 0b0000'0111, 0u)
        << "checkpoint recorded a border the machine did not have";
    EXPECT_EQ(cp->chipset.pFE & 0b0000'0111, 0u);

    // Live state moves to white, then a restore must bring black back.
    _context->emulatorState.pFE = 0x07;
    _context->emulatorState.border_attr = 0x07;

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(0));

    EXPECT_EQ(_context->emulatorState.pFE & 0b0000'0111, 0u) << "border latch not restored";
    if (_context->pScreen)
    {
        EXPECT_EQ(_context->pScreen->GetBorderColor() & 0b0000'0111, 0u)
            << "screen kept the pre-seek border colour";
    }
}

/// Banking selects which RAM the CPU and the renderer see; restoring registers
/// into the wrong bank layout produces a machine that looks right and runs
/// wrong.
TEST_F(TTD_FullRestore_Test, Banking_SurvivesCaptureAndRestore)
{
    _context->emulatorState.p7FFD = 0x10;  // 48K ROM paged in, screen bank 5

    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();

    _context->emulatorState.p7FFD = 0x08;  // shadow screen, different ROM

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(0));
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x10) << "p7FFD not restored";
}

/// CPU state is the part everything else hangs off; a partial restore here is
/// the difference between resuming and crashing into ROM.
TEST_F(TTD_FullRestore_Test, CpuState_SurvivesCaptureAndRestore)
{
    Z80* z80 = _context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr);

    z80->pc = 0x9520;
    z80->sp = 0xFEE5;
    z80->bc = 0x1234;
    z80->de = 0x5678;
    z80->hl = 0x9ABC;
    z80->im = 2;
    z80->iff1 = 0;

    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();

    z80->pc = 0x0038;
    z80->sp = 0x0000;
    z80->bc = 0;
    z80->de = 0;
    z80->hl = 0;
    z80->im = 0;
    z80->iff1 = 1;

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(0));

    EXPECT_EQ(z80->pc, 0x9520);
    EXPECT_EQ(z80->sp, 0xFEE5);
    EXPECT_EQ(z80->bc, 0x1234);
    EXPECT_EQ(z80->de, 0x5678);
    EXPECT_EQ(z80->hl, 0x9ABC);
    EXPECT_EQ(z80->im, 2);
    EXPECT_EQ(z80->iff1, 0) << "interrupt enable flag not restored - a demo "
                               "running with DI would take an interrupt it never had";
}

// ---------------------------------------------------------------------------
// Through a file
// ---------------------------------------------------------------------------

/// The same completeness has to survive a trip through .ttd. Serializing a
/// session and loading it back must restore memory the guest wrote before
/// recording started - that is the whole point of handing a file to someone.
TEST_F(TTD_FullRestore_Test, SerializedSession_RestoresMemoryAfterReload)
{
    FillRamPage(_memory, 1, 0x77);
    FillRamPage(_memory, 6, 0x66);

    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();
    _ttd->StopRecording();

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;

    _ttd->InvalidateSession("test reload");
    ASSERT_EQ(_ttd->GetCheckpointCount(), 0u);

    WipeRamPageSilently(1);
    WipeRamPageSilently(6);

    std::istringstream in(out.str(), std::ios::binary);
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;
    ASSERT_GE(_ttd->GetCheckpointCount(), 1u);

    ASSERT_TRUE(_ttd->RestoreCheckpointForTesting(0));

    EXPECT_TRUE(RamPageMatches(_memory, 1, 0x77)) << "page 1 lost across the file";
    EXPECT_TRUE(RamPageMatches(_memory, 6, 0x66)) << "page 6 lost across the file";
}

/// A session only restores into the machine model it came from: a checkpoint is
/// raw RAM pages plus a chipset snapshot, so loading a Pentagon recording into a
/// 48K instance would be silently wrong rather than obviously broken.
TEST_F(TTD_FullRestore_Test, LoadRefusesForeignModel)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _ttd->OnFrameBoundary();
    _ttd->StopRecording();

    std::ostringstream out(std::ios::binary);
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;

    // Pretend this instance is a different machine.
    const MEM_MODEL original = _context->config.mem_model;
    _context->config.mem_model =
        (original == MM_SPECTRUM48) ? MM_PENTAGON : MM_SPECTRUM48;

    std::istringstream in(out.str(), std::ios::binary);
    EXPECT_FALSE(_ttd->DeserializeSession(in, err))
        << "a session from another model was accepted";
    EXPECT_NE(err.find("model"), std::string::npos)
        << "refusal did not explain the model mismatch: " << err;

    _context->config.mem_model = original;
}
