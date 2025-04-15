#pragma once
#include "stdafx.h"

#include "common/logger.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/platform.h"
#include <cstring>

// _ALWAYS_USE_LOGGING always override the behavior
#if !defined(_CODE_UNDER_TEST) || defined(_ALWAYS_USE_LOGGING)

// Write all Debug / Info logs if not under unit testing / benchmarking
#define MLOGDEBUG(format, ...) _logger->Debug(_MODULE, _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGINFO(format, ...) _logger->Info(_MODULE, _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGWARNING(format, ...) _logger->Warning(_MODULE, _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGERROR(format, ...) _logger->Error(_MODULE, _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGEMPTY(...) _logger->EmptyLine(##__VA_ARGS__)

// Macros with custom submodule
#define MLOGDEBUG_SUBMODULE(submodule, format, ...) _logger->Debug(_MODULE, submodule, format, ##__VA_ARGS__)
#define MLOGINFO_SUBMODULE(submodule, format, ...) _logger->Info(_MODULE, submodule, format, ##__VA_ARGS__)
#define MLOGWARNING_SUBMODULE(submodule, format, ...) _logger->Warning(_MODULE, submodule, format, ##__VA_ARGS__)
#define MLOGERROR_SUBMODULE(submodule, format, ...) _logger->Error(_MODULE, submodule, format, ##__VA_ARGS__)
#define MLOGEMPTY_SUBMODULE(submodule, ...) _logger->EmptyLine(submodule, ##__VA_ARGS__)

// Backwards compatibility macros
#define MLOGDEBUG_OVERRIDE_(submodule, ...) MLOGDEBUG_SUBMODULE(submodule, __VA_ARGS__)
#define MLOGINFO_OVERRIDE_(submodule, ...) MLOGINFO_SUBMODULE(submodule, __VA_ARGS__)
#define MLOGWARNING_OVERRIDE_(submodule, ...) MLOGWARNING_SUBMODULE(submodule, __VA_ARGS__)
#define MLOGERROR_OVERRIDE_(submodule, ...) MLOGERROR_SUBMODULE(submodule, __VA_ARGS__)
#define MLOGEMPTY_OVERRIDE_(submodule, ...) MLOGEMPTY_SUBMODULE(submodule, __VA_ARGS__)

// Macros with optional submodule override (uses _SUBMODULE if not specified)
#define MLOGDEBUG_OPT(submodule, format, ...) _logger->Debug(_MODULE, submodule ? submodule : _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGINFO_OPT(submodule, format, ...) _logger->Info(_MODULE, submodule ? submodule : _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGWARNING_OPT(submodule, format, ...) _logger->Warning(_MODULE, submodule ? submodule : _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGERROR_OPT(submodule, format, ...) _logger->Error(_MODULE, submodule ? submodule : _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGEMPTY_OPT(submodule, ...) _logger->EmptyLine(submodule ? submodule : _SUBMODULE, ##__VA_ARGS__)

// Helper macro to make submodule optional
#define MLOGDEBUG_(...) MLOGDEBUG_OPT(nullptr, __VA_ARGS__)
#define MLOGINFO_(...) MLOGINFO_OPT(nullptr, __VA_ARGS__)
#define MLOGWARNING_(...) MLOGWARNING_OPT(nullptr, __VA_ARGS__)
#define MLOGERROR_(...) MLOGERROR_OPT(nullptr, __VA_ARGS__)
#define MLOGEMPTY_(...) MLOGEMPTY_OPT(nullptr, __VA_ARGS__)

#else

// No Debug / Info for unit tests and benchmarks
#define MLOGDEBUG(format, ...)
#define MLOGINFO(format, ...)
#define MLOGWARNING(format, ...) _logger->Warning(_MODULE, _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGERROR(format, ...) _logger->Error(_MODULE, _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGEMPTY(...) _logger->EmptyLine(##__VA_ARGS__)

// Macros with custom submodule
#define MLOGDEBUG_SUBMODULE(submodule, format, ...) _logger->Debug(_MODULE, submodule, format, ##__VA_ARGS__)
#define MLOGINFO_SUBMODULE(submodule, format, ...) _logger->Info(_MODULE, submodule, format, ##__VA_ARGS__)
#define MLOGWARNING_SUBMODULE(submodule, format, ...) _logger->Warning(_MODULE, submodule, format, ##__VA_ARGS__)
#define MLOGERROR_SUBMODULE(submodule, format, ...) _logger->Error(_MODULE, submodule, format, ##__VA_ARGS__)
#define MLOGEMPTY_SUBMODULE(submodule, ...) _logger->EmptyLine(submodule, ##__VA_ARGS__)

// Backwards compatibility macros
#define MLOGDEBUG_OVERRIDE_(submodule, ...) MLOGDEBUG_SUBMODULE(submodule, __VA_ARGS__)
#define MLOGINFO_OVERRIDE_(submodule, ...) MLOGINFO_SUBMODULE(submodule, __VA_ARGS__)
#define MLOGWARNING_OVERRIDE_(submodule, ...) MLOGWARNING_SUBMODULE(submodule, __VA_ARGS__)
#define MLOGERROR_OVERRIDE_(submodule, ...) MLOGERROR_SUBMODULE(submodule, __VA_ARGS__)
#define MLOGEMPTY_OVERRIDE_(submodule, ...) MLOGEMPTY_SUBMODULE(submodule, __VA_ARGS__)

// Macros with optional submodule override (uses _SUBMODULE if not specified)
#define MLOGDEBUG_OPT(submodule, format, ...) _logger->Debug(_MODULE, submodule ? submodule : _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGINFO_OPT(submodule, format, ...) _logger->Info(_MODULE, submodule ? submodule : _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGWARNING_OPT(submodule, format, ...) _logger->Warning(_MODULE, submodule ? submodule : _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGERROR_OPT(submodule, format, ...) _logger->Error(_MODULE, submodule ? submodule : _SUBMODULE, format, ##__VA_ARGS__)
#define MLOGEMPTY_OPT(submodule, ...) _logger->EmptyLine(submodule ? submodule : _SUBMODULE, ##__VA_ARGS__)

// Helper macro to make submodule optional
#define MLOGDEBUG_(...) MLOGDEBUG_OPT(nullptr, __VA_ARGS__)
#define MLOGINFO_(...) MLOGINFO_OPT(nullptr, __VA_ARGS__)
#define MLOGWARNING_(...) MLOGWARNING_OPT(nullptr, __VA_ARGS__)
#define MLOGERROR_(...) MLOGERROR_OPT(nullptr, __VA_ARGS__)
#define MLOGEMPTY_(...) MLOGEMPTY_OPT(nullptr, __VA_ARGS__)

#endif

/// region <Structures>

/// All levels of logging
/// Trace - most detailed information, error - least
/// Examples of use:
///     LogError level set - only error messages will be accepted
//      LogDebug level set - LogDebug, LogInfo, LogWarning, LogError messages will be accepted. But not LogTrace.
enum LoggerLevel : uint8_t
{
    LogUnknown = 0,     // Uninitialized state
    LogTrace = 1,
    LogDebug = 2,
    LogInfo = 3,
    LogWarning = 4,
    LogError = 5,
    LogNone = 6
};

struct LoggerSettings
{
    uint32_t modules;                       // Per module on/off flags

    union
    {
        // Indexed access array
        // Allow all modules logging by default
        uint16_t submodules[12] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };

        struct
        {
            uint16_t unknownSubmodules;     // When module is unspecified - just a stub
            uint16_t coreSubmodules;        // Core submodules on/off flags
            uint16_t z80Submodules;         // Z80 submodules on/off flags
            uint16_t memSubmodules;         // Memory submodules on/off flags
            uint16_t ioSubmodules;          // I/O submodules on/off flags
            uint16_t diskSubmodules;        // Disk submodules on/off flags
            uint16_t soundSubmodules;       // Sound submodules on/off flags
            uint16_t videoSubmodules;       // Video submodules on/off flags
            uint16_t dmaSubmodules;         // DMA submodules on/off flags
            uint16_t loaderSubmodules;      // Loader submodules on/off flags
            uint16_t debuggerSubmodules;    // Memory submodules on/off flags
            uint16_t disassemblerSubmodules;// Disassembler submodules on/off flags
        };
    };
};

class LoggerSettingsModulePayload : public MessagePayload
{
public:
    LoggerSettings _settings;

public:
    LoggerSettingsModulePayload(LoggerSettings& settings)
    {
        // Copy structure content
        memcpy((void *)&_settings, (const void *)&settings, sizeof(LoggerSettings));
    }
    virtual ~LoggerSettingsModulePayload() = default;
};

// Base class for all observer listeners. Derived class can implement method with any name
// But signature should be exactly void _custom_method_(const char* buffer, size_t len)
struct ModuleLoggerObserver
{
public:
    //virtual void ObserverCallbackMethod(const char* buffer, size_t len) = 0;
};

typedef void (ModuleLoggerOutCallback)(const char* buffer, size_t len);                                           // Classic callback
typedef void (ModuleLoggerObserver::* ModuleObserverObserverCallbackMethod)(const char* buffer, size_t len);      // Class method callback

/// endregion </Structures

class EmulatorContext;

class ModuleLogger : public Observer
{
    /// region <Constants>

    static const char* LoggerLevelNames[6];

    static const char* ALL;
    static const char* NONE;

    static const char* moduleNames[12];

    const char* submoduleCoreNames[4] =
    {
        "Generic",
        "Config",
        "Files",
        "Mainloop"
    };

    const char* submoduleZ80Names[10] =
    {
        "Generic",
        "M1",
        "Calls",
        "Jumps",
        "Interrupts",
        "Bit",
        "Arithmetics",
        "Stack",
        "Registers",
        "I/O"
    };

    const char* submoduleMemoryNames[3] =
    {
        "Generic",
        "ROM",
        "RAM"
    };

    const char* submoduleIONames[7] =
    {
        "Generic",
        "In",
        "Out",
        "Keyboard",
        "Tape",
        "Kempston joystick",
        "Kempston mouse"
    };

    const char* submoduleDiskNames[3] =
    {
        "Generic",
        "Floppy",
        "HDD"
    };

    const char* submoduleVideoNames[8] =
    {
        "Generic",
        "ULA",
        "ULA+",
        "Misc.",
        "ZX-Next",
        "Profi",
        "ATM",
        "TSConf"
    };

    const char* submoduleSoundNames[8] =
    {
        "Generic",
        "Beeper",
        "AY",
        "TurboSound",
        "TurboSound FM",
        "General Sound",
        "MoonSound",
        "SAA1099"
    };

    const char* submoduleDMANames[1] =
    {
        "Generic",
    };

    const char* submoduleLoaderNames[2] =
    {
        "SNA",
        "Z80"
    };

    const char* submoduleDebuggerNames[1] =
    {
        "Generic",
    };

    const char* submoduleDisassemblerNames[1] =
    {
        "Generic",
    };

    /// endregion </Constants>

    /// region <Fields>
protected:
    LoggerSettings _settings;
    volatile bool _mute = false;
    LoggerLevel _level = LoggerLevel::LogTrace;                      // Allow all types of logging messages by default

    EmulatorContext* _context = nullptr;
    ModuleLoggerOutCallback* _outCallback = nullptr;

    ModuleLoggerObserver* _observerInstance = nullptr;               // Observer class instance
    ModuleObserverObserverCallbackMethod _callbackMethod = nullptr;  // Class method

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    ModuleLogger(EmulatorContext* context);
    virtual ~ModuleLogger();
    /// endregion </Constructors / Destructors>

    /// region <Configuration methods>
public:
    void SetLoggingSettings(LoggerSettings& settings);

    void Mute();
    void Unmute();
    void TurnOffLoggingForAll();
    void TurnOnLoggingForAll();
    void TurnOffLoggingForModule(PlatformModulesEnum module, uint16_t submodule);
    void TurnOnLoggingForModule(PlatformModulesEnum module, uint16_t submodule);
    void SetLoggingLevel(LoggerLevel level);

    void SetLoggerOut(ModuleLoggerOutCallback callback);
    void SetLoggerOut(ModuleLoggerObserver* instance, ModuleObserverObserverCallbackMethod callback);
    void ResetLoggerOut();

    /// endregion </Configuration methods>

    /// region <Methods>
public:
    template<typename Fmt, typename... Args>
    void Trace(PlatformModulesEnum module, uint16_t submodule, Fmt fmt, Args... args)
    {
        if (!IsLoggingEnabledForLogLevel(module, submodule, LoggerLevel::LogTrace))
            return;

        LogMessage(LoggerLevel::LogTrace, module, submodule, fmt, args...);
    }

    template<typename Fmt, typename... Args>
    void Debug(PlatformModulesEnum module, uint16_t submodule, Fmt fmt, Args... args)
    {
        if (!IsLoggingEnabledForLogLevel(module, submodule, LoggerLevel::LogDebug))
            return;

        LogMessage(LoggerLevel::LogDebug, module, submodule, fmt, args...);
    }

    template<typename Fmt, typename... Args>
    void Info(PlatformModulesEnum module, uint16_t submodule, Fmt fmt, Args... args)
    {
        if (!IsLoggingEnabledForLogLevel(module, submodule, LoggerLevel::LogInfo))
            return;

        LogMessage(LoggerLevel::LogInfo, module, submodule, fmt, args...);
    }

    template<typename Fmt, typename... Args>
    void Warning(PlatformModulesEnum module, uint16_t submodule, Fmt fmt, Args... args)
    {
        if (!IsLoggingEnabledForLogLevel(module, submodule, LoggerLevel::LogWarning))
            return;

        LogMessage(LoggerLevel::LogWarning, module, submodule, fmt, args...);
    }

    template<typename Fmt, typename... Args>
    void Error(PlatformModulesEnum module, uint16_t submodule, Fmt fmt, Args... args)
    {
        if (!IsLoggingEnabledForLogLevel(module, submodule, LoggerLevel::LogError))
            return;

        LogMessage(LoggerLevel::LogError, module, submodule, fmt, args...);
    }

    void EmptyLine();

    void LogMessage(LoggerLevel level, PlatformModulesEnum module, uint16_t submodule, const std::string fmt, ...);
    void LogMessage(LoggerLevel level, PlatformModulesEnum module, uint16_t submodule, const char* fmt, ...);

    void OutLine(const char* buffer, size_t len);
    void Out(const char* buffer, size_t len);
    void Flush();

    /// endregion </Methods>

    /// region <Helper methods>
protected:
    bool IsLoggingEnabled(PlatformModulesEnum module, uint16_t submodule);
    bool IsLoggingEnabledForLogLevel(PlatformModulesEnum module, uint16_t submodule, LoggerLevel level);

    const char* GetSubmoduleName(PlatformModulesEnum module, uint16_t submodule);
    std::string GetModuleSubmoduleBriefString(PlatformModulesEnum module, uint16_t submodule);
    std::string GetModuleSubmoduleHexString(PlatformModulesEnum module, uint16_t submodule);
    /// endregion </Helper methods>

    /// region <Handle MessageCenter settings events>
public:
    void OnSettingsChangeRequested(int id, Message* message);
    void OnModuleSettingsChangeRequested(int id, Message* message);
    /// endregion </Handle MessageCenter keyboard events>

    /// region <Debug methods>
public:
    std::string DumpRequestedSettingsChange(uint32_t change);
    std::string DumpSettings();

protected:
    std::string DumpModules(uint32_t moduleFlags);
    std::string DumpModuleName(uint16_t module);
    bool DumpResolveSubmodule(uint16_t module, const char*** submoduleNames, size_t* submoduleNamesSize);
    std::string DumpResolveFlags(uint16_t flags, const char* names[], size_t nameSize);
    /// endregion </Debug methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class ModuleLoggerCUT : public ModuleLogger
{
public:
    ModuleLoggerCUT(EmulatorContext* context) : ModuleLogger(context) {};

public:
    using ModuleLogger::_settings;

    using ModuleLogger::IsLoggingEnabled;
    using ModuleLogger::GetModuleSubmoduleHexString;

    using ModuleLogger::DumpModules;
    using ModuleLogger::DumpModuleName;
    using ModuleLogger::DumpResolveSubmodule;
    using ModuleLogger::DumpResolveFlags;
};
#endif // _CODE_UNDER_TEST