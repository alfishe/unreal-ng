#pragma once

// Forwards to unreal-disk (unrealdisk/diskimage.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/diskimage.h"
#include "stdafx.h"   // the old header gave its users these (until the cleanup)
#include "common/dumphelper.h"   // the old header gave its users these (until the cleanup)
#include "emulator/io/fdc/fdc.h"
#include "emulator/io/fdc/trdos.h"

using unrealdisk::DiskImage;
