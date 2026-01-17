#pragma once

#include "stdafx.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/cpu/core.h"
#include "emulator/spectrumconstants.h"
#include "emulator/io/fdc/wd1793.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"

/// TRDOSTestHelper: Utilities for TR-DOS integration testing
/// Provides methods to:
/// 1. Execute BASIC commands via RANDOMIZE USR 15616 trap
/// 2. Activate TR-DOS menu/prompt
/// 3. Verify TR-DOS state and system variables
class TRDOSTestHelper
{
    /// region <Constants>
public:
    /// TR-DOS entry point via BASIC (RANDOMIZE USR 15616)
    static constexpr uint16_t TRDOS_TRAP_ADDRESS = 0x3D00;  // 15616 decimal
    
    /// TR-DOS ROM initialization entry point
    static constexpr uint16_t TRDOS_ROM_INIT = 0x0000;
    
    /// Maximum cycles to run before timeout (prevents infinite loops)
    static constexpr uint64_t MAX_EXECUTION_CYCLES = 10'000'000;  // ~3 seconds at 3.5MHz
    
    /// Sentinel return address for controlled execution
    static constexpr uint16_t SENTINEL_ADDRESS = 0xFFFE;
    /// endregion </Constants>

    /// region <Fields>
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
    Z80* _z80 = nullptr;
    BasicEncoder _encoder;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    /// Constructor - takes ownership of emulator instance
    /// @param emulator Initialized emulator instance (must have TR-DOS enabled)
    explicit TRDOSTestHelper(Emulator* emulator);
    
    virtual ~TRDOSTestHelper() = default;
    /// endregion </Constructors / Destructors>

    /// region <BASIC Command Execution>
public:
    /// Execute a BASIC command via RANDOMIZE USR 15616 trap
    /// This simulates typing a command at the BASIC prompt and executing it
    /// @param basicCommand BASIC command without line number (e.g., "FORMAT \"TEST\"")
    /// @param maxCycles Maximum cycles to execute (default: MAX_EXECUTION_CYCLES)
    /// @return Number of cycles executed, or 0 on failure
    uint64_t executeBasicCommand(const std::string& basicCommand, 
                                  uint64_t maxCycles = MAX_EXECUTION_CYCLES);

    /// Execute a full BASIC program (with line numbers)
    /// Loads the program into memory and runs it
    /// @param basicProgram Full BASIC program with line numbers
    /// @param maxCycles Maximum cycles to execute
    /// @return Number of cycles executed, or 0 on failure
    uint64_t executeBasicProgram(const std::string& basicProgram,
                                  uint64_t maxCycles = MAX_EXECUTION_CYCLES);
    /// endregion </BASIC Command Execution>

    /// region <TR-DOS Menu Activation>
public:
    /// Activate TR-DOS menu/prompt
    /// This switches from 48K BASIC to TR-DOS and shows the TR-DOS menu
    /// @param maxCycles Maximum cycles to execute
    /// @return true if TR-DOS menu activated successfully
    bool activateTRDOSMenu(uint64_t maxCycles = MAX_EXECUTION_CYCLES);

    /// Check if TR-DOS ROM is currently active
    /// @return true if TR-DOS ROM is paged in
    bool isTRDOSActive() const;
    /// endregion </TR-DOS Menu Activation>

    /// region <State Verification>
public:
    /// Verify TR-DOS system variables are initialized
    /// @return true if TR-DOS variables look valid
    bool verifyTRDOSVariables() const;

    /// Get current TR-DOS error code
    /// @return Error code from ERR_NR system variable
    uint8_t getTRDOSError() const;

    /// Check if emulator is at a breakpoint/halt
    /// @return true if execution stopped (RET to sentinel, HALT, etc.)
    bool isExecutionStopped() const;
    /// endregion </State Verification>

    /// region <Helper Methods>
protected:
    /// Run Z80 CPU for specified number of cycles or until stopped
    /// @param maxCycles Maximum cycles to run
    /// @param stopAddress Optional address to stop at (default: SENTINEL_ADDRESS)
    /// @return Actual cycles executed
    uint64_t runUntilStoppedOrCycles(uint64_t maxCycles, 
                                      uint16_t stopAddress = SENTINEL_ADDRESS);

    /// Set up Z80 registers for BASIC command execution
    /// @param commandAddress Address where command is stored
    void setupBasicCommandExecution(uint16_t commandAddress);

    /// Activate TR-DOS ROM
    void activateTRDOSROM();
    /// endregion </Helper Methods>
};
