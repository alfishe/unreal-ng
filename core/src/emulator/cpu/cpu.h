#pragma once
#include "stdafx.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/cpu/cputables.h"
#include "emulator/cpu/z80.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/ports.h"
#include "emulator/memory/rom.h"
#include "emulator/io/hdd/hdd.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "emulator/emulatorcontext.h"

class ModuleLogger;
class MessageCenter;
class Z80;
class PortDecoder;

class CPU
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
    const uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_GENERIC;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Static>
    // Ensure that all flag / decoding tables are initialized only once using static member
public:
    static CPUTables _cpuTables;
    /// endregion </Static>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;
    EmulatorState* _state = nullptr;
    const CONFIG* _config = nullptr;
    ModuleLogger* _logger = nullptr;

    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    Ports* _ports = nullptr;
    PortDecoder* _portDecoder = nullptr;
    ROM* _rom = nullptr;
    Keyboard* _keyboard = nullptr;
    SoundManager* _sound = nullptr;
    HDD* _hdd = nullptr;
    VideoControl* _video = nullptr;
    Screen* _screen = nullptr;

    ROMModeEnum _mode = ROMModeEnum::RM_NOCHANGE;
    bool _pauseRequested = false;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    CPU() = delete;			        // Disable default constructor. C++ 11 feature
    CPU(EmulatorContext* context);  // Only constructor with context param is allowed
    virtual ~CPU();
    /// endregion </Constructors / Destructors

    /// region <Initialization>
    [[nodiscard]] bool Init();
    void Release();
    /// endregion </Initialization>

    /// region <Properties>
    Z80* GetZ80() { return _z80; }
    Memory* GetMemory() { return _memory; }
    Ports* GetPorts() { return _ports; }
    ROM* GetROM() { return _rom; }

    /// endregion </Properties>

    // Configuration methods
public:
    void UseFastMemoryInterface();
    void UseDebugMemoryInterface();

    // Z80 CPU-related methods
public:
    void Reset();
    void Pause();
    void Resume();

    void SetCPUClockSpeed(uint8_t);
    uint32_t GetBaseCPUFrequency();
    uint32_t GetCPUFrequency();
    uint16_t GetCPUFrequencyMultiplier();

    void CPUFrameCycle();
    void AdjustFrameCounters();

    // Event handlers
public:
    void UpdateScreen();
};
