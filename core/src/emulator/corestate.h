#pragma once

#include "stdafx.h"

#include <emulator/io/fdc/diskimage.h>
#include <emulator/io/fdc/fdd.h>

enum BaseFrequency_t : uint8_t
{
    FREQ_3_5MHZ = 1,
    FREQ_7MHZ = 2,
    FREQ_14MHZ = 4,
    FREQ_28MHZ = 8,
    FREQ_56MHZ = 16
};

// CoreState - Runtime state for emulator media and peripherals
//
// Purpose: Tracks currently loaded media files (tape, snapshot, disk images) and runtime configuration
// Used by: Emulator core, CLI processors, WebAPI handlers, GUI components for status display
// Updated by: 
//   - Emulator::LoadTape/LoadSnapshot/LoadDisk() - Sets file paths on successful media load
//   - Media eject operations - Clears corresponding file paths
//   - Speed/frequency changes - Updates baseFreqMultiplier
// Access: Read via Emulator::GetContext()->coreState, typically for status queries and UI updates
struct CoreState
{
    uint8_t baseFreqMultiplier = (uint8_t)FREQ_3_5MHZ;     // How fast is Z80 clocked comparing to standard 3.5MHz model

    // region <Tape related>

    // Tape file selected
    std::string tapeFilePath;

    // endregion </Tape related>

    // region <Snapshot related>

    // Snapshot file loaded
    std::string snapshotFilePath;

    // endregion </Snapshot related>

    // region <FDD related>

    // What each drive holds, for display: the media manager's slots (fdd.a-d)
    // keep it current; the disk images belong to the media manager
    std::string diskFilePaths[4] = {};

    FDD* diskDrives[4] = {};

    // endregion </FDD related>
};
