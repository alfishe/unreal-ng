#pragma once
#include "stdafx.h"

#include "emulator/platform.h"

class ModuleLogger;
class EmulatorContext;

class LabelManager
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_MEMORY;
    const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_GENERIC;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LabelManager(EmulatorContext* context);
    virtual ~LabelManager();
    /// endregion </Constructors / destructors>

    /// region <Methods>
    bool LoadLabels(std::string& path);

    std::string GetLabelByZ80Address(uint16_t addr);
    std::string GetLabelByPhysicalAddress(uint8_t address);
    /// endregion </Methods>
};
