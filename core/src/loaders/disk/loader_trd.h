#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_trd.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_trd.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/trdos.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderTRD with the emulator's context: the options come from its configuration
class LoaderTRD : public unrealdisk::LoaderTRD
{
public:
    using unrealdisk::LoaderTRD::LoaderTRD;
    LoaderTRD(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderTRD(DiskLoaderOptions(context), filepath) {}
};

#ifdef _CODE_UNDER_TEST
/// unrealdisk::LoaderTRDCUT with the emulator's context: the options come from its configuration
class LoaderTRDCUT : public unrealdisk::LoaderTRDCUT
{
public:
    using unrealdisk::LoaderTRDCUT::LoaderTRDCUT;
    LoaderTRDCUT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderTRDCUT(DiskLoaderOptions(context), filepath) {}
};
#endif  // _CODE_UNDER_TEST
