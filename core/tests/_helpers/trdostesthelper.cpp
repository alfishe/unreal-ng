#include "trdostesthelper.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/io/keyboard/keyboard.h"

TRDOSTestHelper::TRDOSTestHelper(Emulator* emulator)
    : _emulator(emulator)
{
    if (_emulator)
    {
        _context = _emulator->GetContext();
        _memory = _emulator->GetMemory();
        _z80 = _context ? _context->pCore->GetZ80() : nullptr;
    }
}

uint64_t TRDOSTestHelper::executeTRDOSCommandViaBasic(const std::string& trdosCommand,
                                               uint64_t maxCycles)
{
    if (!_emulator || !_memory || !_z80)
        return 0;

    // TODO: implement safeguard that we're in SOSROM (48k) mode (not just temporary serving RST interrupts)

    // Build a BASIC program that executes the command via RANDOMIZE USR
    // Line 10: RANDOMIZE USR 15616: REM: <command>
    std::string program = "10 RANDOMIZE USR 15616: REM: " + trdosCommand + "\n";

    // Load the program into memory
    if (!_encoder.loadProgram(_memory, program))
        return 0;

    // Set PC to start of BASIC program
    uint8_t progL = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG);
    uint8_t progH = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG + 1);
    uint16_t progStart = progL | (progH << 8);
    
    _z80->pc = progStart;
    
    // Set up stack with sentinel return address
    _z80->sp = 0xFEFE;
    _memory->DirectWriteToZ80Memory(_z80->sp, SENTINEL_ADDRESS & 0xFF);
    _memory->DirectWriteToZ80Memory(_z80->sp + 1, (SENTINEL_ADDRESS >> 8) & 0xFF);

    // TODO: Reimplement with breakpoint-driven execution
    // runUntilStoppedOrCycles removed - use StartAsync/Pause/Resume pattern
    return 0;
}

uint64_t TRDOSTestHelper::executeBasicProgram(const std::string& basicProgram,
                                               uint64_t maxCycles)
{
    if (!_emulator || !_memory || !_z80)
        return 0;

    // Load the program into memory
    if (!_encoder.loadProgram(_memory, basicProgram))
        return 0;

    // Set PC to start of BASIC program
    uint8_t progL = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG);
    uint8_t progH = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG + 1);
    uint16_t progStart = progL | (progH << 8);
    
    _z80->pc = progStart;
    
    // Set up stack with sentinel return address
    _z80->sp = 0xFEFE;
    _memory->DirectWriteToZ80Memory(_z80->sp, SENTINEL_ADDRESS & 0xFF);
    _memory->DirectWriteToZ80Memory(_z80->sp + 1, (SENTINEL_ADDRESS >> 8) & 0xFF);

    // TODO: Reimplement with breakpoint-driven execution
    // runUntilStoppedOrCycles removed - use StartAsync/Pause/Resume pattern
    return 0;
}

bool TRDOSTestHelper::activateTRDOSMenu(uint64_t maxCycles)
{
    if (!_emulator || !_memory || !_z80)
        return false;

    // Activate TR-DOS ROM
    activateTRDOSROM();

    // Set PC to TR-DOS ROM initialization
    _z80->pc = TRDOS_ROM_INIT;
    
    // Set up stack with sentinel return address
    _z80->sp = 0xFEFE;
    _memory->DirectWriteToZ80Memory(_z80->sp, SENTINEL_ADDRESS & 0xFF);
    _memory->DirectWriteToZ80Memory(_z80->sp + 1, (SENTINEL_ADDRESS >> 8) & 0xFF);

    // TODO: Reimplement with breakpoint-driven execution
    // runUntilStoppedOrCycles removed - use StartAsync/Pause/Resume pattern
    return false;
}

bool TRDOSTestHelper::isTRDOSActive() const
{
    if (!_memory)
        return false;

    return _memory->isCurrentROMDOS();
}

uint64_t TRDOSTestHelper::directFormatDisk(uint8_t diskType, uint64_t maxCycles)
{
    if (!_emulator || !_memory || !_z80 || !_context)
        return 0;

    // ========================================================================
    // Guest-Authentic FORMAT Test - Breakpoint-Controlled Execution
    // See: docs/inprogress/2026-01-16-write-track-integration-test/trdos-format-test-design.md
    //
    // DESIGN RULES:
    // - Use ONLY emulator->Pause() and emulator->Resume() 
    // - NO RunNCPUCycles or cycle-based execution!
    // - Breakpoint hit pauses execution, we verify state, then Resume
    // - Real clock timeouts: max 2 seconds per step
    // ========================================================================
    
    std::cout << "========================================\n";
    std::cout << "[FORMAT] Guest-Authentic FORMAT Test\n";
    std::cout << "========================================\n";
    
    // Constants
    constexpr uint16_t BASIC_MAIN_EXEC = 0x12A2;  // MAIN-EXEC: BASIC editor loop
    constexpr uint16_t SYSVAR_BASE = 0x5C3A;      // IY should point here after init
    constexpr auto STEP_TIMEOUT = std::chrono::seconds(2);  // Max 2 seconds per step
    
    // ========================================================================
    // STEP 1-3: Setup (emulator already instantiated by test fixture)
    // ========================================================================
    std::cout << "[STEP 1-3] Setup: Emulator ready, 48K ROM, disk inserted\n";
    
    // Verify we have BASIC ROM (check for expected byte at 0x0000)
    uint8_t rom0000 = _memory->DirectReadFromZ80Memory(0x0000);
    std::cout << "[STEP 2] ROM@0x0000 = 0x" << std::hex << (int)rom0000 << std::dec << "\n";
    
    // ========================================================================
    // STEP 4: Initialize CPU state (while paused)
    // ========================================================================
    std::cout << "[STEP 4] Initialize CPU: PC=0x0000, SP=0xFFFF\n";
    
    _emulator->Pause();  // Ensure paused before modifying state
    
    _z80->pc = 0x0000;   // ROM entry point
    _z80->sp = 0xFFFF;   // Top of memory (standard Z80 reset)
    _z80->iy = 0x5C3A;   // Init to expected value for safety
    _z80->halted = false;
    
    // Enable debug mode for breakpoints
    _emulator->DebugOn();
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    if (!bpManager)
    {
        std::cout << "[STEP 4] FAIL: BreakpointManager not available\n";
        return 0;
    }
    
    // Clear any existing breakpoints
    bpManager->ClearBreakpoints();
    std::cout << "[STEP 4] CPU initialized, debug mode enabled ✓\n";
    
    // ========================================================================
    // STEP 5: Set execution breakpoint at BASIC main loop (0x12A2)
    // ========================================================================
    std::cout << "[STEP 5] Setting execution breakpoint at 0x" << std::hex << BASIC_MAIN_EXEC << std::dec << "\n";
    
    BreakpointDescriptor* bp = new BreakpointDescriptor();
    bp->type = BreakpointTypeEnum::BRK_MEMORY;
    bp->memoryType = BRK_MEM_EXECUTE;
    bp->z80address = BASIC_MAIN_EXEC;
    uint16_t bpId = bpManager->AddBreakpoint(bp);
    if (bpId == BRK_INVALID)
    {
        std::cout << "[STEP 5] FAIL: Could not add breakpoint\n";
        return 0;
    }
    std::cout << "[STEP 5] Breakpoint ID " << bpId << " set at 0x12A2 ✓\n";
    
    // Set up MessageCenter observer for breakpoint hit notification
    std::atomic<bool> bpHit{false};
    std::atomic<uint16_t> bpHitAddress{0};
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    auto handler = [&bpHit, &bpHitAddress, this](int id, Message* msg) {
        bpHitAddress.store(_z80->pc);
        bpHit.store(true);
        std::cout << "[BREAKPOINT] Hit at PC=0x" << std::hex << _z80->pc << std::dec << "\n";
        // Note: Do NOT resume here - let the test loop handle it
    };
    mc.AddObserver(NC_EXECUTION_BREAKPOINT, handler);
    std::cout << "[STEP 5] MessageCenter observer registered ✓\n";
    
    // ========================================================================
    // STEP 6: Start emulator and wait for breakpoint (real clock timeout)
    // ========================================================================
    std::cout << "[STEP 6] Starting emulator, waiting for breakpoint...\n";
    
    // Reset emulator to ensure clean state (important for ROM boot)
    _emulator->Reset();
    std::cout << "[STEP 6] Emulator reset complete\n";
    
    // Start the emulator mainloop asynchronously
    _emulator->StartAsync();
    std::cout << "[STEP 6] Emulator started, IsRunning=" << _emulator->IsRunning() << "\n";
    
    // Resume execution (mainloop will run until breakpoint)
    _emulator->Resume();
    std::cout << "[STEP 6] Emulator resumed, IsPaused=" << _emulator->IsPaused() << "\n";
    
    // Wait for breakpoint hit with real clock timeout (2 seconds)
    // Breakpoint might pause emulator OR fire MessageCenter event
    auto startTime = std::chrono::steady_clock::now();
    while (!bpHit.load() && !_emulator->IsPaused())
    {
        if (std::chrono::steady_clock::now() - startTime > STEP_TIMEOUT)
        {
            std::cout << "[STEP 6] TIMEOUT! Breakpoint not hit within 2 seconds\n";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Pause emulator after breakpoint or timeout
    _emulator->Pause();
    
    std::cout << "[STEP 6] Execution stopped. PC=0x" << std::hex << _z80->pc << std::dec << "\n";
    std::cout << "[STEP 6] Breakpoint triggered (MessageCenter): " << (bpHit.load() ? "YES" : "NO") << "\n";
    std::cout << "[STEP 6] Emulator paused (alternate detection): " << (_emulator->IsPaused() ? "YES" : "NO") << "\n";
    
    // Cleanup: remove observer and breakpoint
    mc.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);
    bpManager->RemoveBreakpointByID(bpId);
    
    // ========================================================================
    // STEP 7: Verify BASIC init success
    // ========================================================================
    std::cout << "[STEP 7] Verifying BASIC initialization...\n";
    
    if (!bpHit.load())
    {
        std::cout << "[STEP 7] FAIL: Breakpoint at 0x12A2 was NOT triggered (timeout)\n";
        std::cout << "[STEP 7] Current PC = 0x" << std::hex << _z80->pc << std::dec << "\n";
        _emulator->Stop();
        return 0;
    }
    std::cout << "[STEP 7] Breakpoint triggered ✓\n";
    
    // Check IY points to system variables (0x5C3A)
    if (_z80->iy != SYSVAR_BASE)
    {
        std::cout << "[STEP 7] FAIL: IY != 0x5C3A (got 0x" << std::hex << _z80->iy << std::dec << ")\n";
        _emulator->Stop();
        return 0;
    }
    std::cout << "[STEP 7] IY = 0x5C3A ✓\n";
    
    // Check ERR_NR (0x5C3A) = 0xFF (no error)
    uint8_t errNr = _memory->DirectReadFromZ80Memory(SYSVAR_BASE);
    if (errNr != 0xFF)
    {
        std::cout << "[STEP 7] FAIL: ERR_NR != 0xFF (got 0x" << std::hex << (int)errNr << std::dec << ")\n";
        _emulator->Stop();
        return 0;
    }
    std::cout << "[STEP 7] ERR_NR = 0xFF ✓\n";
    
    std::cout << "========================================\n";
    std::cout << "[CHECKPOINT] Steps 1-7 PASSED ✓\n";
    std::cout << "========================================\n";
    
    // Stop emulator for now (Steps 8-20 will continue after this passes)
    _emulator->Stop();
    
    // TODO: Steps 8-20 will be implemented after Step 7 passes
    
    return 1;  // Success
}

bool TRDOSTestHelper::verifyTRDOSVariables() const
{
    if (!_memory)
        return false;

    // Check PROG and VARS are reasonable
    uint8_t progL = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG);
    uint8_t progH = _memory->DirectReadFromZ80Memory(SystemVariables48k::PROG + 1);
    uint16_t progAddr = progL | (progH << 8);

    uint8_t varsL = _memory->DirectReadFromZ80Memory(SystemVariables48k::VARS);
    uint8_t varsH = _memory->DirectReadFromZ80Memory(SystemVariables48k::VARS + 1);
    uint16_t varsAddr = varsL | (varsH << 8);

    // VARS should be >= PROG
    if (varsAddr < progAddr)
        return false;

    // Both should be in reasonable range
    if (progAddr < 0x5C00 || progAddr > 0xFF00)
        return false;

    return true;
}

uint8_t TRDOSTestHelper::getTRDOSError() const
{
    if (!_memory)
        return 0xFF;

    return _memory->DirectReadFromZ80Memory(SystemVariables48k::ERR_NR);
}

bool TRDOSTestHelper::isExecutionStopped() const
{
    if (!_z80)
        return true;

    // Check if PC is at sentinel address
    if (_z80->pc == SENTINEL_ADDRESS || _z80->pc == SENTINEL_ADDRESS + 1)
        return true;

    // Check if HALT instruction was executed
    if (_z80->halted)
        return true;

    return false;
}


void TRDOSTestHelper::setupBasicCommandExecution(uint16_t commandAddress)
{
    if (!_z80 || !_memory)
        return;

    // Set PC to command address
    _z80->pc = commandAddress;

    // Set up stack
    _z80->sp = 0xFEFE;
    
    // Push sentinel return address
    _memory->DirectWriteToZ80Memory(_z80->sp, SENTINEL_ADDRESS & 0xFF);
    _memory->DirectWriteToZ80Memory(_z80->sp + 1, (SENTINEL_ADDRESS >> 8) & 0xFF);
}

void TRDOSTestHelper::activateTRDOSROM()
{
    if (!_memory)
        return;

    // Switch to TR-DOS ROM and update port decoder
    _memory->SetROMDOS(true);
}
