#pragma once

#include "emulator/emulator.h"
#include "emulator/io/keyboard/keyboard.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include <string>

/// BasicTestHelper: Utilities for Spectrum BASIC program testing
/// Provides methods to inject and execute Spectrum BASIC programs in tests
class SpectrumBasicTestHelper
{
public:
    /// Constructor
    /// @param emulator Initialized emulator instance
    explicit SpectrumBasicTestHelper(Emulator* emulator);
    
    /// Inject BASIC program into memory and auto-execute it
    /// @param basicProgram BASIC program with line numbers (e.g., "10 PRINT \"HELLO\"\n20 STOP\n")
    /// @return true if injection successful
    bool injectAndRun(const std::string& basicProgram);
    
    /// Inject BASIC program and trigger RUN via keyboard
    /// @param basicProgram BASIC program text
    /// @param runCycles Number of CPU cycles to run (default: 100 frames worth)
    /// @return true if successful
    bool injectAndRunViaKeyboard(const std::string& basicProgram, unsigned runCycles = 7000000);
    
    /// Type RUN command and press ENTER
    /// Simulates user typing "RUN" at BASIC prompt
    void typeRunCommand();
    
    /// Execute CPU cycles
    /// @param cycles Number of T-states to execute
    void runCycles(unsigned cycles);
    
private:
    Emulator* _emulator;
    BasicEncoder _encoder;
};
