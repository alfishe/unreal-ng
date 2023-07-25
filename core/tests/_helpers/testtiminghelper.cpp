#include "testtiminghelper.h"

#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"

/// region <GTest helpers>

bool areUint8ArraysEqual(const uint8_t* arr1, const uint8_t* arr2, size_t size)
{
    bool result = true;

    for (size_t i = 0; i < size; ++i)
    {
        if (arr1[i] != arr2[i])
        {
            result = false;
            break;
        }
    }

    return result;
}

/// endregion </GTest helpers>

/// region <Methods>



/// endregion </Methods>
