#pragma once

// Shared live-emulator fixture for DeZog adapter / server tests.
//
// Boots a real emulator instance (async thread), pauses it, and installs a tiny
// self-contained Z80 program in RAM so execution breakpoints and memory
// watchpoints can be exercised deterministically:
//
//   8000: F3           DI
//   8001: 3E 01        LD   A,1
//   8003: 32 00 90     LD   (9000h),A     ; watchpoint target
//   8006: C3 01 80     JP   8001h         ; execution breakpoint target
//
// Notifications from the adapter are captured through a condition variable so
// tests can wait for exactly one NTF_PAUSE-equivalent event with a timeout.

#include "pch.h"

#include "dezogdebugadapter.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "debugger/breakpoints/breakpointmanager.h"

#include "_helpers/testwaithelper.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class DezogEmulatorFixture : public ::testing::Test
{
protected:
    static constexpr uint16_t PROGRAM_START = 0x8000;
    static constexpr uint16_t PROGRAM_LOOP = 0x8001;
    static constexpr uint16_t PROGRAM_STORE = 0x8003;
    static constexpr uint16_t PROGRAM_JP = 0x8006;
    static constexpr uint16_t WATCH_TARGET = 0x9000;

    struct PauseEvent
    {
        dzrp::BreakReason reason;
        uint16_t address;
        uint8_t bank;
    };

    void SetUp() override
    {
        EmulatorManager* manager = EmulatorManager::GetInstance();
        ASSERT_NE(manager, nullptr);

        _emulator = manager->CreateEmulator("dezog-test", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);

        // Deterministic boot into 48K BASIC ROM regardless of staged unreal.ini
        _emulator->GetContext()->config.reset_rom = RM_SOS;
        _emulator->Reset();

        // Deliberately still a 10 ms poll, not TestWait.
        //
        // Pause() parks the emulator thread wherever it has got to - including
        // mid-frame, via the pause check inside Z80FrameCycle - and TTD decodes
        // history per frame, so the in-frame offset at which the session opens
        // decides how many instructions sit behind the baseline. The
        // history-walk tests assert on that depth.
        //
        // Measured: with this loop the park is consistent and depth is a stable
        // 7 entries. Polling faster parks inside frame 0 and depth becomes
        // 1849 / 2332 / 3058 - different every run. Waiting for a completed
        // frame first, or following Pause() with RunFrame()/RunNFrames(1), does
        // not help either: both preserve the in-frame offset rather than
        // clearing it (t landed at 65913, 26489, 30479, 5455... run to run).
        //
        // The real defect is that these tests depend on a park point no API
        // currently lets them state. Until there is a "run to the next frame
        // boundary" primitive, this wait stays as it is - the ~10 ms it costs
        // is the price of a stable baseline, and the free-run sections below,
        // which were the expensive part, are converted regardless.
        _emulator->StartAsync();
        for (int i = 0; i < 50 && _emulator->GetState() != StateRun; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ASSERT_EQ(_emulator->GetState(), StateRun);

        _emulator->Pause();
        ASSERT_TRUE(_emulator->IsPaused());

        _adapter = std::make_unique<DezogDebugAdapter>(_emulator);
        _adapter->setPauseNotifier([this](dzrp::BreakReason reason, uint16_t addr, uint8_t bank) {
            std::lock_guard<std::mutex> lock(_eventMutex);
            _events.push_back({reason, addr, bank});
            _eventCv.notify_all();
        });
    }

    void TearDown() override
    {
        if (_adapter)
        {
            _adapter->setPauseNotifier(nullptr);
            _adapter->clearTemporaryBreakpoints();
        }

        if (_emulator)
        {
            if (BreakpointManager* bpManager = _emulator->GetBreakpointManager())
                bpManager->ClearBreakpoints();

            // Release a Z80 thread parked in WaitWhilePaused before teardown
            if (_emulator->IsPaused())
                _emulator->Resume();

            std::string id = _emulator->GetId();
            _adapter.reset();
            _emulator.reset();
            EmulatorManager::GetInstance()->RemoveEmulator(id);
        }
    }

    /// Install the test program and point PC at it (emulator must be paused)
    void installProgram()
    {
        const std::vector<uint8_t> program = {
            0xF3,              // DI
            0x3E, 0x01,        // LD A,1
            0x32, 0x00, 0x90,  // LD (9000h),A
            0xC3, 0x01, 0x80   // JP 8001h
        };
        _adapter->writeMemory(PROGRAM_START, program);
        _adapter->writeMemory(WATCH_TARGET, {0x00});
        _adapter->setRegister(dzrp::RegisterId::PC, PROGRAM_START);
        _adapter->setRegister(dzrp::RegisterId::SP, 0xFF00);
    }

    /// Wait for the next pause event (returns false on timeout)
    bool waitForEvent(PauseEvent& out, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
    {
        std::unique_lock<std::mutex> lock(_eventMutex);
        if (!_eventCv.wait_for(lock, timeout, [this] { return !_events.empty(); }))
            return false;
        out = _events.front();
        _events.erase(_events.begin());
        return true;
    }

    size_t pendingEvents()
    {
        std::lock_guard<std::mutex> lock(_eventMutex);
        return _events.size();
    }

    /// @brief Prove no further pause event arrives within `settle`.
    ///
    /// The negative counterpart of waitForEvent, and the reason the tests below
    /// no longer sleep before an `EXPECT_EQ(pendingEvents(), 0)`. A fixed sleep
    /// was wrong in both directions: it always paid its full length, and it
    /// still only proved "nothing arrived in the interval I guessed". This
    /// waits on the same condition variable, so a stray event ends the wait
    /// immediately (fast, informative failure) while the passing path costs the
    /// settle window once.
    bool noFurtherEvents(std::chrono::milliseconds settle = std::chrono::milliseconds(10))
    {
        std::unique_lock<std::mutex> lock(_eventMutex);
        return !_eventCv.wait_for(lock, settle, [this] { return !_events.empty(); });
    }

    /// @brief Number of complete frames the emulated machine has run.
    uint64_t frameCounter() const { return _emulator->GetContext()->emulatorState.frame_counter; }

    /// @brief Let the machine free-run for `frames` complete frames, then pause.
    ///
    /// Replaces `Resume(); sleep_for(N ms); Pause();`. The old form specified
    /// the thing it did not care about (wall-clock time) and left the thing it
    /// did care about (how many frames, hence how much history accrued) to the
    /// machine's load - so the same test recorded a different number of
    /// checkpoints run to run. With turbo on, the frames elapse as fast as the
    /// host can produce them.
    ///
    /// @param useAdapter drive resume/pause through the adapter, so its
    ///        session bookkeeping and the manual-pause notification happen;
    ///        false drives the emulator directly, as a GUI/WebAPI client would.
    void freeRunFrames(unsigned frames, bool useAdapter = false)
    {
        const uint64_t target = frameCounter() + frames;

        // Turbo for the duration of the run only. Enabling it in SetUp would
        // change where Pause() parks (see the note there); switching it on
        // around a free run does not, and it is what turns "wait for 10 frames"
        // from 200 ms of throttled real time into a few milliseconds. Host-side
        // only, so the frames themselves are identical.
        _emulator->EnableTurboMode();

        if (useAdapter)
            _adapter->resume();
        else
            _emulator->Resume();

        const bool reached = TestWait::For([this, target] { return frameCounter() >= target; },
                                           std::chrono::milliseconds(5000));

        if (useAdapter)
            _adapter->pause();
        else
            _emulator->Pause();

        _emulator->DisableTurboMode();

        EXPECT_TRUE(reached) << "free run did not reach frame " << target << " (at " << frameCounter() << ")";
    }

    void clearEvents()
    {
        std::lock_guard<std::mutex> lock(_eventMutex);
        _events.clear();
    }

    std::shared_ptr<Emulator> _emulator;
    std::unique_ptr<DezogDebugAdapter> _adapter;

    std::mutex _eventMutex;
    std::condition_variable _eventCv;
    std::vector<PauseEvent> _events;
};
