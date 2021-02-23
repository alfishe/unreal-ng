#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include <set>

class ModuleLogger;
class EmulatorContext;
class Memory;
class Screen;
class Keyboard;

/// region <Types>

/// Base class to mark all devices connected to port decoder
class PortDevice
{
public:
    virtual uint8_t PortDeviceInMethod(uint16_t port) = 0;
    virtual void PortDeviceOutMethod(uint16_t port, uint8_t) = 0;
};

typedef uint8_t (PortDevice::* PortDeviceInMethod)(uint16_t port);              // Class method callback
typedef void (PortDevice::* PortDeviceOutMethod)(uint16_t port, uint8_t value); // Class method callback

/// endregion </Types>

/// Base class for all model port decoders
class PortDecoder
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_GENERIC;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Static methods>
public:
    static PortDecoder* GetPortDecoderForModel(MEM_MODEL model, EmulatorContext* context);
    /// endregion </Static methods>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;

    State* _state = nullptr;
    Keyboard* _keyboard = nullptr;
    Memory* _memory = nullptr;
    Screen* _screen = nullptr;
    ModuleLogger* _logger = nullptr;

    // Registered port handlers from peripheral devices
    std::map<uint16_t, PortDevice*> _portDevices;

    // Set of ports to mute logging to
    std::set<uint16_t> _loggingMutePorts;

    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    PortDecoder() = delete;     // Disable default constructor; C++ 11 feature
    PortDecoder(EmulatorContext* context);
    virtual ~PortDecoder();
    /// endregion </Constructors / destructors>

    /// region <Interface methods>
public:
    virtual void Reset() = 0;
    virtual uint8_t DecodePortIn(uint16_t addr, uint16_t pc);
    virtual void DecodePortOut(uint16_t addr, uint8_t value, uint16_t pc);

    virtual void SetRAMPage(uint8_t oage) = 0;
    virtual void SetROMPage(uint8_t page) = 0;

    virtual bool IsFEPort(uint16_t port);
    virtual void Port_FE_Out(uint16_t port, uint8_t value, uint16_t pc);

protected:
    virtual std::string GetPCAddressLocator(uint16_t pc);
    /// endregion </Interface methods>

    /// region <Interaction with peripherals>
public:
    bool RegisterPortHandler(uint16_t port, PortDevice* device);
    void UnregisterPortHandler(uint16_t port);

    uint8_t PeripheralPortIn(uint16_t port);
    void PeripheralPortOut(uint16_t port, uint8_t value);

    /// endregion </Interaction with peripherals>


    /// region <Debug information>
public:
    void MuteLoggingForPort(uint16_t port);
    void UnmuteLoggingForPort(uint16_t port);

protected:
    virtual std::string DumpPortValue(uint16_t refPort, uint16_t port, uint8_t value, uint16_t pc, const char* comment = nullptr);
    virtual std::string Dump_FE_value(uint8_t value);

    /// endregion </Debug information>

};