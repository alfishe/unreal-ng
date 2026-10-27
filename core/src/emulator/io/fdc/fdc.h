#pragma once

// Forwards to unreal-disk (unrealdisk/fdc.h, core/src/3rdparty/unreal-disk): the emulator keeps its old names.

#include "unrealdisk/fdc.h"

using unrealdisk::FDD_RPM;
using unrealdisk::FDD_RPS;
using unrealdisk::FDD_INDEX_PULSE_DURATION_MS;
using unrealdisk::FDD_INDEX_PULSE_DURATION_T_STATES;
using unrealdisk::FDD_INTERFACE_SPEED_MFM_KBPS;
using unrealdisk::FDD_INTERFACE_SPEED_FM_KBPS;
using unrealdisk::MAX_TRACK_LEN;
using unrealdisk::NOMINAL_TRACK_LEN_FM;
using unrealdisk::MAX_TRACK_DATA_LEN;
using unrealdisk::MAX_CYLINDERS;
using unrealdisk::MAX_SIDES;
using unrealdisk::MAX_PHYSICAL_CYLINDER;
using unrealdisk::MIN_SECTORS;
using unrealdisk::MAX_SECTORS;
using unrealdisk::MAX_DRQ_SERVICE_TIME_MFM_US;
using unrealdisk::MAX_DRQ_SERVICE_TIME_MFM_T_STATES;
using unrealdisk::MAX_DRQ_SERVICE_TIME_FM_US;
using unrealdisk::MAX_DRQ_SERVICE_TIME_FM_T_STATES;
using unrealdisk::SAFE_DRQ_SERVICE_TIME_MFM_US;
using unrealdisk::SAFE_DRQ_SERVICE_TIME_FM_US;
using unrealdisk::FdcClock;
using unrealdisk::FdcDataRate;
using unrealdisk::FdcClockPolicy;
using unrealdisk::CRCHelper;
