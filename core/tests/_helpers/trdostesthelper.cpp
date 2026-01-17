#include "trdostesthelper.h"
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

uint64_t TRDOSTestHelper::executeBasicCommand(const std::string& basicCommand,
                                               uint64_t maxCycles)
{
    if (!_emulator || !_memory || !_z80)
        return 0;

    // Build a BASIC program that executes the command via RANDOMIZE USR
    // Line 10: RANDOMIZE USR 15616: REM: <command>
    std::string program = "10 RANDOMIZE USR 15616: REM: " + basicCommand + "\n";

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

    // Run until stopped or max cycles
    return runUntilStoppedOrCycles(maxCycles);
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

    // Run until stopped or max cycles
    return runUntilStoppedOrCycles(maxCycles);
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

    // Run until menu is displayed or max cycles
    uint64_t cyclesExecuted = runUntilStoppedOrCycles(maxCycles);

    // Verify TR-DOS is active
    return cyclesExecuted > 0 && isTRDOSActive();
}

bool TRDOSTestHelper::isTRDOSActive() const
{
    if (!_memory)
        return false;

    return _memory->isCurrentROMDOS();
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

uint64_t TRDOSTestHelper::runUntilStoppedOrCycles(uint64_t maxCycles,
                                                   uint16_t stopAddress)
{
    if (!_emulator || !_z80)
        return 0;

    uint64_t cyclesExecuted = 0;
    const uint64_t CYCLES_PER_ITERATION = 10000;  // Run 10000 cycles at a time
    
    // Track monitoring for progress (only print on change)
    WD1793* wd1793 = _context ? _context->pBetaDisk : nullptr;
    uint8_t lastTrack = wd1793 ? wd1793->getTrackRegister() : 0xFF;

    while (cyclesExecuted < maxCycles)
    {
        // Check if we've reached the stop address
        if (_z80->pc == stopAddress)
            break;

        // Check if halted
        if (_z80->halted)
            break;

        // Execute a batch of cycles
        uint64_t cyclesToRun = std::min(CYCLES_PER_ITERATION, maxCycles - cyclesExecuted);
        _emulator->RunNCPUCycles(cyclesToRun, true);  // Skip breakpoints
        
        cyclesExecuted += cyclesToRun;
        
        // Monitor track position changes (print only when track changes)
        if (wd1793)
        {
            uint8_t currentTrack = wd1793->getTrackRegister();
            if (currentTrack != lastTrack)
            {
                std::cout << "[FDC] Track: " << (int)currentTrack << std::endl;
                lastTrack = currentTrack;
            }
        }
    }

    return cyclesExecuted;
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
