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
