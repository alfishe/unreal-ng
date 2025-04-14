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
    // Ensure config area is clean
    this->config = CONFIG{};

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

/// Constructor registering reference to parent Emulator object
EmulatorContext::EmulatorContext(Emulator* emulator, LoggerLevel level) : EmulatorContext(level)
{
    pEmulator = emulator;
}

EmulatorContext::~EmulatorContext()
{
    if (pModuleLogger != nullptr)
    {
        delete pModuleLogger;
    }
}
/// endregion </Constructors / destructors>


