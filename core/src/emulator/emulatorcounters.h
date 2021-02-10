#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "emulator/platform.h"

class EmulatorContext;

class EmulatorCounters
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
    const uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_COUNTERS;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Constructors / Destructors>
public:
    EmulatorCounters(EmulatorContext* context);
    virtual ~EmulatorCounters();
    /// endregion </Constructors / Destructors>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    /// endregion </Fields>

    /// region <Frame counters>
protected:
    // TODO: add meaningful counters

public:
    void resetFrameCounters();
    /// endregion </Frame counters>
};

