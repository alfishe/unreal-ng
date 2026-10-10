#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_fdi.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_fdi.h"
#include "emulator/io/fdc/diskimage.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderFDI with the emulator's context: the options come from its configuration
class LoaderFDI : public unrealdisk::LoaderFDI
{
public:
    using unrealdisk::LoaderFDI::LoaderFDI;
    LoaderFDI(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderFDI(DiskLoaderOptions(context), filepath) {}
};

#ifdef _CODE_UNDER_TEST
/// unrealdisk::LoaderFDICUT with the emulator's context: the options come from its configuration
class LoaderFDICUT : public unrealdisk::LoaderFDICUT
{
public:
    using unrealdisk::LoaderFDICUT::LoaderFDICUT;
    LoaderFDICUT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderFDICUT(DiskLoaderOptions(context), filepath) {}
};
#endif  // _CODE_UNDER_TEST
