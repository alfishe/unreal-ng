#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_hfe.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_hfe.h"
#include "emulator/io/fdc/diskimage.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderHFE with the emulator's context: the options come from its configuration
class LoaderHFE : public unrealdisk::LoaderHFE
{
public:
    using unrealdisk::LoaderHFE::LoaderHFE;
    LoaderHFE(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderHFE(DiskLoaderOptions(context), filepath) {}
};
