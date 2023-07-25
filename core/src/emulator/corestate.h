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

struct CoreState
{
    uint8_t baseFreqMultiplier = (uint8_t)FREQ_3_5MHZ;     // How fast is Z80 clocked comparing to standard 3.5MHz model

    /// region <Tape related>

    // Tape file selected
    std::string tapeFilePath;

    /// endregion <Tape related>

    /// region <FDD related>

    // Disk image files mounted
    std::string diskFilePaths[4] = {};

    // Disk images loaded
    DiskImage* diskImages[4] = {};

    FDD* diskDrives[4] = {};

    /// endregion </FDD related>
};
