#pragma once
#include "stdafx.h"

#include "emulator/video/zx/screenzx.h"

/// The TSConf renderer (PLAN #41 phase 3, TSConf technical-design §3.9): the
/// Screen subclass VideoController::CreateScreen picks for MM_TSL (PLAN
/// #60(e)). Until phase 3 it is the ZX renderer unchanged - TS-Conf's ZX mode
/// is a ZX screen - so the selection path is in place and phase 3 only adds
/// the TS modes (16C, 256C, TXT, TSU layers, the 720x288 geometry) here.
///
/// The former skeleton of this class (a port of the ancestor's per-line
/// renderer reading the shared `state.ts`) was never instantiated and is
/// removed; phase 3 builds from the design and the ancestor's `tsconf.cpp`.
class ScreenTSConf : public ScreenZX
{
public:
    ScreenTSConf() = delete;
    explicit ScreenTSConf(EmulatorContext* context) : ScreenZX(context) {}
    ~ScreenTSConf() override = default;
};
