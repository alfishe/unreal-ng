#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_td0.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_td0.h"
#include "emulator/io/fdc/diskimage.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderTD0 with the emulator's context: the options come from its configuration
class LoaderTD0 : public unrealdisk::LoaderTD0
{
public:
    using unrealdisk::LoaderTD0::LoaderTD0;
    LoaderTD0(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderTD0(DiskLoaderOptions(context), filepath) {}
};

#ifdef _CODE_UNDER_TEST
/// unrealdisk::LoaderTD0CUT with the emulator's context: the options come from its configuration
class LoaderTD0CUT : public unrealdisk::LoaderTD0CUT
{
public:
    using unrealdisk::LoaderTD0CUT::LoaderTD0CUT;
    LoaderTD0CUT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderTD0CUT(DiskLoaderOptions(context), filepath) {}
};
#endif  // _CODE_UNDER_TEST
