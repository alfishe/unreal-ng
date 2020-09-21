#include "stdafx.h"
#include "modulelogger.h"

#include "common/bithelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / Destructors>

ModuleLogger::ModuleLogger(EmulatorContext* context)
{
    _context = context;

    // Subscribe for logger settings change notifications
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callbackOnSettingsChange = static_cast<ObserverCallbackMethod>(&ModuleLogger::OnSettingsChangeRequested);
    messageCenter.AddObserver(NC_LOGGER_SETTINGS_MODULES_CHANGE, observerInstance, callbackOnSettingsChange);

    ObserverCallbackMethod moduleSettingsChange = static_cast<ObserverCallbackMethod>(&ModuleLogger::OnModuleSettingsChangeRequested);
    messageCenter.AddObserver(NC_LOGGER_SETTINGS_SUBMODULES_CHANGE, observerInstance, moduleSettingsChange);
}

ModuleLogger::~ModuleLogger()
{
    // Unsubscribe from logger settings change notifications
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callbackOnKeyPressed = static_cast<ObserverCallbackMethod>(&ModuleLogger::OnSettingsChangeRequested);
    messageCenter.RemoveObserver(NC_LOGGER_SETTINGS_MODULES_CHANGE, observerInstance, callbackOnKeyPressed);

    ObserverCallbackMethod moduleSettingsChange = static_cast<ObserverCallbackMethod>(&ModuleLogger::OnModuleSettingsChangeRequested);
    messageCenter.RemoveObserver(NC_LOGGER_SETTINGS_SUBMODULES_CHANGE, observerInstance, moduleSettingsChange);
}

/// endregion </Constructors / Destructors>

/// region <Methods>

void ModuleLogger::SetLoggingSettings(LoggerSettings& settings)
{
    // Copy new settings to the context
    memcpy((void *)&_context->loggerSettings, (const void *)&settings, sizeof (LoggerSettings));
}

/// endregion </Methods>

/// region <Handle MessageCenter settings events>

/// Full logging settings change requested
void ModuleLogger::OnSettingsChangeRequested(int id, Message* message)
{
    if (message && message->obj)
    {
        LoggerSettings* settings = dynamic_cast<LoggerSettings*>(message->obj);

        if (settings)
        {
            SetLoggingSettings(*settings);
        }
    }
}

/// Single module logging settings change requested
void ModuleLogger::OnModuleSettingsChangeRequested(int id, Message* message)
{
    if (message && message->obj)
    {
        SimpleNumberPayload* settings = dynamic_cast<SimpleNumberPayload*>(message->obj);

        if (settings)
        {
            uint32_t value = settings->_payloadNumber;
            uint16_t module = value >> 16;
            uint16_t moduleSettings = value & 0x0000FFFF;

            LoggerSettings& loggerSettings = _context->loggerSettings;

            if (module < sizeof(loggerSettings.submodules) / sizeof (loggerSettings.submodules[0]))
            {
                loggerSettings.submodules[module] = moduleSettings;
            }
        }
    }
}

/// endregion </Handle MessageCenter settings events>

/// region <Debug methods>

std::string ModuleLogger::DumpModules(uint32_t moduleFlags)
{
    std::string result;
    std::stringstream ss;

    if (moduleFlags == 0xFFFFFFFF)
    {
        ss << ALL;
    }
    else if (moduleFlags == 0x00000000)
    {
        ss << NONE;
    }
    else
    {
        bool isFirstModule = true;
        for (int i = 0; i < sizeof(moduleNames) / sizeof(moduleNames[0]); i++)
        {
            if (moduleFlags & (1 << i))
            {
                if (!isFirstModule)
                    ss << ", ";

                ss << DumpModuleName(i).c_str();
                isFirstModule = false;
            }
        }
    }

    result = ss.str();

    return result;
}

std::string ModuleLogger::DumpRequestedSettingsChange(uint32_t change)
{
    std::string result;
    std::stringstream ss;

    uint16_t module = change >> 16;
    uint16_t moduleSettings = change & 0x0000FFFF;

     ss << StringHelper::Format("Module: %s (%d)", moduleNames[module], module) << std::endl;

     const char** submoduleNames;
     size_t submoduleNamesSize = 0;
     if (DumpResolveSubmodule(module, &submoduleNames, &submoduleNamesSize) && submoduleNames && submoduleNamesSize > 0)
     {
         ss << StringHelper::Format("Submodules: %s", DumpResolveFlags(moduleSettings, submoduleNames, submoduleNamesSize).c_str());
     }

    return result;
}

bool ModuleLogger::DumpResolveSubmodule(uint16_t module, const char*** submoduleNames, size_t* submoduleNamesSize)
{
    bool result = true;

    if (module > sizeof(moduleNames) / sizeof(moduleNames[0]))
    {
        result = false;

        return result;
    }

    switch (module)
    {
        case MODULE_CORE:
            *submoduleNames = submoduleCoreNames;
            *submoduleNamesSize = sizeof(submoduleCoreNames) / sizeof(submoduleCoreNames[0]);
            break;
        case MODULE_Z80:
            *submoduleNames = submoduleZ80Names;
            *submoduleNamesSize = sizeof(submoduleZ80Names) / sizeof(submoduleZ80Names[0]);
            break;
        case MODULE_MEMORY:
            *submoduleNames = submoduleMemoryNames;
            *submoduleNamesSize = sizeof(submoduleMemoryNames) / sizeof(submoduleMemoryNames[0]);
            break;
        case MODULE_IO:
            *submoduleNames = submoduleIONames;
            *submoduleNamesSize = sizeof(submoduleIONames) / sizeof(submoduleIONames[0]);
            break;
        case MODULE_DISK:
            *submoduleNames = submoduleDiskNames;
            *submoduleNamesSize = sizeof(submoduleDiskNames) / sizeof(submoduleDiskNames[0]);
            break;
        case MODULE_VIDEO:
            *submoduleNames = submoduleVideoNames;
            *submoduleNamesSize = sizeof(submoduleVideoNames) / sizeof(submoduleVideoNames[0]);
            break;
        case MODULE_SOUND:
            *submoduleNames = submoduleSoundNames;
            *submoduleNamesSize = sizeof(submoduleSoundNames) / sizeof(submoduleSoundNames[0]);
            break;
        case MODULE_DMA:
            *submoduleNames = submoduleDMANames;
            *submoduleNamesSize = sizeof(submoduleDMANames) / sizeof(submoduleDMANames[0]);
            break;
        case MODULE_DEBUGGER:
            *submoduleNames = submoduleDebuggerNames;
            *submoduleNamesSize = sizeof(submoduleDebuggerNames) / sizeof(submoduleDebuggerNames[0]);
            break;
        default:
            result = false;
            break;
    };

    return result;
}

std::string ModuleLogger::DumpSettings()
{
    std::string result;
    std::stringstream ss;

    LoggerSettings& loggerSettings = _context->loggerSettings;

    for (int i = 0; i < sizeof(moduleNames) / sizeof(moduleNames[0]); i++)
    {
        if (loggerSettings.modules & 1 << i)
        {
            ss << DumpModuleName(i);

            const char** submoduleNames;
            size_t submoduleNamesSize = 0;
            if (DumpResolveSubmodule(i, &submoduleNames, &submoduleNamesSize) && submoduleNames && submoduleNamesSize > 0)
            {
                ss << StringHelper::Format("Submodules: %s", DumpResolveFlags(loggerSettings.submodules[i], submoduleNames, submoduleNamesSize).c_str());
            }
        }
    }

    return result;
}

std::string ModuleLogger::DumpModuleName(uint16_t module)
{
    std::string result = moduleNames[0];

    if (module < sizeof(moduleNames) / sizeof(moduleNames[0]))
    {
        result = moduleNames[module];
    }

    return result;
}

std::string ModuleLogger::DumpResolveFlags(uint16_t flags, const char* names[], size_t nameSize)
{
    std::string result;
    std::stringstream  ss;

    if (flags == 0)
    {
        ss << NONE;
    }
    else if (flags == 0xFFFF)
    {
        ss << ALL;
    }
    else
    {
        bool isFirst = true;

        for (int i = 0; i < nameSize; i++)
        {
            uint16_t mask = 1 << i;
            if (flags & mask)
            {
                if (!isFirst)
                    ss << " | ";
                else
                    isFirst = false;

                ss << names[i];
            }
        }
    }

    result = ss.str();

    return result;
}
/// endregion </Debug methods>