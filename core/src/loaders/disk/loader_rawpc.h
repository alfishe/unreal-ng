#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_rawpc.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_rawpc.h"
#include "emulator/io/fdc/diskimage.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderRawPcFloppy with the emulator's context: the options come from its configuration
class LoaderRawPcFloppy : public unrealdisk::LoaderRawPcFloppy
{
public:
    using unrealdisk::LoaderRawPcFloppy::LoaderRawPcFloppy;
    LoaderRawPcFloppy(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderRawPcFloppy(DiskLoaderOptions(context), filepath) {}
};

#ifdef _CODE_UNDER_TEST
/// unrealdisk::LoaderRawPcFloppyCUT with the emulator's context: the options come from its configuration
class LoaderRawPcFloppyCUT : public unrealdisk::LoaderRawPcFloppyCUT
{
public:
    using unrealdisk::LoaderRawPcFloppyCUT::LoaderRawPcFloppyCUT;
    LoaderRawPcFloppyCUT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderRawPcFloppyCUT(DiskLoaderOptions(context), filepath) {}
};
#endif  // _CODE_UNDER_TEST
