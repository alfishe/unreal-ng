#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_dsk.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_dsk.h"
#include "emulator/io/fdc/diskimage.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderDSK with the emulator's context: the options come from its configuration
class LoaderDSK : public unrealdisk::LoaderDSK
{
public:
    using unrealdisk::LoaderDSK::LoaderDSK;
    LoaderDSK(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderDSK(DiskLoaderOptions(context), filepath) {}
};

#ifdef _CODE_UNDER_TEST
/// unrealdisk::LoaderDSKCUT with the emulator's context: the options come from its configuration
class LoaderDSKCUT : public unrealdisk::LoaderDSKCUT
{
public:
    using unrealdisk::LoaderDSKCUT::LoaderDSKCUT;
    LoaderDSKCUT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderDSKCUT(DiskLoaderOptions(context), filepath) {}
};
#endif  // _CODE_UNDER_TEST
