#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/memory/memory.h"

/// TTD input ownership: while the machine executes recorded history - a seek
/// replay, or running forward after a seek - its keyboard and mouse input come
/// from the journal only, applied straight to the matrix / mouse at the
/// instruction boundaries they were recorded at, and every live source is
/// refused. Live input is applied on the machine's own thread and journaled
/// at the boundary where it takes effect, so a recording driven by the host UI
/// (MessageCenter thread) replays exactly.
namespace
{
struct InputTrajectoryPoint
{
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    ttd::TTDCpuState cpu;
    uint64_t ramHash = 0;
    uint8_t matrix[8] = {};
    uint8_t mouseX = 0;
    uint8_t mouseY = 0;
    uint8_t mouseButtons = 0;
};

InputTrajectoryPoint Observe(EmulatorContext* context)
{
    const Z80* z80 = context->pCore->GetZ80();
    InputTrajectoryPoint point;
    point.frame = context->emulatorState.frame_counter;
    point.tInFrame = z80->t;
    point.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
    const size_t ramBytes = static_cast<size_t>(context->config.ramsize) * 1024u;
    point.ramHash = ttd::HashBytes(context->pMemory->RAMBase(), ramBytes);
    std::memcpy(point.matrix, reinterpret_cast<KeyboardCUT*>(context->pKeyboard)->_keyboardMatrixState,
                sizeof(point.matrix));
    point.mouseX = context->pMouse->GetX();
    point.mouseY = context->pMouse->GetY();
    point.mouseButtons = context->pMouse->GetButtons();
    return point;
}

void ExpectSamePoint(const InputTrajectoryPoint& actual, const InputTrajectoryPoint& expected)
{
    EXPECT_EQ(actual.frame, expected.frame);
    EXPECT_EQ(actual.tInFrame, expected.tInFrame);
    EXPECT_EQ(std::memcmp(&actual.cpu, &expected.cpu, sizeof(actual.cpu)), 0) << "CPU state differs";
    EXPECT_EQ(actual.ramHash, expected.ramHash) << "RAM differs (the program saw different input)";
    EXPECT_EQ(std::memcmp(actual.matrix, expected.matrix, sizeof(actual.matrix)), 0) << "keyboard matrix differs";
    EXPECT_EQ(actual.mouseX, expected.mouseX);
    EXPECT_EQ(actual.mouseY, expected.mouseY);
    EXPECT_EQ(actual.mouseButtons, expected.mouseButtons);
}
}  // namespace

class TTD_InputPlayback_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    DebugKeyboardManager* _keys = nullptr;
    DebugMouseManager* _mouse = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _keys = _context->pDebugManager->GetKeyboardManager();
        _mouse = _context->pDebugManager->GetMouseManager();
        ASSERT_NE(_keys, nullptr);
        ASSERT_NE(_mouse, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Run forward (Detached, synchronously) to exactly the recorded position
    void RunTo(const InputTrajectoryPoint& target)
    {
        const EmulatorState* state = &_context->emulatorState;
        const uint64_t frame = target.frame;
        const uint32_t t = target.tInFrame;
        _emulator->RunUntilCondition([state, frame, t](const Z80State& z80)
                                     { return state->frame_counter > frame || (state->frame_counter == frame && z80.t >= t); });
    }

    const uint8_t* Matrix() const { return reinterpret_cast<KeyboardCUT*>(_context->pKeyboard)->_keyboardMatrixState; }

    /// 48K BASIC sysvar LAST_K (23560): the ROM keyboard scan stored a key - proof
    /// the recorded input actually reached the program (else the test is vacuous)
    uint8_t LastKey() const { return _context->pMemory->DirectReadFromZ80Memory(23560); }

    /// Boot until the ROM's keyboard scan runs (48K BASIC: past the RAM test)
    static constexpr int kBootFrames = 150;

    /// A program for which every instruction matters: interrupts off, the
    /// keyboard port polled in a tight loop with an iteration counter, and on
    /// every change of the key state the counter is logged to RAM (#9000..).
    /// A journal time off by a single instruction changes the log - the ROM's
    /// once-per-frame keyboard scan would hide it
    static constexpr uint16_t kPollerAddress = 0x8000;
    static constexpr uint16_t kPollerLog = 0x9000;
    void InstallKeyPoller()
    {
        static const uint8_t program[] = {
            0xF3,                    // 8000 DI
            0x21, 0x00, 0x00,        // 8001 LD HL,0
            0xDD, 0x21, 0x00, 0x90,  // 8004 LD IX,#9000
            0x0E, 0xFF,              // 8008 LD C,#FF        previous key state
            0x23,                    // 800A loop: INC HL
            0xAF,                    // 800B XOR A           all half-rows
            0xDB, 0xFE,              // 800C IN A,(#FE)
            0xF6, 0xE0,              // 800E OR #E0          keys only (bits 0-4)
            0xB9,                    // 8010 CP C
            0x28, 0xF7,              // 8011 JR Z,loop
            0x4F,                    // 8013 LD C,A
            0xDD, 0x75, 0x00,        // 8014 LD (IX+0),L     log: counter, state
            0xDD, 0x74, 0x01,        // 8017 LD (IX+1),H
            0xDD, 0x71, 0x02,        // 801A LD (IX+2),C
            0xDD, 0x23,              // 801D INC IX
            0xDD, 0x23,              // 801F INC IX
            0xDD, 0x23,              // 8021 INC IX
            0x18, 0xE5,              // 8023 JR loop
        };
        Z80* z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(kPollerAddress + i), program[i]);
        for (uint16_t a = kPollerLog; a < kPollerLog + 0x100; a++)
            z80->DirectWrite(a, 0);
        z80->pc = kPollerAddress;
    }

    /// Key state changes the poller logged
    size_t PollerLogEntries() const
    {
        size_t entries = 0;
        for (uint16_t a = kPollerLog + 2; a < kPollerLog + 0x100; a += 3)
        {
            if (_context->pMemory->DirectReadFromZ80Memory(a) == 0)
                break;
            entries++;
        }
        return entries;
    }
};

/// Keys and mouse applied at arbitrary points while recording are replayed by
/// the journal when the recorded history is executed again after a seek: the
/// program (the ROM keyboard scan) sees the same input at the same
/// instruction, so the machine arrives in the same state
TEST_F(TTD_InputPlayback_Test, RecordedInputDrivesTheReExecutedHistory)
{
    _emulator->RunNFrames(kBootFrames);  // boot into the ROM's keyboard scan
    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _context->emulatorState.frame_counter;

    _emulator->RunNCPUCycles(3000);  // mid-frame
    _keys->PressKey(ZXKEY_A);
    _emulator->RunNFrames(3);
    _emulator->RunNCPUCycles(777);
    _keys->ReleaseKey(ZXKEY_A);
    ASSERT_TRUE(_mouse->Move(5, -3).ok());
    _emulator->RunNFrames(2);
    _keys->PressKey(ZXKEY_Q);
    _emulator->RunNCPUCycles(1234);
    _keys->ReleaseKey(ZXKEY_Q);
    ASSERT_TRUE(_mouse->Move(-2, 7).ok());
    _emulator->RunNFrames(10);
    _emulator->RunNCPUCycles(500);

    const InputTrajectoryPoint recorded = Observe(_context);
    _ttd->StopRecording();
    ASSERT_GE(_ttd->GetInputJournal().Size(), 6u);
    ASSERT_NE(LastKey(), 0) << "the ROM never scanned the keys - the replay check below would be vacuous";

    ASSERT_TRUE(_ttd->SeekTo({startFrame, 0}));
    RunTo(recorded);
    ExpectSamePoint(Observe(_context), recorded);
}

/// While the machine executes recorded history, live input is refused - the
/// journal owns input. Past the end of the recording it is accepted again
TEST_F(TTD_InputPlayback_Test, LiveInputIsRefusedWhileExecutingRecordedHistory)
{
    _emulator->RunNFrames(20);
    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(8);
    _ttd->StopRecording();
    const uint64_t endFrame = _ttd->SessionEndPosition().frame;

    ASSERT_TRUE(_ttd->SeekTo({startFrame, 0}));
    EXPECT_TRUE(_ttd->OwnsInput());

    uint8_t before[8];
    std::memcpy(before, Matrix(), sizeof(before));
    _keys->PressKey(ZXKEY_A);
    EXPECT_EQ(std::memcmp(before, Matrix(), sizeof(before)), 0) << "live key reached the matrix during playback";
    EXPECT_FALSE(_mouse->Move(3, 3).ok()) << "live mouse input must be refused during playback";

    // Still refused while running forward through the history
    _emulator->RunNFrames(2);
    EXPECT_TRUE(_ttd->OwnsInput());

    // Past the recorded end: live input again
    while (_context->emulatorState.frame_counter <= endFrame)
        _emulator->RunNFrames(1);
    EXPECT_FALSE(_ttd->OwnsInput());
    _keys->PressKey(ZXKEY_A);
    EXPECT_NE(std::memcmp(before, Matrix(), sizeof(before)), 0) << "live key refused after the recorded history ended";
}

/// A reset while positioned in the recorded history leaves it: live input again
TEST_F(TTD_InputPlayback_Test, ResetLeavesTheRecordedHistory)
{
    _emulator->RunNFrames(10);
    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(5);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo({startFrame, 0}));
    ASSERT_TRUE(_ttd->OwnsInput());
    _emulator->Reset();
    EXPECT_FALSE(_ttd->OwnsInput());
}

/// Host keystrokes arrive on the MessageCenter thread while the emulator loop
/// runs on its own. They are applied - and journaled - by the emulator thread
/// at an instruction boundary, so the recorded time is exactly when the program
/// could first see them and re-executing the history reproduces the machine
/// state (journaling the MessageCenter thread's view of z80.t did not)
TEST_F(TTD_InputPlayback_Test, HostInputFromAnotherThreadReplaysExactly)
{
    // Turbo: the loop runs frames back to back (no pacing sleep), so host
    // keystrokes land mid-frame at arbitrary instruction boundaries
    _emulator->EnableTurboMode(false);
    _emulator->StartAsync();
    const auto runDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (_emulator->GetState() != StateRun && std::chrono::steady_clock::now() < runDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(_emulator->GetState(), StateRun);

    const auto waitFrames = [this](uint64_t frames)
    {
        const uint64_t target = _context->emulatorState.frame_counter + frames;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (_context->emulatorState.frame_counter < target && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    waitFrames(kBootFrames);

    _emulator->Pause();
    ASSERT_TRUE(_emulator->WaitForPauseConfirmation(2000));
    InstallKeyPoller();
    _emulator->Resume();
    waitFrames(2);

    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _ttd->GetCheckpoint(0)->time.frame;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    const std::string id = _emulator->GetUUID();
    const ZXKeysEnum keys[] = {ZXKEY_A, ZXKEY_Q, ZXKEY_SPACE, ZXKEY_ENTER};
    for (ZXKeysEnum key : keys)
    {
        messageCenter.Post(MC_KEY_PRESSED, new KeyboardEvent(key, KEY_PRESSED, id));
        waitFrames(2);
        messageCenter.Post(MC_KEY_RELEASED, new KeyboardEvent(key, KEY_RELEASED, id));
        waitFrames(2);
    }
    const auto journalDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (_ttd->GetInputJournal().Size() < 8 && std::chrono::steady_clock::now() < journalDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_GE(_ttd->GetInputJournal().Size(), 8u) << "host keystrokes were not applied by the emulator thread";
    waitFrames(3);

    _emulator->Pause();
    ASSERT_TRUE(_emulator->WaitForPauseConfirmation(2000));
    const InputTrajectoryPoint recorded = Observe(_context);
    _ttd->StopRecording();
    ASSERT_GE(PollerLogEntries(), 8u) << "the poller never saw the keys - the replay check below would be vacuous";

    ASSERT_TRUE(_ttd->SeekTo({startFrame, 0}));
    RunTo(recorded);
    ExpectSamePoint(Observe(_context), recorded);

    _emulator->Stop();
}
