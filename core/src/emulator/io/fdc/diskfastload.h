#pragma once

#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"

class DiskFastLoad
{
public:
    explicit DiskFastLoad(EmulatorContext* context);
    ~DiskFastLoad();

    /// @brief Fast disk loading is armed: feature on, TR-DOS paged in, Beta 128 present, bank 0 is ROM.
    /// Also gates FDC timing compression.
    bool IsArmed() const;

    /// @brief Handle TR-DOS sector read loop trap at $3FEC (INI).
    /// Asks the FDC to drain the rest of the current sector into memory via Z80::wd(),
    /// updates HL/B/PC, and returns true if consumed. Returns false on decline.
    bool HandleSectorDrainTrap(Z80& cpu);

private:
    /// Check that mapped bank 0 at $3FEC holds the INI opcode (ED A2).
    bool IsIniAtReadLoop() const;

    EmulatorContext* _context = nullptr;
    ModuleLogger* _logger = nullptr;
};
