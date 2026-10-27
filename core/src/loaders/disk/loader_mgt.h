#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_mgt.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_mgt.h"
#include "emulator/io/fdc/diskimage.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderMGT with the emulator's context: the options come from its configuration
class LoaderMGT : public unrealdisk::LoaderMGT
{
public:
    using unrealdisk::LoaderMGT::LoaderMGT;
    LoaderMGT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderMGT(DiskLoaderOptions(context), filepath) {}
};

#ifdef _CODE_UNDER_TEST
/// unrealdisk::LoaderMGTCUT with the emulator's context: the options come from its configuration
class LoaderMGTCUT : public unrealdisk::LoaderMGTCUT
{
public:
    using unrealdisk::LoaderMGTCUT::LoaderMGTCUT;
    LoaderMGTCUT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderMGTCUT(DiskLoaderOptions(context), filepath) {}
};
#endif  // _CODE_UNDER_TEST
