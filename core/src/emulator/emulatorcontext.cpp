#include "stdafx.h"

#include "emulatorcontext.h"

#include "common/modulelogger.h"

/// region <Constructors / destructors>

/// Default constructor with LogTrace default logging level
EmulatorContext::EmulatorContext() : EmulatorContext(LoggerLevel::LogTrace)
{
}

/// Constructor allowing to specify default logging level
EmulatorContext::EmulatorContext(LoggerLevel level)
{
    // Create advanced logging
    ModuleLogger* moduleLogger = new ModuleLogger(this);
    if (moduleLogger)
    {
        moduleLogger->SetLoggingLevel(level);

        moduleLogger->LogMessage(LoggerLevel::LogDebug, PlatformModulesEnum::MODULE_CORE, PlatformCoreSubmodulesEnum::SUBMODULE_CORE_CONFIG, "Emulator - ModuleLogger initialized");

        pModuleLogger = moduleLogger;
    }
    else
    {
        throw std::runtime_error("EmulatorContext::EmulatorContext() - Unable to initialize ModuleLogger");
    }
}

EmulatorContext::~EmulatorContext()
{
    if (pModuleLogger != nullptr)
    {
        delete pModuleLogger;
    }
}
/// endregion </Constructors / destructors>


