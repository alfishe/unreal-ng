/// @file ttdinputjournal_test.cpp
/// @brief Phase 2 Item 3 — Input journal tests.
///
/// Per parent TDD §5 row #1 and §5.1. Coverage areas:
///   - TTDInputJournal data structure (Record / Size / Events / Clear /
///     DropAfter / PeekNextEventTimeOnOrAfter)
///   - Capture integration (DebugKeyboardManager::PressKey/ReleaseKey →
///     TimeTravelManager::RecordInputEvent → journal)
///   - Capture suppression rules (no capture outside Recording, no
///     capture during replay)
///   - Inject path (InjectDueEvents / InjectDueInputEvents)
///   - Lifecycle integration (StartRecording clears journal, StopRecording
///     preserves it, InvalidateSession clears it)
///   - General Sound host stimuli from automation (Apply per kind, the live
///     path onto the machine's thread, journaled replay)

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

// ===========================================================================
// Pure journal unit tests — no emulator, no manager. Just the data structure.
// ===========================================================================

class TTD_InputJournal_Test : public ::testing::Test
{
protected:
    ttd::TTDInputJournal _journal;

    ttd::TTDInputEvent MakeEv(uint64_t frame, uint32_t tInFrame, uint8_t key, bool pressed)
    {
        ttd::TTDInputEvent ev;
        ev.time.frame    = frame;
        ev.time.tInFrame = tInFrame;
        ev.key           = key;
        ev.pressed       = pressed;
        return ev;
    }
};

TEST_F(TTD_InputJournal_Test, InitiallyEmpty)
{
    EXPECT_TRUE(_journal.IsEmpty());
    EXPECT_EQ(_journal.Size(), 0u);
    EXPECT_TRUE(_journal.Events().empty());
}

TEST_F(TTD_InputJournal_Test, Record_AppendsAndPreservesFields)
{
    _journal.Record(MakeEv(5, 1000, ZXKEY_SPACE, true));
    EXPECT_EQ(_journal.Size(), 1u);
    EXPECT_FALSE(_journal.IsEmpty());

    const auto& events = _journal.Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].time.frame, 5u);
    EXPECT_EQ(events[0].time.tInFrame, 1000u);
    EXPECT_EQ(events[0].key, static_cast<uint8_t>(ZXKEY_SPACE));
    EXPECT_TRUE(events[0].pressed);
}

TEST_F(TTD_InputJournal_Test, Record_MultipleEvents_AppendedInOrder)
{
    _journal.Record(MakeEv(1, 0,    ZXKEY_SPACE, true));
    _journal.Record(MakeEv(1, 100,  ZXKEY_A,     true));
    _journal.Record(MakeEv(2, 0,    ZXKEY_SPACE, false));
    _journal.Record(MakeEv(5, 5000, ZXKEY_ENTER, true));

    EXPECT_EQ(_journal.Size(), 4u);
    EXPECT_EQ(_journal.Events()[0].time.frame, 1u);
    EXPECT_EQ(_journal.Events()[3].time.frame, 5u);
}

TEST_F(TTD_InputJournal_Test, Clear_ResetsToEmpty)
{
    _journal.Record(MakeEv(1, 0, ZXKEY_SPACE, true));
    _journal.Record(MakeEv(2, 0, ZXKEY_A,     true));
    ASSERT_EQ(_journal.Size(), 2u);

    _journal.Clear();
    EXPECT_TRUE(_journal.IsEmpty());
    EXPECT_EQ(_journal.Size(), 0u);
}

// ---------------------------------------------------------------------------
// PeekNextEventTimeOnOrAfter
// ---------------------------------------------------------------------------

TEST_F(TTD_InputJournal_Test, Peek_ReturnsDefaultWhenJournalEmpty)
{
    const ttd::TTDTimePoint from{1, 0};
    const auto result = _journal.PeekNextEventTimeOnOrAfter(from);
    EXPECT_EQ(result.frame, 0u);
    EXPECT_EQ(result.tInFrame, 0u);
}

TEST_F(TTD_InputJournal_Test, Peek_ReturnsExactMatchWhenPresent)
{
    _journal.Record(MakeEv(1, 0,   ZXKEY_SPACE, true));
    _journal.Record(MakeEv(3, 100, ZXKEY_A,     true));
    _journal.Record(MakeEv(5, 0,   ZXKEY_SPACE, false));

    // Ask for events at-or-after (3, 50) — should land on (3, 100).
    const auto result = _journal.PeekNextEventTimeOnOrAfter({3, 50});
    EXPECT_EQ(result.frame,    3u);
    EXPECT_EQ(result.tInFrame, 100u);
}

TEST_F(TTD_InputJournal_Test, Peek_ReturnsFirstEventWhenFromBeforeAll)
{
    _journal.Record(MakeEv(10, 0, ZXKEY_SPACE, true));
    _journal.Record(MakeEv(20, 0, ZXKEY_A,     true));

    const auto result = _journal.PeekNextEventTimeOnOrAfter({0, 0});
    EXPECT_EQ(result.frame, 10u);
}

TEST_F(TTD_InputJournal_Test, Peek_ReturnsDefaultWhenFromAfterAll)
{
    _journal.Record(MakeEv(10, 0,   ZXKEY_SPACE, true));
    _journal.Record(MakeEv(20, 100, ZXKEY_A,     true));

    const auto result = _journal.PeekNextEventTimeOnOrAfter({30, 0});
    EXPECT_EQ(result.frame,    0u);
    EXPECT_EQ(result.tInFrame, 0u);
}

TEST_F(TTD_InputJournal_Test, Peek_SkipsEventsStrictlyBeforeFrom)
{
    _journal.Record(MakeEv(1, 0,   ZXKEY_A, true));
    _journal.Record(MakeEv(1, 100, ZXKEY_B, true));
    _journal.Record(MakeEv(2, 50,  ZXKEY_C, true));

    // Ask from (1, 100) — the event at exactly (1, 100) is "at-or-after"
    // itself, so we should land on it, not skip to (2, 50).
    const auto result = _journal.PeekNextEventTimeOnOrAfter({1, 100});
    EXPECT_EQ(result.frame,    1u);
    EXPECT_EQ(result.tInFrame, 100u);
}

// ---------------------------------------------------------------------------
// DropAfter (Resume-from-past truncation hook for Item 5)
// ---------------------------------------------------------------------------

TEST_F(TTD_InputJournal_Test, DropAfter_RemovesEventsStrictlyAfterThreshold)
{
    _journal.Record(MakeEv(1, 0,   ZXKEY_A, true));
    _journal.Record(MakeEv(2, 0,   ZXKEY_B, true));
    _journal.Record(MakeEv(3, 0,   ZXKEY_C, true));
    _journal.Record(MakeEv(4, 0,   ZXKEY_D, true));

    _journal.DropAfter({2, 0});

    EXPECT_EQ(_journal.Size(), 2u);
    EXPECT_EQ(_journal.Events()[0].time.frame, 1u);
    EXPECT_EQ(_journal.Events()[1].time.frame, 2u);
}

TEST_F(TTD_InputJournal_Test, DropAfter_KeepsEventsAtExactThreshold)
{
    _journal.Record(MakeEv(1, 100, ZXKEY_A, true));
    _journal.Record(MakeEv(1, 200, ZXKEY_B, true));
    _journal.Record(MakeEv(1, 200, ZXKEY_C, true));  // same time, different key
    _journal.Record(MakeEv(1, 300, ZXKEY_D, true));

    _journal.DropAfter({1, 200});

    EXPECT_EQ(_journal.Size(), 3u);  // (1,100), (1,200), (1,200)
    EXPECT_EQ(_journal.Events()[2].key, static_cast<uint8_t>(ZXKEY_C));
}

TEST_F(TTD_InputJournal_Test, DropAfter_OnEmptyJournal_IsNoOp)
{
    _journal.DropAfter({100, 0});
    EXPECT_TRUE(_journal.IsEmpty());
}

TEST_F(TTD_InputJournal_Test, DropAfter_WithFutureThreshold_KeepsEverything)
{
    _journal.Record(MakeEv(1, 0, ZXKEY_A, true));
    _journal.Record(MakeEv(2, 0, ZXKEY_B, true));

    _journal.DropAfter({1000, 0});
    EXPECT_EQ(_journal.Size(), 2u);
}

TEST_F(TTD_InputJournal_Test, DropAfter_WithPastThreshold_ClearsAll)
{
    _journal.Record(MakeEv(5, 0, ZXKEY_A, true));
    _journal.Record(MakeEv(6, 0, ZXKEY_B, true));

    _journal.DropAfter({0, 0});
    EXPECT_TRUE(_journal.IsEmpty());
}

// ===========================================================================
// Inject path — InjectDueEvents via the real Keyboard
// ===========================================================================

class TTD_InputJournal_Inject_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Keyboard* _keyboard = nullptr;
    ttd::TTDInputJournal _journal;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _keyboard = _context->pKeyboard;
        ASSERT_NE(_keyboard, nullptr);

        // Make sure the matrix starts in a known state.
        _keyboard->Reset();
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

    /// Read a keyboard matrix row by its ULA port address. The port's low
    /// byte determines which row is read; bit 0 = row 0 (CAPS-V), bit 4 =
    /// row 4 (EDIT-6). A bit is LOW (0) when the corresponding key is
    /// pressed; HIGH (1) when released. We invert to make assertions read
    /// naturally (true = key is currently pressed).
    ///
    /// Port address for row N: 0xFEFE | (~(1 << N) & 0xFF) << 8 — i.e.
    /// 0xFEFE, 0xFDFE, 0xFBFE, 0xF7FE, 0xEFFE for rows 0..4.
    bool IsKeyPressedAtRow(uint8_t row, ZXKeysEnum key)
    {
        const uint16_t port = 0xFE00 | static_cast<uint16_t>(~(1u << row) & 0xFF);
        const uint8_t rowBits = _keyboard->HandlePortIn(port);
        // Bit for the key column. Column is determined by the key enum's
        // bit-encoding. ZXKEY_SPACE = 0x20 — its bit-3 in the row data.
        // We don't need to decode this in general; the caller is expected
        // to know the bit position for the specific key being tested.
        (void)key;
        return rowBits != 0xFF;  // any key pressed in this row
    }

    ttd::TTDInputEvent MakeEv(uint64_t frame, uint32_t tInFrame, ZXKeysEnum key, bool pressed)
    {
        ttd::TTDInputEvent ev;
        ev.time.frame    = frame;
        ev.time.tInFrame = tInFrame;
        ev.key           = static_cast<uint8_t>(key);
        ev.pressed       = pressed;
        return ev;
    }
};

TEST_F(TTD_InputJournal_Inject_Test, InjectDueEvents_ReturnsZeroWhenNoEventsMatch)
{
    _journal.Record(MakeEv(5, 100, ZXKEY_SPACE, true));
    _journal.Record(MakeEv(5, 500, ZXKEY_A,     true));

    const size_t injected = _journal.InjectDueEvents(*_keyboard, {5, 200});
    EXPECT_EQ(injected, 0u);
}

TEST_F(TTD_InputJournal_Inject_Test, InjectDueEvents_ReturnsCountWhenMatches)
{
    _journal.Record(MakeEv(5, 100, ZXKEY_SPACE, true));
    _journal.Record(MakeEv(5, 100, ZXKEY_A,     true));
    _journal.Record(MakeEv(5, 500, ZXKEY_B,     true));

    const size_t injected = _journal.InjectDueEvents(*_keyboard, {5, 100});
    EXPECT_EQ(injected, 2u);
}

TEST_F(TTD_InputJournal_Inject_Test, InjectDueEvents_PressesAndReleasesKeyOnKeyboard)
{
    // SPACE sits in keyboard matrix row 7, bit 0 (see _zxKeyMap). We don't
    // need to verify the matrix state through HandlePortIn (the column-bit
    // encoding is annoying to derive); we just verify the journal correctly
    // dispatches the call by counting. PressKey/ReleaseKey on the real
    // Keyboard already have their own coverage in keyboard_test.
    //
    // We DO exercise a press+release round trip here so any crash in the
    // inject path surfaces immediately (e.g. bad cast, missing method).
    _journal.Record(MakeEv(5, 100, ZXKEY_SPACE, true));
    _journal.Record(MakeEv(5, 200, ZXKEY_SPACE, false));

    EXPECT_EQ(_journal.InjectDueEvents(*_keyboard, {5, 100}), 1u);
    EXPECT_EQ(_journal.InjectDueEvents(*_keyboard, {5, 150}), 0u);
    EXPECT_EQ(_journal.InjectDueEvents(*_keyboard, {5, 200}), 1u);
}

TEST_F(TTD_InputJournal_Inject_Test, InjectDueEvents_AtSameTime_FiresAllMatchingEvents)
{
    // Two simultaneous presses at the same TTDTimePoint (e.g. user held
    // two keys on the same frame boundary). Both should inject.
    _journal.Record(MakeEv(5, 0, ZXKEY_A, true));
    _journal.Record(MakeEv(5, 0, ZXKEY_S, true));

    EXPECT_EQ(_journal.InjectDueEvents(*_keyboard, {5, 0}), 2u);
    EXPECT_EQ(_journal.InjectDueEvents(*_keyboard, {5, 0}), 2u);  // Idempotent re-inject
}

// ===========================================================================
// Capture integration — DebugKeyboardManager → TimeTravelManager → journal
// ===========================================================================

class TTD_InputJournal_Capture_Test : public ::testing::Test
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

        // Enable TTD features
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
};

TEST_F(TTD_InputJournal_Capture_Test, PressKey_OutsideRecording_DoesNotJournalize)
{
    // Before StartRecording, journal must be empty
    EXPECT_EQ(_ttd->GetInputJournal().Size(), 0u);

    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);
    km->PressKey(ZXKEY_SPACE);
    km->ReleaseKey(ZXKEY_SPACE);

    EXPECT_EQ(_ttd->GetInputJournal().Size(), 0u)
        << "Capture must be gated on Recording state";
}

TEST_F(TTD_InputJournal_Capture_Test, PressKey_DuringRecording_JournalizesPressEvent)
{
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_EQ(_ttd->GetInputJournal().Size(), 0u);

    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);
    km->PressKey(ZXKEY_SPACE);

    ASSERT_EQ(_ttd->GetInputJournal().Size(), 1u);
    const auto& ev = _ttd->GetInputJournal().Events()[0];
    EXPECT_EQ(ev.key, static_cast<uint8_t>(ZXKEY_SPACE));
    EXPECT_TRUE(ev.pressed);
    // Time should reflect the current frame (frame 0 — emulator hasn't run)
    EXPECT_EQ(ev.time.frame, _context->emulatorState.frame_counter);
}

TEST_F(TTD_InputJournal_Capture_Test, ReleaseKey_DuringRecording_JournalizesReleaseEvent)
{
    ASSERT_TRUE(_ttd->StartRecording());

    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);
    km->PressKey(ZXKEY_A);
    km->ReleaseKey(ZXKEY_A);

    ASSERT_EQ(_ttd->GetInputJournal().Size(), 2u);
    EXPECT_TRUE (_ttd->GetInputJournal().Events()[0].pressed);
    EXPECT_FALSE(_ttd->GetInputJournal().Events()[1].pressed);
    EXPECT_EQ(_ttd->GetInputJournal().Events()[1].key,
              static_cast<uint8_t>(ZXKEY_A));
}

TEST_F(TTD_InputJournal_Capture_Test, PressKey_DuringReplay_DoesNotJournalize)
{
    // Even though we're Recording, replay must suppress capture (matches
    // the Item 2 suppression guard: capture happens AFTER the replay check
    // in PressKey, so the journal never sees the event).
    ASSERT_TRUE(_ttd->StartRecording());

    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);

    _ttd->EnterReplayMode();
    km->PressKey(ZXKEY_SPACE);
    km->ReleaseKey(ZXKEY_SPACE);
    _ttd->ExitReplayMode();

    EXPECT_EQ(_ttd->GetInputJournal().Size(), 0u)
        << "Replay must suppress journal capture — injected events are "
        << "drawn from history, not re-recorded";
}

TEST_F(TTD_InputJournal_Capture_Test, InjectDueInputEvents_ManagerHelper_ReturnsCount)
{
    ASSERT_TRUE(_ttd->StartRecording());

    // Record two events at the same TTDTimePoint
    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);
    km->PressKey(ZXKEY_A);
    km->PressKey(ZXKEY_S);

    ASSERT_EQ(_ttd->GetInputJournal().Size(), 2u);

    // Move into replay mode and inject — both events fire at the captured
    // TTDTimePoint. RecordInputEvent reads the intra-frame position from
    // z80.t (the per-frame t-state counter), so we must compute `now` the
    // same way for the injection to match.
    _ttd->EnterReplayMode();
    ttd::TTDTimePoint now;
    now.frame    = _context->emulatorState.frame_counter;
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    ASSERT_NE(z80, nullptr);
    now.tInFrame = z80->t;
    const size_t injected = _ttd->InjectDueInputEvents(now);
    EXPECT_EQ(injected, 2u);
    _ttd->ExitReplayMode();
}

TEST_F(TTD_InputJournal_Capture_Test, InjectDueInputEvents_ManagerHelper_NoOpWhenNotInReplay)
{
    ASSERT_TRUE(_ttd->StartRecording());

    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);
    km->PressKey(ZXKEY_SPACE);

    // Replay not engaged — defensive no-op
    ttd::TTDTimePoint now;
    now.frame = _context->emulatorState.frame_counter;
    const size_t injected = _ttd->InjectDueInputEvents(now);
    EXPECT_EQ(injected, 0u);
}

// ---------------------------------------------------------------------------
// Lifecycle integration
// ---------------------------------------------------------------------------

TEST_F(TTD_InputJournal_Capture_Test, StartRecording_ClearsPriorJournal)
{
    ASSERT_TRUE(_ttd->StartRecording());
    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);
    km->PressKey(ZXKEY_A);
    ASSERT_EQ(_ttd->GetInputJournal().Size(), 1u);

    // Stop and restart — journal must be empty
    _ttd->StopRecording();
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_EQ(_ttd->GetInputJournal().Size(), 0u)
        << "StartRecording must reset the journal — old history is invalid";
}

TEST_F(TTD_InputJournal_Capture_Test, StopRecording_PreservesJournal)
{
    ASSERT_TRUE(_ttd->StartRecording());
    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);
    km->PressKey(ZXKEY_A);
    ASSERT_EQ(_ttd->GetInputJournal().Size(), 1u);

    _ttd->StopRecording();

    EXPECT_EQ(_ttd->GetInputJournal().Size(), 1u)
        << "StopRecording must preserve the journal — the seek engine may "
        << "still need to replay events from the recorded timeline";

    // Capture also stops (no Recording state)
    km->PressKey(ZXKEY_B);
    EXPECT_EQ(_ttd->GetInputJournal().Size(), 1u)
        << "Post-StopRecording presses must not be captured";
}

TEST_F(TTD_InputJournal_Capture_Test, InvalidateSession_ClearsJournal)
{
    ASSERT_TRUE(_ttd->StartRecording());
    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);
    km->PressKey(ZXKEY_A);
    ASSERT_EQ(_ttd->GetInputJournal().Size(), 1u);

    _ttd->InvalidateSession("test");

    EXPECT_EQ(_ttd->GetInputJournal().Size(), 0u)
        << "InvalidateSession must drop the journal together with the timeline";
}

// ===========================================================================
// General Sound host stimuli (automation send_command / send_data / nmi /
// reset_card / reset). The card's Z80 is stepped by every host-port call, so
// automation never calls the card from its own thread: the stimulus is live
// input, applied on the machine's thread at an instruction boundary and
// journaled while recording.
// ===========================================================================

class TTD_InputJournalGS_Test : public ::testing::Test
{
protected:
    // Keep the General Sound card the ATM710 config fits (GSType=Z80)
    SoundCardScope _soundCards{TestSound::GeneralSound};

    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM710", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        ASSERT_NE(Card(), nullptr) << "ATM710 must fit a GS card by default";
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    GeneralSoundCard* Card() const { return _context->pSoundManager->getGeneralSound(); }

    static ttd::TTDInputEvent GS(ttd::TTDInputKind kind, uint8_t value = 0)
    {
        ttd::TTDInputEvent ev;
        ev.kind = kind;
        ev.value = value;
        return ev;
    }

    /// Everything a GS stimulus can change: mailbox, banking, CPU registers
    struct CardState
    {
        uint8_t status, commandFromHost, dataFromHost, dataToHost, page;
        bool halted;
        uint16_t regs[static_cast<int>(GSCpuRegister::MEMPTR) + 1];

        bool operator==(const CardState& o) const
        {
            return status == o.status && commandFromHost == o.commandFromHost && dataFromHost == o.dataFromHost &&
                   dataToHost == o.dataToHost && page == o.page && halted == o.halted &&
                   std::equal(std::begin(regs), std::end(regs), std::begin(o.regs));
        }
    };

    CardState Observe() const
    {
        const GeneralSoundCard* gs = Card();
        CardState st{};
        st.status = gs->getStatusRaw();
        st.commandFromHost = gs->getCommandFromHost();
        st.dataFromHost = gs->getDataFromHost();
        st.dataToHost = gs->getDataToHost();
        st.page = gs->getMPAG();
        st.halted = gs->isCPUHalted();
        for (int r = 0; r <= static_cast<int>(GSCpuRegister::MEMPTR); r++)
            st.regs[r] = gs->getCPUReg(static_cast<GSCpuRegister>(r));
        return st;
    }
};

/// Each kind reaches the card through the host-port contract; without a card
/// the event is reported as not applied
TEST_F(TTD_InputJournalGS_Test, Apply_EachKindDrivesTheCard)
{
    GeneralSoundCard* gs = Card();
    const uint64_t nmisBefore = gs->getActivityCounters().nmisAccepted;

    ASSERT_TRUE(ttd::TTDInputJournal::Apply(GS(ttd::TTDInputKind::GSCommand, 0x5A), nullptr, nullptr, gs));
    EXPECT_EQ(gs->getCommandFromHost(), 0x5A);
    EXPECT_NE(gs->getStatusRaw() & 0x01, 0) << "OUT #BB sets the command flag";

    ASSERT_TRUE(ttd::TTDInputJournal::Apply(GS(ttd::TTDInputKind::GSData, 0xA5), nullptr, nullptr, gs));
    EXPECT_EQ(gs->getDataFromHost(), 0xA5);
    EXPECT_NE(gs->getStatusRaw() & 0x80, 0) << "OUT #B3 sets the data flag";

    // #33 bit 7: the card restarts, the host mailbox (external flip-flops) survives
    ASSERT_TRUE(ttd::TTDInputJournal::Apply(GS(ttd::TTDInputKind::GSResetCard), nullptr, nullptr, gs));
    EXPECT_EQ(gs->getCPUReg(GSCpuRegister::PC), 0);
    EXPECT_NE(gs->getStatusRaw() & 0x01, 0) << "reset_card must keep the pending command";

    ASSERT_TRUE(ttd::TTDInputJournal::Apply(GS(ttd::TTDInputKind::GSNmi), nullptr, nullptr, gs));
    _emulator->RunNFrames(1);  // the NMI is taken by the card's next instruction
    EXPECT_GT(gs->getActivityCounters().nmisAccepted, nmisBefore);

    // Power-on reset clears the mailbox too
    ASSERT_TRUE(ttd::TTDInputJournal::Apply(GS(ttd::TTDInputKind::GSCommand, 0x11), nullptr, nullptr, gs));
    ASSERT_TRUE(ttd::TTDInputJournal::Apply(GS(ttd::TTDInputKind::GSReset), nullptr, nullptr, gs));
    EXPECT_EQ(gs->getStatusRaw() & 0x81, 0) << "full reset must clear the command and data flags";

    for (auto kind : {ttd::TTDInputKind::GSCommand, ttd::TTDInputKind::GSData, ttd::TTDInputKind::GSNmi,
                      ttd::TTDInputKind::GSResetCard, ttd::TTDInputKind::GSReset})
        EXPECT_FALSE(ttd::TTDInputJournal::Apply(GS(kind, 1), nullptr, nullptr, nullptr));
}

/// Loop not running: the caller is the only thread, the stimulus applies at
/// once and is journaled at that exact time while recording
TEST_F(TTD_InputJournalGS_Test, SubmitLiveInput_AppliesAndJournalsWhileRecording)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNCPUCycles(1000);

    ASSERT_TRUE(_ttd->SubmitLiveInput(GS(ttd::TTDInputKind::GSData, 0x42)));
    EXPECT_EQ(Card()->getDataFromHost(), 0x42);

    const auto& events = _ttd->GetInputJournal().Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, ttd::TTDInputKind::GSData);
    EXPECT_EQ(events[0].value, 0x42);
    EXPECT_EQ(events[0].time.frame, _context->emulatorState.frame_counter);
    EXPECT_EQ(events[0].time.tInFrame, _context->pCore->GetZ80()->t);
    _ttd->StopRecording();
}

/// While the journal drives the machine every live source is refused - the
/// card must not change behind the replay's back
TEST_F(TTD_InputJournalGS_Test, SubmitLiveInput_RefusedWhileReplayOwnsInput)
{
    const CardState before = Observe();
    _context->ttdReplayActive = true;
    EXPECT_FALSE(_ttd->SubmitLiveInput(GS(ttd::TTDInputKind::GSCommand, 0x33)));
    EXPECT_FALSE(_ttd->SubmitLiveInput(GS(ttd::TTDInputKind::GSReset)));
    _context->ttdReplayActive = false;
    EXPECT_TRUE(Observe() == before);
}

/// The race this path exists for: automation runs on its own thread while the
/// emulator loop steps the card. The stimulus is only queued there; the loop
/// thread applies it at an instruction boundary. Paused, it waits for execution
/// to continue - nothing touches the card while the machine is parked.
/// Runs the async loop (~2-3 frames): the cross-thread hand-off is the subject
TEST_F(TTD_InputJournalGS_Test, SubmitLiveInput_FromAnotherThreadIsAppliedByTheLoop)
{
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([this] { return _emulator->GetState() == StateRun; }));

    _emulator->Pause();
    ASSERT_TRUE(_emulator->WaitForPauseConfirmation(2000));
    const uint64_t writesBefore = Card()->getActivityCounters().hostDataWritten;

    ASSERT_TRUE(_ttd->SubmitLiveInput(GS(ttd::TTDInputKind::GSData, 0x6B)));
    EXPECT_TRUE(_context->ttdInputWork.load()) << "queued for the loop thread";
    EXPECT_EQ(Card()->getActivityCounters().hostDataWritten, writesBefore) << "applied while the machine was parked";

    _emulator->Resume();
    ASSERT_TRUE(TestWait::For([this] { return !_context->ttdInputWork.load(); }));
    _emulator->Pause();
    ASSERT_TRUE(_emulator->WaitForPauseConfirmation(2000));

    EXPECT_EQ(Card()->getActivityCounters().hostDataWritten, writesBefore + 1);
    EXPECT_EQ(Card()->getDataFromHost(), 0x6B);
    _emulator->Stop();
}

/// Stimuli recorded mid-frame are replayed at the same instruction when the
/// history is executed again after a seek: the card arrives in the same state
TEST_F(TTD_InputJournalGS_Test, RecordedStimuliReplayExactly)
{
    _emulator->RunNFrames(2);  // card firmware past its cold start
    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _context->emulatorState.frame_counter;

    _emulator->RunNCPUCycles(3000);
    ASSERT_TRUE(_ttd->SubmitLiveInput(GS(ttd::TTDInputKind::GSData, 0x10)));
    _emulator->RunNCPUCycles(777);
    ASSERT_TRUE(_ttd->SubmitLiveInput(GS(ttd::TTDInputKind::GSCommand, 0x30)));
    _emulator->RunNFrames(1);
    ASSERT_TRUE(_ttd->SubmitLiveInput(GS(ttd::TTDInputKind::GSNmi)));
    _emulator->RunNCPUCycles(1234);
    ASSERT_TRUE(_ttd->SubmitLiveInput(GS(ttd::TTDInputKind::GSResetCard)));
    _emulator->RunNFrames(1);
    ASSERT_EQ(_ttd->GetInputJournal().Size(), 4u);

    const uint64_t endFrame = _context->emulatorState.frame_counter;
    const uint32_t endT = _context->pCore->GetZ80()->t;
    const CardState recorded = Observe();
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo({startFrame, 0}));
    const EmulatorState* state = &_context->emulatorState;
    _emulator->RunUntilCondition([state, endFrame, endT](const Z80State& z80)
                                 { return state->frame_counter > endFrame || (state->frame_counter == endFrame && z80.t >= endT); });
    ASSERT_EQ(_context->emulatorState.frame_counter, endFrame);
    ASSERT_EQ(_context->pCore->GetZ80()->t, endT);
    EXPECT_TRUE(Observe() == recorded) << "the replayed stimuli must leave the card exactly as recorded";
}
