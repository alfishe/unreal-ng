#include "stdafx.h"
#include "modulelogger.h"

#include "common/bithelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"

/// region <Constants>

const char* ModuleLogger::LoggerLevelNames[6] =
{
    "None",
    "Trace",
    "Debug",
    "Info",
    "Warning",
    "Error"
};

const char* ModuleLogger::ALL = "<All>";
const char* ModuleLogger::NONE = "<None>";

const char* ModuleLogger::moduleNames[12] =
{
    "<Unknown>",
    "Core",
    "Z80",
    "Memory",
    "I/O",
    "Disk",
    "Video",
    "Sound",
    "DMA",
    "Loader",
    "Debugger",
    "Disassembler"
};

/// endregion </Constants>

/// region <Constructors / Destructors>

ModuleLogger::ModuleLogger(EmulatorContext* context)
{
    _context = context;

    // Ensure all modules / submodules are enabled by default
    _settings = LoggerSettings();
    _settings.modules = 0xFFFFFFFF;
    for (size_t i = 0; i < sizeof(_settings.submodules) / sizeof(_settings.submodules[0]); ++i)
    {
        _settings.submodules[i] = 0xFFFF;
    }

    //_settings.modules &= ~PlatformModulesEnum::MODULE_IO;
    //_settings.modules &= ~PlatformModulesEnum::MODULE_Z80;
    //_settings.submodules[PlatformModulesEnum::MODULE_IO] &= ~PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;

    // Ensure auto-flushing for default streams
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

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
    // Mark logger as shutting down to prevent use-after-free in logging calls from destructors
    _shutdown = true;

    // Unsubscribe from logger settings change notifications
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callbackOnKeyPressed = static_cast<ObserverCallbackMethod>(&ModuleLogger::OnSettingsChangeRequested);
    messageCenter.RemoveObserver(NC_LOGGER_SETTINGS_MODULES_CHANGE, observerInstance, callbackOnKeyPressed);

    ObserverCallbackMethod moduleSettingsChange = static_cast<ObserverCallbackMethod>(&ModuleLogger::OnModuleSettingsChangeRequested);
    messageCenter.RemoveObserver(NC_LOGGER_SETTINGS_SUBMODULES_CHANGE, observerInstance, moduleSettingsChange);
}

/// endregion </Constructors / Destructors>

/// region <Configuration methods>

void ModuleLogger::SetLoggingSettings(LoggerSettings& settings)
{
    // Copy new settings to the context
    memcpy((void *)&_settings, (const void *)&settings, sizeof(LoggerSettings));
}

void ModuleLogger::Mute()
{
    _mute = true;
}

void ModuleLogger::Unmute()
{
    _mute = false;
}

/// Disable log outputs for all modules and theis submodules
void ModuleLogger::TurnOffLoggingForAll()
{
    // Disable all modules
    _settings.modules = 0x0000'0000;

    size_t size = sizeof(_settings.submodules) / sizeof(_settings.submodules[0]);
    for (size_t i = 0; i < size; i++)
    {
        _settings.submodules[i] = 0x0000; // Disable all submodule bits
    }
}

/// Enable log outputs for all modules and theis submodules
void ModuleLogger::TurnOnLoggingForAll()
{
    // Enable all modules
    _settings.modules = 0xFFFF'FFFF;

    size_t size = sizeof(_settings.submodules) / sizeof(_settings.submodules[0]);
    for (size_t i = 0; i < size; i++)
    {
        _settings.submodules[i] = 0xFF; // Enable all submodule bits
    }
}

void ModuleLogger::TurnOffLoggingForModule(PlatformModulesEnum module, uint16_t submodule)
{
    /// region <Sanity checks>
    if (module > PlatformModulesEnum::MODULE_DISASSEMBLER)
    {
        std::string message = StringHelper::Format("Module cannot have id > %d. Found %d", PlatformModulesEnum::MODULE_DISASSEMBLER, module);
        throw std::logic_error(message);
    }

    if (submodule == 0x00)
    {
        std::string message = StringHelper::Format("No sense to pass NONE module");
        throw std::logic_error(message);
    }

    if (submodule != 0xFF && BitHelper::CountSetBits(submodule) > 1)
    {
        std::string message = StringHelper::Format("Submodule specified incorrectly. Single bit should be provided. Value: %x", submodule);
        throw std::logic_error(message);
    }

    /// endregion </Sanity checks>

    if (submodule == 0xFF)
    {
        // Disable the whole module
        _settings.modules &= ~(1 << module);
        _settings.submodules[module] = 0x00;
    }
    else
    {
        _settings.submodules[module] &= ~submodule;
    }
}

void ModuleLogger::TurnOnLoggingForModule(PlatformModulesEnum module, uint16_t submodule)
{
    /// region <Sanity checks>
    if (module > PlatformModulesEnum::MODULE_DISASSEMBLER)
    {
        std::string message = StringHelper::Format("Module cannot have id > %d. Found %d", PlatformModulesEnum::MODULE_DISASSEMBLER, module);
        throw std::logic_error(message);
    }

    if (submodule == 0x0000)
    {
        std::string message = StringHelper::Format("No sense to pass NONE module");
        throw std::logic_error(message);
    }

    if (submodule != 0xFFFF && BitHelper::CountSetBits(submodule) > 1)
    {
        std::string message = StringHelper::Format("Submodule specified incorrectly. Single bit should be provided. Value: %x", submodule);
        throw std::logic_error(message);
    }

    /// endregion </Sanity checks>

    // Enable the whole module
    _settings.modules |= module;

    if (submodule == 0xFF)
    {
        _settings.submodules[module] = 0xFF;
    }
    else
    {
        _settings.submodules[module] |= submodule;
    }
}

void ModuleLogger::SetLoggingLevel(LoggerLevel level)
{
    _level = level;
}

void ModuleLogger::SetLoggerOut(ModuleLoggerOutCallback callback)
{
    if (callback != nullptr)
    {
        _outCallback = callback;
    }
}

void ModuleLogger::SetLoggerOut(ModuleLoggerObserver* instance, ModuleObserverObserverCallbackMethod callback)
{
    if (instance && callback)
    {
        _observerInstance = instance;
        _callbackMethod = callback;
    }
}

void ModuleLogger::ResetLoggerOut()
{
    _outCallback = nullptr;
    _observerInstance = nullptr;
    _callbackMethod = nullptr;
}

/// endregion </Configuration methods>

/// region <Methods>

void ModuleLogger::EmptyLine()
{
    static const char linefeed[] = "\n";
    static size_t len = strlen(linefeed);

    Out(linefeed, len);
}

void ModuleLogger::LogMessage(LoggerLevel level, PlatformModulesEnum module, uint16_t submodule, const std::string fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    LogMessage(level, module, submodule, fmt.c_str(), args);

    va_end(args);
}

void ModuleLogger::LogMessage(LoggerLevel level, PlatformModulesEnum module, uint16_t submodule, const char* fmt, ...)
{
    // Skip messages with level below allowed (message has more details than we want)
    if (level < _level)
        return;

    // Don't log message if we configured logger not to
    if (!IsLoggingEnabledForLogLevel(module, submodule, level))
        return;

    /// region <Sanity checks>
#ifdef _DEBUG
    if (module > MODULE_DISASSEMBLER)
    {
        std::string error = StringHelper::Format("LogModuleMessage - invalid module %d", module);
        throw std::logic_error(error);
    }

    switch (module)
    {
        case MODULE_CORE:
            if (submodule > SUBMODULE_CORE_MAINLOOP)
            {
                std::string error = StringHelper::Format("LogModuleMessage - module Core, invalid submodule %d", submodule);
                throw std::logic_error(error);
            }
            break;
        case MODULE_Z80:
            if (submodule > SUBMODULE_Z80_IO)
            {
                std::string error = StringHelper::Format("LogModuleMessage - module Z80, invalid submodule %d", submodule);
                throw std::logic_error(error);
            }
            break;
        case MODULE_MEMORY:
            if (submodule > SUBMODULE_MEM_RAM)
            {
                std::string error = StringHelper::Format("LogModuleMessage - module Memory, invalid submodule %d", submodule);
                throw std::logic_error(error);
            }
            break;
        default:
            break;
    }
#endif // _DEBUG

    /// endregion </Sanity checks>

    char buffer[1024];
    size_t time_len = 0;
    struct tm *tm_info;
    struct timeval tv;

    std::string format = std::string(LoggerLevelNames[level]) + ": " + fmt;
    va_list args;
    va_start(args, fmt);

    /// region <Print timestamp>

    gettimeofday(&tv, NULL);

    #if defined __GNUC__
        time_t rawtime;
        time(&rawtime);
        tm_info = localtime(&rawtime);
    #elif defined __APPLE__
        tm_info = localtime(&tv);
    #endif

    if (tm_info == nullptr)
    {
        throw std::logic_error("Unable to get localtime");
    }

    time_len += strftime(buffer, sizeof(buffer), "[%H:%M:%S", tm_info);

    #if defined _WIN32 && defined MSVC
        time_len += snprintf(buffer + time_len, sizeof(buffer) - time_len,".%03lld.%03lld] ", tv.tv_usec / 1000, tv.tv_usec % 1000);
    #else
        time_len += snprintf(buffer + time_len, sizeof(buffer) - time_len,".%03d.%03d]", (int)(tv.tv_usec / 1000), (int)(tv.tv_usec % 1000));
    #endif

    /// endregion </Print timestamp>

    /// region <Print module-submodule information>
    //time_len += snprintf(buffer + time_len, sizeof(buffer) - time_len, "[%s] ", GetModuleSubmoduleHexString(module, submodule).c_str());
    time_len += snprintf(buffer + time_len, sizeof(buffer) - time_len, "[%s] ", GetModuleSubmoduleBriefString(module, submodule).c_str());
    /// endregion </Print module-submodule information>

    /// region <Print formatted value>

    int msg_len = vsnprintf(buffer + time_len, sizeof(buffer) - time_len, format.c_str(), args);

    OutLine(buffer, time_len + msg_len);

    /// endregion </Print formatted value>

    va_end(args);
}

void ModuleLogger::OutLine(const char* buffer, size_t len)
{
    if (_observerInstance && _callbackMethod)
    {
        (_observerInstance->*_callbackMethod)(buffer, len);
    }
    else if (_outCallback)
    {
        // Externally provided callback
        _outCallback(buffer, len);
    }
    else
    {
        // Default out
        std::cout << buffer << std::endl;
    }
}

void ModuleLogger::Out(const char* buffer, size_t len)
{
    if (_observerInstance && _callbackMethod)
    {
        (_observerInstance->*_callbackMethod)(buffer, len);
    }
    else if (_outCallback)
    {
        // Externally provided callback
        _outCallback(buffer, len);
    }
    else
    {
        // Default out
        std::cout << buffer;
    }
}

void ModuleLogger::Flush()
{
    fflush(stdout);
    fflush(stderr);
}

/// endregion </Methods>

/// region <Helper methods>

bool ModuleLogger::IsLoggingEnabled(PlatformModulesEnum module, uint16_t submodule)
{
    try
    {
        bool result = false;

        // Skip all checks if muted or shutting down
        if (_mute || _shutdown)
            return result;

        uint8_t moduleBitNumber = BitHelper::GetFirstSetBitPosition(static_cast<uint8_t>(module));

        // Check if logging for the whole module allowed
        if (BitHelper::IsBitSet(_settings.modules, moduleBitNumber))
        {
            uint8_t submoduleBitNumber = BitHelper::GetFirstSetBitPosition(submodule);

            // Check settings on submodule level
            if (BitHelper::IsBitSet(_settings.submodules[module], submoduleBitNumber))
            {
                result = true;
            }
        }

        return result;
    }
    catch (...)
    {
        // If anything goes wrong (corrupted object, invalid memory access, etc.),
        // assume logging is disabled to prevent crashes
        return false;
    }
}

bool ModuleLogger::IsLoggingEnabledForLogLevel(PlatformModulesEnum module, uint16_t submodule, LoggerLevel level)
{
    try
    {
        // Check of logging for the module / submodule allowed at all
        bool result = IsLoggingEnabled(module, submodule);

        // Check if logger level is appropriate
        result &= level >= _level;

        return result;
    }
    catch (...)
    {
        // If anything goes wrong (corrupted object, invalid memory access, etc.),
        // assume logging is disabled to prevent crashes
        return false;
    }
}

const char* ModuleLogger::GetSubmoduleName(PlatformModulesEnum module, uint16_t submodule)
{
    const char* result = "<UNRESOLVED>";

    // Convert bitmask to index if needed
    uint16_t submoduleIndex = submodule;
    if (submodule > 0 && submodule < 0xFFFF)
    {
        submoduleIndex = BitHelper::GetFirstSetBitPosition(submodule);
    }

    const char** submoduleNames;
    size_t len;
    if (GetSubmoduleNameCollection(module, &submoduleNames, &len))
    {
        if (submoduleIndex < len)
        {
            result = submoduleNames[submoduleIndex];
        }
    }

    return result;
}

std::string ModuleLogger::GetModuleSubmoduleBriefString(PlatformModulesEnum module, uint16_t submodule)
{
    const char* moduleName = moduleNames[module];
    const char* submoduleName = GetSubmoduleName(module, submodule);

    std::string result = StringHelper::Format("%s_%s", moduleName, submoduleName);

    return result;
}

std::string ModuleLogger::GetModuleSubmoduleHexString(PlatformModulesEnum module, uint16_t submodule)
{
    std::string result = StringHelper::Format("%04d_%04d", module, submodule);

    return result;
}

/// endregion </Helper methods>

/// region <Handle MessageCenter settings events>

/// Full logging settings change requested
void ModuleLogger::OnSettingsChangeRequested([[maybe_unused]] int id, Message* message)
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
void ModuleLogger::OnModuleSettingsChangeRequested([[maybe_unused]] int id, Message* message)
{
    if (message && message->obj)
    {
        SimpleNumberPayload* settings = dynamic_cast<SimpleNumberPayload*>(message->obj);

        if (settings)
        {
            uint32_t value = settings->_payloadNumber;
            uint16_t module = value >> 16;
            uint16_t moduleSettings = value & 0x0000FFFF;

            if (module < sizeof(_settings.submodules) / sizeof (_settings.submodules[0]))
            {
                _settings.submodules[module] = moduleSettings;
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
        for (unsigned i = 0; i < sizeof(moduleNames) / sizeof(moduleNames[0]); i++)
        {
            if (moduleFlags & (1 << i))
            {
                if (!isFirstModule)
                    ss << ", ";
                else
                    isFirstModule = false;

                ss << DumpModuleName(i).c_str();
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
     if (GetSubmoduleNameCollection(module, &submoduleNames, &submoduleNamesSize) && submoduleNames && submoduleNamesSize > 0)
     {
         ss << StringHelper::Format("Submodules: %s", DumpResolveFlags(moduleSettings, submoduleNames, submoduleNamesSize).c_str());
     }

     result = ss.str();

    return result;
}

bool ModuleLogger::GetSubmoduleNameCollection(uint16_t module, const char*** submoduleNames, size_t* submoduleNamesSize)
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
        case MODULE_LOADER:
            *submoduleNames = submoduleLoaderNames;
            *submoduleNamesSize = sizeof(submoduleLoaderNames) / sizeof(submoduleLoaderNames[0]);
            break;
        case MODULE_DEBUGGER:
            *submoduleNames = submoduleDebuggerNames;
            *submoduleNamesSize = sizeof(submoduleDebuggerNames) / sizeof(submoduleDebuggerNames[0]);
            break;
        case MODULE_DISASSEMBLER:
            *submoduleNames = submoduleDisassemblerNames;
            *submoduleNamesSize = sizeof(submoduleDisassemblerNames) / sizeof(submoduleDisassemblerNames[0]);
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

    ss << "Module logger settings dump:" << std::endl;

    size_t moduleCount = sizeof(moduleNames) / sizeof(moduleNames[0]);
    for (size_t i = 1; i < moduleCount; ++i)
    {
        std::string moduleName = DumpModuleName(i);
        std::string moduleStatus = "off";
        bool hasEnabledSubmodules = false;
        bool allSubmodulesEnabled = true;

        // Check submodule status
        const char** submoduleNames;
        size_t submoduleNamesSize = 0;
        if (GetSubmoduleNameCollection(i, &submoduleNames, &submoduleNamesSize) && submoduleNames && submoduleNamesSize > 0)
        {
            uint16_t allSubmodulesMask = (1 << submoduleNamesSize) - 1;
            uint16_t enabledSubmodules = _settings.submodules[i];
            
            if (enabledSubmodules != 0)
            {
                hasEnabledSubmodules = true;
                if (enabledSubmodules != allSubmodulesMask)
                {
                    allSubmodulesEnabled = false;
                }
            }
        }

        // Determine module status
        if (_settings.modules & (1 << i))
        {
            if (allSubmodulesEnabled)
            {
                moduleStatus = "on";
            }
            else if (hasEnabledSubmodules)
            {
                moduleStatus = "partial";
            }
        }

        ss << StringHelper::Format("%s: %s", moduleName.c_str(), moduleStatus.c_str()) << std::endl;

        // Only show submodule details if module is partial or off but has enabled submodules
        if ((moduleStatus == "partial" || (moduleStatus == "off" && hasEnabledSubmodules)) && 
            GetSubmoduleNameCollection(i, &submoduleNames, &submoduleNamesSize) && 
            submoduleNames && submoduleNamesSize > 0)
        {
            ss << "  Submodules:" << std::endl;
            ss << "" << DumpResolveFlags(_settings.submodules[i], submoduleNames, submoduleNamesSize) << std::endl;
        }
    }

    result = ss.str();

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
        for (size_t i = 0; i < nameSize; ++i)
        {
            std::string submoduleName = names[i];
            std::string submoduleStatus = "off";

            uint16_t mask = 1 << i;
            if (flags & mask)
            {
                submoduleStatus = "on";
            }
            else
            {
                submoduleStatus = "off";
            }

            ss << StringHelper::Format("  %s: %s", submoduleName.c_str(), submoduleStatus.c_str());
            if (i < nameSize - 1)  // Only add newline if not the last line
                ss << std::endl;
        }
    }

    result = ss.str();

    return result;
}
/// endregion </Debug methods>
