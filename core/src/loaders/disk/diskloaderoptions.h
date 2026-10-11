#pragma once

// The emulator's settings the unreal-disk format drivers take (unrealdisk::LoaderOptions)

#include "emulator/emulatorcontext.h"
#include "unrealdisk/options.h"

inline unrealdisk::LoaderOptions DiskLoaderOptions(EmulatorContext* context)
{
    unrealdisk::LoaderOptions options;
    if (context)
        options.trdosInterleave = context->config.trdos_interleave;
    return options;
}
