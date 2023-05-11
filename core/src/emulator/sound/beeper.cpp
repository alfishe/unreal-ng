#include "beeper.h"

#include "emulatorcontext.h"
#include "emulator/sound/soundmanager.h"

/// region <Methods>
void Beeper::reset()
{

}

void Beeper::handlePortOut(uint8_t value, uint32_t frameTState)
{
    // Port #FE
    // Bit  [3]    - MIC output bit
    // Bit  [4]    - EAR output bit
    uint8_t maskedValue = value & 0b0001'1000;
    uint8_t micValue = value & 0b0000'1000;
    uint8_t earValue = value & 0b0001'0000;

    bool beeperBit = earValue > 0;

    if (earValue != _portFEState)
    {
        // TODO: create DAC table (See below)
        // For now it's just square wave with max amplitude
        int16_t left = beeperBit ? INT16_MAX : INT16_MIN;
        int16_t right = left;
        _context->pSoundManager->updateDAC(frameTState, left, right);
    }

    _portFEState = earValue;

    _prevFrameTState = frameTState;
}
/// endregion>

/// region <Helper methods>

// Process Volume change...
// @see http://www.worldofspectrum.org/faq/reference/48kreference.htm
// issue 2: 0.39D, 0.73D, 3.66D, 3.79D
// issue 3: 0.34D, 0.66D, 3.56D, 3.70D
void Beeper::prepareBeeperDACTable()
{

}
/// endregion </Helper methods>
