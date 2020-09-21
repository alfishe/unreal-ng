#pragma once
#include "stdafx.h"

#include "common/logger.h"

#include "3rdparty/message-center/messagecenter.h"
#include <cstring>

/// region <Structures>

struct LoggerSettings
{
    uint32_t modules;                       // All modules on/off flags

    union
    {
        // Indexed access array
        // Allow all modules logging by default
        uint16_t submodules[10] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };

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
            uint16_t debuggerSubmodules;    // Memory submodules on/off flags
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
        memcpy((void *)&settings, (const void *)&settings, sizeof (LoggerSettings));
    }
    virtual ~LoggerSettingsModulePayload() {};
};

/// endregion </Structures

class EmulatorContext;

class ModuleLogger : public Observer
{
    /// region <Constants>

    const char* ALL = "<All>";
    const char* NONE = "<None>";

    const char* moduleNames[10] =
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
        "Debugger"
    };

    const char* submoduleCoreNames[1] =
    {
        "Files"
    };

    const char* submoduleZ80Names[8] =
    {
        "Calls",
        "Jumps",
        "Interrupts",
        "Bit",
        "Arithmetics",
        "Stack",
        "Registers",
        "I/O"
    };

    const char* submoduleMemoryNames[2] =
    {
        "ROM",
        "RAM"
    };

    const char* submoduleIONames[3] =
    {
        "Keyboard",
        "Kempston joystick",
        "Kempston mouse"
    };

    const char* submoduleDiskNames[2] =
    {
            "Floppy",
            "HDD"
    };

    const char* submoduleVideoNames[7] =
    {
        "ULA",
        "ULA+",
        "Misc.",
        "ZX-Next",
        "Profi",
        "ATM",
        "TSConf"
    };

    const char* submoduleSoundNames[4] =
    {
        "AY",
        "General Sound",
        "MoonSound",
        "SAA1099"
    };

    const char* submoduleDMANames[1] =
    {
        "None",
    };

    const char* submoduleDebuggerNames[1] =
    {
        "None",
    };



    /// endregion </Constants>
protected:
    EmulatorContext* _context;

    /// region <Constructors / Destructors>
public:
    ModuleLogger(EmulatorContext* context);
    virtual ~ModuleLogger();
    /// endregion </Constructors / Destructors>

    /// region <Methods>
public:
    void SetLoggingSettings(LoggerSettings& settings);
    /// endregion </Methods>

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
    using ModuleLogger::DumpModules;
    using ModuleLogger::DumpModuleName;
    using ModuleLogger::DumpResolveSubmodule;
    using ModuleLogger::DumpResolveFlags;
};
#endif // _CODE_UNDER_TEST