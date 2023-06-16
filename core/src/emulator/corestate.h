#pragma once
#include "stdafx.h"

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

    // Tape file selected
    std::string tapeFilePath;
};
