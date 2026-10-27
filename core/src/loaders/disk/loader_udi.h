#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_udi.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_udi.h"
#include "emulator/io/fdc/diskimage.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderUDI with the emulator's context: the options come from its configuration
class LoaderUDI : public unrealdisk::LoaderUDI
{
public:
    using unrealdisk::LoaderUDI::LoaderUDI;
    LoaderUDI(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderUDI(DiskLoaderOptions(context), filepath) {}
};

#ifdef _CODE_UNDER_TEST
/// unrealdisk::LoaderUDICUT with the emulator's context: the options come from its configuration
class LoaderUDICUT : public unrealdisk::LoaderUDICUT
{
public:
    using unrealdisk::LoaderUDICUT::LoaderUDICUT;
    LoaderUDICUT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderUDICUT(DiskLoaderOptions(context), filepath) {}
};
#endif  // _CODE_UNDER_TEST
