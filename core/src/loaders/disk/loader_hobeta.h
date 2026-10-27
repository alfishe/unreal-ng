#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_hobeta.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_hobeta.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/trdos.h"
#include "loaders/disk/loader_scl.h"
#include "loaders/disk/diskloaderoptions.h"


/// unrealdisk::LoaderHobeta with the emulator's context: the options come from its configuration
class LoaderHobeta : public unrealdisk::LoaderHobeta
{
public:
    using unrealdisk::LoaderHobeta::LoaderHobeta;
    LoaderHobeta(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderHobeta(DiskLoaderOptions(context), filepath) {}
};

#ifdef _CODE_UNDER_TEST
/// unrealdisk::LoaderHobetaCUT with the emulator's context: the options come from its configuration
class LoaderHobetaCUT : public unrealdisk::LoaderHobetaCUT
{
public:
    using unrealdisk::LoaderHobetaCUT::LoaderHobetaCUT;
    LoaderHobetaCUT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderHobetaCUT(DiskLoaderOptions(context), filepath) {}
};
#endif  // _CODE_UNDER_TEST
