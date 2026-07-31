/// @file ttd_subsystem_restore_test.cpp
/// @brief Integration tests for TTD seek/replay correctness across the four
///        subsystems that historically cause restore bugs:
///          1. Memory banking (p7FFD RAM page switches)
///          2. Screen bank switches (p7FFD bit 3: normal <-> shadow)
///          3. Tape playback position
///          4. FDC (WD1793) register + FDD state
///
/// The existing unit-level serializer tests (ttd_tape_serializer_test.cpp,
/// ttd_wd1793_serializer_test.cpp, etc.) verify each device's
/// TTDSaveState/TTDLoadState round-trip in isolation. The existing restore
/// test (ttd_restore_test.cpp) covers paging. This file fills the gap by
/// driving each subsystem through its public API, capturing checkpoints via
/// OnFrameBoundary, mutating state, then SeekTo-ing back and asserting
/// byte-identical restoration -- the exact path a user-initiated seek takes.
///
/// Why this matters: serializer correctness is necessary but not sufficient.
/// RestoreCheckpoint orchestrates CPU + chipset + paging + RAM + peripherals
/// + screen renderer. Any bug in the orchestration (wrong call order,
/// missing resync, missed peripheral) produces subtle state divergence that
/// the per-device unit tests cannot catch.
///
/// All tests use the standard Pentagon 128K model unless noted -- it has
/// banking, screen shadow bank, tape, and BDI/FDC.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/machine_state_hash.h"
#include "debugger/ttd/ttd_dirty_tracker.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/tape/tape.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

// ===========================================================================
// Fixture
// ===========================================================================

class TTD_Subsystem_Restore_Test : public ::testing::Test
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
        ASSERT_NE(_emulator, nullptr);
        ASSERT_TRUE(_emulator->Init()) << "Failed to initialize emulator";

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);
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

    /// Enable debugmode + timetravel features so capture/restore hooks fire.
    void EnableTTD()
    {
        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
    }

    /// Drive the emulator forward by N frames, capturing one checkpoint per
    /// frame boundary. Wraps Emulator::RunNFrames so test bodies stay terse.
    void RunFrames(unsigned n)
    {
        _emulator->RunNFrames(n, /*skipBreakpoints=*/true);
    }

    /// Write a recognizable byte pattern into a RAM page so we can detect
    /// whether restore put the right content back. Marks the page dirty so
    /// OnFrameBoundary Interns it into the page store.
    void ScribbleRamPage(uint16_t page, uint8_t marker)
    {
        uint8_t* base = _memory->RAMPageAddress(page);
        ASSERT_NE(base, nullptr);
        for (uint32_t i = 0; i < 0x4000; i += 0x100)
            base[i] = static_cast<uint8_t>(marker + (i >> 8));

        ttd::TTDDirtyTracker* tracker = _memory->GetTTDDirtyTracker();
        ASSERT_NE(tracker, nullptr);
        tracker->MarkDirty(page);
    }
};

// ===========================================================================
// 1. MEMORY PAGE SWITCHES (p7FFD RAM banking)
//
// Pentagon 128 has 8 RAM pages (0..7). p7FFD bits 0-2 select which page is
// mapped at CPU address 0xC000-0xFFFF (bank 3). Restoring a checkpoint must
// restore both the port latch AND the rebuilt bank mapping, otherwise a
// post-seek memory write at 0xC000 lands in the wrong physical page.
//
// This test exercises MULTIPLE page switches across frames, then seeks back
// to verify each switchpoint is correctly restorable.
// ===========================================================================

TEST_F(TTD_Subsystem_Restore_Test, MemoryBanking_SeekRestoresAllPageSwitches)
{
    EnableTTD();

    ASSERT_TRUE(_ttd->StartRecording());

    // Pentagon 128 has 8 RAM pages -- sanity (checked after StartRecording
    // because _modelRamPages is populated there).
    ASSERT_GE(_ttd->GetModelRamPages(), 8u);

    EmulatorState& st = _context->emulatorState;
    PortDecoder* pd = _context->pPortDecoder;
    ASSERT_NE(pd, nullptr);

    const uint8_t initialP7FFD = st.p7FFD;
    const uint8_t initialBank3 = initialP7FFD & 0b0000'0111;

    // We'll switch through pages 0, 1, 2, 3 (skipping the initial one) and
    // mark each with a unique byte pattern so we can verify which page is
    // mapped after each seek.
    struct Frame
    {
        uint8_t p7ffd;       // p7FFD value after switch
        uint8_t bank3;       // resulting bank-3 page index
        uint8_t scribbleMarker;
        uint64_t frame;      // emulatorState.frame_counter at capture
    };
    std::vector<Frame> frames;

    // Capture baseline at initial paging.
    ScribbleRamPage(initialBank3, 0x10);
    _ttd->OnFrameBoundary();
    frames.push_back({initialP7FFD, initialBank3, 0x10, st.frame_counter});

    // Now switch through three more pages, scribbling + capturing each.
    // NOTE: OnFrameBoundary() does NOT advance frame_counter -- only
    // MainLoop::OnFrameEnd (reached via RunNFrames) does. To give each
    // capture a unique timeline position, we bump frame_counter manually
    // (same pattern used by ttd_manager_test.cpp lines 326/502/548).
    uint8_t nextBank = initialBank3;
    for (uint8_t i = 0; i < 3; ++i)
    {
        nextBank = static_cast<uint8_t>((nextBank + 1) & 0b0000'0111);
        // Avoid the special screen pages (5 normal, 7 shadow) to keep the
        // test focused on banking alone.
        if (nextBank == 5 || nextBank == 7)
            nextBank = static_cast<uint8_t>((nextBank + 1) & 0b0000'0111);

        const uint8_t newP7FFD = (initialP7FFD & 0b1111'1000) | nextBank;
        const uint8_t marker   = static_cast<uint8_t>(0x20 + i * 0x10);

        pd->DecodePortOut(0x7FFD, newP7FFD, 0x0000);
        ScribbleRamPage(nextBank, marker);
        st.frame_counter++;                 // advance to a new timeline slot
        _ttd->OnFrameBoundary();

        frames.push_back({newP7FFD, nextBank, marker, st.frame_counter});
        EXPECT_EQ(st.p7FFD, newP7FFD) << "switch #" << i << " didn't update latch";
    }

    // Stop recording so SeekTo is allowed.
    _ttd->StopRecording();
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Idle);

    // For each captured frame, seek back and verify:
    //   (a) p7FFD latch matches
    //   (b) bank 3 maps to the page we scribbled
    //   (c) the scribbled bytes are present at that page
    for (size_t i = 0; i < frames.size(); ++i)
    {
        SCOPED_TRACE("captured frame index " + std::to_string(i)
                     + " (frame_counter=" + std::to_string(frames[i].frame)
                     + ", bank3=" + std::to_string(frames[i].bank3) + ")");

        const uint64_t targetFrame = frames[i].frame;
        ASSERT_TRUE(_ttd->SeekTo({targetFrame, 0}))
            << "SeekTo failed for frame " << targetFrame;

        EXPECT_EQ(st.p7FFD, frames[i].p7ffd)
            << "p7FFD latch not restored";

        // Verify the bank-3 page contains the scribbled marker.
        uint8_t* page = _memory->RAMPageAddress(frames[i].bank3);
        ASSERT_NE(page, nullptr);
        EXPECT_EQ(page[0], static_cast<uint8_t>(frames[i].scribbleMarker))
            << "bank-3 page content not restored -- paging rebuild missed";
        EXPECT_EQ(page[0x100], static_cast<uint8_t>(frames[i].scribbleMarker + 1))
            << "bank-3 page content partially restored";
    }
}

// ===========================================================================
// 2. SCREEN BANK SWITCH (p7FFD bit 3: normal <-> shadow)
//
// Same port (p7FFD) bit 3 also flips the active screen between bank 5
// (SCREEN_NORMAL) and bank 7 (SCREEN_SHADOW). The renderer caches
// `_activeScreenMemoryOffset` derived from this bit, and RestoreCheckpoint
// must resync it via SetActiveScreen.
//
// This test verifies that after seeking to frames captured with different
// screen banks active, the p7FFD latch, the renderer's _activeScreen enum,
// and the underlying RAM bank content are all correctly restored. The
// actual framebuffer pixel output is verified end-to-end by the divergence
// corpus test (ttd_divergence_corpus_test.cpp) using real demo data.
// ===========================================================================

TEST_F(TTD_Subsystem_Restore_Test, ScreenBankSwitch_SeekRestoresActiveBank)
{
    EnableTTD();

    // Run a few frames first to initialize the video subsystem.
    RunFrames(2);

    ASSERT_TRUE(_ttd->StartRecording());

    Screen* screen = _context->pScreen;
    ASSERT_NE(screen, nullptr);
    PortDecoder* pd = _context->pPortDecoder;
    ASSERT_NE(pd, nullptr);
    EmulatorState& st = _context->emulatorState;

    // Put distinct sentinel bytes at the start of banks 5 and 7 so we can
    // verify the correct bank content is present after each seek.
    uint8_t* bank5 = _memory->RAMPageAddress(5);
    uint8_t* bank7 = _memory->RAMPageAddress(7);
    ASSERT_NE(bank5, nullptr);
    ASSERT_NE(bank7, nullptr);
    bank5[0] = 0xA5;
    bank7[0] = 0x5A;

    ttd::TTDDirtyTracker* tracker = _memory->GetTTDDirtyTracker();
    ASSERT_NE(tracker, nullptr);
    tracker->MarkDirty(5);
    tracker->MarkDirty(7);

    // Capture at NORMAL screen (bit 3 = 0).
    const uint8_t p7FFD_normal = st.p7FFD & 0b1111'0111;
    pd->DecodePortOut(0x7FFD, p7FFD_normal, 0x0000);
    EXPECT_EQ(st.p7FFD & 0b0000'1000, 0u);
    _ttd->OnFrameBoundary();
    const uint64_t frameAtNormal = st.frame_counter;

    // Switch to SHADOW screen (bit 3 = 1) and capture at a distinct frame.
    const uint8_t p7FFD_shadow = (st.p7FFD & 0b1111'0111) | 0b0000'1000;
    pd->DecodePortOut(0x7FFD, p7FFD_shadow, 0x0000);
    EXPECT_EQ(st.p7FFD & 0b0000'1000, 0b0000'1000);
    st.frame_counter++;
    _ttd->OnFrameBoundary();
    const uint64_t frameAtShadow = st.frame_counter;

    _ttd->StopRecording();

    // Seek to NORMAL: p7FFD bit 3 must be clear, renderer must report
    // SCREEN_NORMAL, and bank 5 must contain the sentinel byte.
    ASSERT_TRUE(_ttd->SeekTo({frameAtNormal, 0}));
    EXPECT_EQ(st.p7FFD & 0b0000'1000, 0u)
        << "NORMAL frame: p7FFD bit 3 not restored";
    EXPECT_EQ(screen->GetActiveScreen(), SCREEN_NORMAL)
        << "NORMAL frame: _activeScreen not SCREEN_NORMAL";

    // Re-fetch bank pointers after restore (the memory subsystem may have
    // swapped page buffers during restore).
    bank5 = _memory->RAMPageAddress(5);
    bank7 = _memory->RAMPageAddress(7);
    ASSERT_NE(bank5, nullptr);
    ASSERT_NE(bank7, nullptr);
    EXPECT_EQ(bank5[0], 0xA5) << "NORMAL frame: bank 5 sentinel not restored";
    EXPECT_EQ(bank7[0], 0x5A) << "NORMAL frame: bank 7 sentinel not restored";

    // Seek to SHADOW: p7FFD bit 3 must be set, renderer must report
    // SCREEN_SHADOW, and both bank sentinels must survive.
    ASSERT_TRUE(_ttd->SeekTo({frameAtShadow, 0}));
    EXPECT_EQ(st.p7FFD & 0b0000'1000, 0b0000'1000)
        << "SHADOW frame: p7FFD bit 3 not restored";
    EXPECT_EQ(screen->GetActiveScreen(), SCREEN_SHADOW)
        << "SHADOW frame: _activeScreen not SCREEN_SHADOW";

    bank5 = _memory->RAMPageAddress(5);
    bank7 = _memory->RAMPageAddress(7);
    ASSERT_NE(bank5, nullptr);
    ASSERT_NE(bank7, nullptr);
    EXPECT_EQ(bank5[0], 0xA5) << "SHADOW frame: bank 5 sentinel not restored";
    EXPECT_EQ(bank7[0], 0x5A) << "SHADOW frame: bank 7 sentinel not restored";

    // Round-trip back to NORMAL to verify determinism.
    ASSERT_TRUE(_ttd->SeekTo({frameAtNormal, 0}));
    EXPECT_EQ(st.p7FFD & 0b0000'1000, 0u);
    EXPECT_EQ(screen->GetActiveScreen(), SCREEN_NORMAL);
    bank5 = _memory->RAMPageAddress(5);
    EXPECT_EQ(bank5[0], 0xA5)
        << "NORMAL (round-trip): bank 5 sentinel not deterministic";
}

// ===========================================================================
// 3. TAPE PLAYBACK POSITION
//
// The tape subsystem serializes playback position (block index, pulse index,
// clock count) per parent TDD section 4 row 3. Content (_tapeBlocks) is
// invariant within a session -- only the cursor moves. We verify that after
// seeking, the cursor is restored to exactly where it was at the captured
// frame.
//
// Note: LoadTape() invalidates the session by design (TDD section 4.2 +
// section 5 row 3). So we must load the tape BEFORE StartRecording, then set
// the cursor to known positions during recording so checkpoints capture the
// advancing position.
//
// We use TapeCUT to set the cursor fields directly rather than relying on
// the ROM LOAD routine to advance playback. This keeps the test deterministic
// -- the ROM routine's cursor advancement depends on CPU timing which would
// make the test hostage to Z80 instruction-count drift.
// ===========================================================================

TEST_F(TTD_Subsystem_Restore_Test, TapePosition_SeekRestoresPlaybackCursor)
{
    EnableTTD();

    Tape* tape = _context->pTape;
    ASSERT_NE(tape, nullptr);

    // Load a tape file before recording. Use a small tap so we have at least
    // one block in _tapeBlocks for the cursor to point into.
    const std::string tapePath = "data/testtapes/hardware test 2005-01-16.tap";
    if (!_emulator->LoadTape(tapePath))
    {
        GTEST_SKIP() << "Tape fixture not found at " << tapePath
                     << " -- skipping tape-position restore test";
    }

    // Now start recording. LoadTape already invalidated the (empty) session,
    // but StartRecording creates a fresh baseline.
    ASSERT_TRUE(_ttd->StartRecording());

    // Use the CUT wrapper to set cursor fields directly. This avoids
    // depending on the ROM LOAD routine to advance playback (which would
    // make the test hostage to Z80 instruction-count timing).
    TapeCUT* tapeCUT = reinterpret_cast<TapeCUT*>(tape);

    // Set the cursor to known non-default values that are easy to spot if
    // restore fails. The values are chosen so they don't collide with
    // default-zero state (the "did nothing get stored?" sentinel).
    tapeCUT->_tapeStarted = true;
    tapeCUT->_tapePosition = 12345;
    tapeCUT->_currentTapeBlockIndex = 1;
    tapeCUT->_currentPulseIdxInBlock = 678;
    tapeCUT->_currentOffsetWithinPulse = 2;
    tapeCUT->_currentClockCount = 99998;

    // Capture checkpoint with this cursor state.
    EmulatorState& st = _context->emulatorState;
    st.frame_counter++;                 // advance to a new timeline slot
    _ttd->OnFrameBoundary();
    const uint64_t capturedFrame = st.frame_counter;

    // Snapshot the expected blob via the public serializer (same path TTD
    // uses internally during capture).
    std::vector<uint8_t> expected(tape->TTDStateSize());
    tape->TTDSaveState(expected.data());
    ASSERT_FALSE(expected.empty()) << "tape state blob is empty -- serializer broken";

    // Mutate the cursor away from captured values. These are all different
    // from the values above, so a missed restore produces an obviously
    // wrong readback.
    tapeCUT->_tapeStarted = false;
    tapeCUT->_tapePosition = 0;
    tapeCUT->_currentTapeBlockIndex = 0;
    tapeCUT->_currentPulseIdxInBlock = 0;
    tapeCUT->_currentOffsetWithinPulse = 0;
    tapeCUT->_currentClockCount = 0;

    // Verify the mutation actually took (sanity).
    EXPECT_FALSE(tapeCUT->_tapeStarted);
    EXPECT_EQ(tapeCUT->_currentClockCount, 0u);

    _ttd->StopRecording();

    // Seek back. RestoreCheckpoint dispatches TTDLoadState on every
    // peripheral, including the tape. The cursor must come back.
    ASSERT_TRUE(_ttd->SeekTo({capturedFrame, 0}))
        << "SeekTo to captured frame failed";

    // Byte-for-byte comparison: the post-restore serialized blob must
    // match the captured one. This catches any field that the individual
    // field checks below might miss.
    std::vector<uint8_t> afterRestore(tape->TTDStateSize());
    tape->TTDSaveState(afterRestore.data());
    EXPECT_EQ(afterRestore, expected)
        << "Tape state blob differs after seek -- some cursor field not "
        << "captured/restored";

    // Individual field verification (catches the bug at a more granular
    // level than the blob comparison, and produces clearer failure messages).
    EXPECT_TRUE(tapeCUT->_tapeStarted)
        << "_tapeStarted not restored";
    EXPECT_EQ(tapeCUT->_tapePosition, 12345u)
        << "_tapePosition not restored";
    EXPECT_EQ(tapeCUT->_currentTapeBlockIndex, 1u)
        << "_currentTapeBlockIndex not restored";
    EXPECT_EQ(tapeCUT->_currentPulseIdxInBlock, 678u)
        << "_currentPulseIdxInBlock not restored";
    EXPECT_EQ(tapeCUT->_currentOffsetWithinPulse, 2u)
        << "_currentOffsetWithinPulse not restored";
    EXPECT_EQ(tapeCUT->_currentClockCount, 99998u)
        << "_currentClockCount not restored";

    tape->stopTape();
}

// ===========================================================================
// 4. FDC (WD1793) REGISTER STATE
//
// The WD1793 + 4 FDDs serialize 251 bytes of state (143 controller + 4x27
// per-drive). Per parent TDD section 4 row 4, all FDC internal state (state
// machine phase, track/sector regs, DRQ/INTRQ timers) must be fully
// serialized.
//
// This test mutates the FDC registers via the CUT (Code Under Test) wrapper
// -- same pattern as ttd_wd1793_serializer_test.cpp -- then verifies the
// registers are byte-identical after a seek round-trip.
// ===========================================================================

TEST_F(TTD_Subsystem_Restore_Test, FDCRegisters_SeekRestoresAllRegisters)
{
    EnableTTD();

    WD1793* fdc = _context->pBetaDisk;
    if (fdc == nullptr)
    {
        GTEST_SKIP() << "Model has no Beta Disk controller -- "
                     << "FDC register restore test requires a BDI-equipped model";
    }

    // Load a disk image before recording so the FDDs have media and the
    // FDC's _selectedDrive pointer has something valid to point at.
    const std::string diskPath = "testdata/loaders/trd/zx-format8.trd";
    if (!_emulator->LoadDisk(diskPath))
    {
        GTEST_SKIP() << "Disk fixture not found at " << diskPath
                     << " -- skipping FDC register restore test";
    }

    ASSERT_TRUE(_ttd->StartRecording());

    // Mutate FDC registers via the CUT wrapper. The test binary is compiled
    // with _CODE_UNDER_TEST so WD1793CUT exposes protected register fields.
    WD1793CUT* fdcCUT = reinterpret_cast<WD1793CUT*>(fdc);

    // Pick register values that are unlikely to occur by accident, so a
    // missed restore produces an obviously-wrong readback.
    fdcCUT->_trackRegister  = 0x12;
    fdcCUT->_sectorRegister = 0x07;
    fdcCUT->_dataRegister   = 0xAB;
    fdcCUT->_drive          = 1;       // drive B
    fdcCUT->_sideUp         = true;

    // Mutate FDD tracks on drives A and B so the per-drive state is non-default.
    ASSERT_NE(_context->coreState.diskDrives[0], nullptr);
    ASSERT_NE(_context->coreState.diskDrives[1], nullptr);
    _context->coreState.diskDrives[0]->setTrack(3);
    _context->coreState.diskDrives[1]->setTrack(42);

    // Capture checkpoint with mutated FDC state.
    EmulatorState& st = _context->emulatorState;
    st.frame_counter++;                 // advance to a new timeline slot
    _ttd->OnFrameBoundary();
    const uint64_t capturedFrame = st.frame_counter;

    // Snapshot the expected blob via the public serializer.
    std::vector<uint8_t> expected(fdc->TTDStateSize());
    fdc->TTDSaveState(expected.data());
    ASSERT_FALSE(expected.empty());

    // Mutate registers away from captured values.
    fdcCUT->_trackRegister  = 0x00;
    fdcCUT->_sectorRegister = 0x00;
    fdcCUT->_dataRegister   = 0x00;
    fdcCUT->_drive          = 0;
    fdcCUT->_sideUp         = false;
    _context->coreState.diskDrives[0]->setTrack(0);
    _context->coreState.diskDrives[1]->setTrack(0);

    // Verify the mutation actually took (sanity).
    EXPECT_EQ(fdcCUT->_trackRegister, 0x00);

    _ttd->StopRecording();

    // Seek back. RestoreCheckpoint dispatches TTDLoadState on every
    // peripheral, including the FDC. The registers must come back.
    ASSERT_TRUE(_ttd->SeekTo({capturedFrame, 0}))
        << "SeekTo failed for frame " << capturedFrame;

    // Read back individual registers via public getters.
    EXPECT_EQ(fdc->getTrackRegister(),  0x12)
        << "WD1793 track register not restored";
    EXPECT_EQ(fdc->getSectorRegister(), 0x07)
        << "WD1793 sector register not restored";
    EXPECT_EQ(fdc->getDataRegister(),   0xAB)
        << "WD1793 data register not restored";

    // _selectedDrive is re-derived from the restored _drive index.
    FDD* selectedDrive = fdc->getDrive();
    ASSERT_NE(selectedDrive, nullptr);
    EXPECT_EQ(selectedDrive, _context->coreState.diskDrives[1])
        << "WD1793 _selectedDrive not re-derived from restored _drive=1";

    // FDD track registers must also be restored (they're in the per-FDD
    // serializer portion of the blob).
    EXPECT_EQ(_context->coreState.diskDrives[0]->getTrack(), 3)
        << "FDD A track not restored";
    EXPECT_EQ(_context->coreState.diskDrives[1]->getTrack(), 42)
        << "FDD B track not restored";

    // Byte-for-byte comparison: the post-restore serialized blob must
    // match the captured one. This catches any field that the individual
    // getter checks above missed.
    std::vector<uint8_t> afterRestore(fdc->TTDStateSize());
    fdc->TTDSaveState(afterRestore.data());
    EXPECT_EQ(afterRestore, expected)
        << "FDC state blob differs after seek -- some field not captured/restored";
}
