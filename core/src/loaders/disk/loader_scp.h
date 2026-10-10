#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_scp.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_scp.h"
#include "emulator/io/fdc/diskimage.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderSCP with the emulator's context: the options come from its configuration
class LoaderSCP : public unrealdisk::LoaderSCP
{
public:
    using unrealdisk::LoaderSCP::LoaderSCP;
    LoaderSCP(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderSCP(DiskLoaderOptions(context), filepath) {}
};
