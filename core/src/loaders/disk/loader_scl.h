#pragma once

// Forwards to unreal-disk (unrealdisk/loaders/loader_scl.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/loaders/loader_scl.h"
#include "loaders/disk/loader_trd.h"
#include "loaders/disk/diskloaderoptions.h"

using unrealdisk::TRDOSDirectoryEntryBase;
using unrealdisk::TRDOSDirectoryEntry;
using unrealdisk::SCLHeader;

/// unrealdisk::LoaderSCL with the emulator's context: the options come from its configuration
class LoaderSCL : public unrealdisk::LoaderSCL
{
public:
    using unrealdisk::LoaderSCL::LoaderSCL;
    LoaderSCL(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderSCL(DiskLoaderOptions(context), filepath) {}
};

#ifdef _CODE_UNDER_TEST
/// unrealdisk::LoaderSCLCUT with the emulator's context: the options come from its configuration
class LoaderSCLCUT : public unrealdisk::LoaderSCLCUT
{
public:
    using unrealdisk::LoaderSCLCUT::LoaderSCLCUT;
    LoaderSCLCUT(EmulatorContext* context, const std::string& filepath) : unrealdisk::LoaderSCLCUT(DiskLoaderOptions(context), filepath) {}
};
#endif  // _CODE_UNDER_TEST
