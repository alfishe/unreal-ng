#pragma once
#include "stdafx.h"

#include <set>
#include "emulator/platform.h"


class ModuleLogger;
class EmulatorContext;
class Memory;
class Screen;
class Beeper;
class Tape;
class SoundManager;
class Keyboard;

/// region <Constants>

constexpr uint16_t PORT_FFFD = 0xFFFD;
constexpr uint16_t PORT_BFFD = 0xBFFD;

// Port 0x7FFD bits
constexpr uint8_t PORT_7FFD_RAM_BANK_BITMASK    = 0b0000'0111;
constexpr uint8_t PORT_7FFD_SCREEN              = (1u << 3);
constexpr uint8_t PORT_7FFD_ROM_BANK            = (1u << 4);
constexpr uint8_t PORT_7FFD_LOCK                = (1u << 5);

constexpr uint8_t PORT_7FFD_SCREEN_NORMAL       = 0;
constexpr uint8_t PORT_7FFD_SCREEN_SHADOW       = (1u << 3);

constexpr uint8_t PORT_7FFD_ROM_BANK_0          = 0;
constexpr uint8_t PORT_7FFD_ROM_BANK_1          = (1u << 4);

constexpr uint8_t PORT_7FFD_RAM_BANK_0          = 0b0000'0000;
constexpr uint8_t PORT_7FFD_RAM_BANK_1          = 0b0000'0001;
constexpr uint8_t PORT_7FFD_RAM_BANK_2          = 0b0000'0010;
constexpr uint8_t PORT_7FFD_RAM_BANK_3          = 0b0000'0011;
constexpr uint8_t PORT_7FFD_RAM_BANK_4          = 0b0000'0100;
constexpr uint8_t PORT_7FFD_RAM_BANK_5          = 0b0000'0101;
constexpr uint8_t PORT_7FFD_RAM_BANK_6          = 0b0000'0110;
constexpr uint8_t PORT_7FFD_RAM_BANK_7          = 0b0000'0111;

/// endregion <Constants>

/// region <Types>

struct PortMatch
{
    uint16_t mask;
    uint16_t match;
    uint16_t resolvedPort;
};

/// Base class to mark all devices connected to port decoder
class PortDevice
{
public:
    virtual uint8_t portDeviceInMethod(uint16_t port) = 0;
    virtual void portDeviceOutMethod(uint16_t port, uint8_t) = 0;
};

typedef uint8_t (PortDevice::* PortDeviceInMethod)(uint16_t port);              // Class method callback
typedef void (PortDevice::* PortDeviceOutMethod)(uint16_t port, uint8_t value); // Class method callback

/// endregion </Types>

/// ============================================================================
/// PORT DECODER ARCHITECTURE
/// ============================================================================
///
/// OVERVIEW:
/// The PortDecoder class hierarchy provides model-specific port decoding for
/// different ZX Spectrum variants. Each model has different port address decoding
/// and may have different peripherals attached.
///
/// IMPORTANT: HARDWARE I/O MUST HAPPEN EXACTLY ONCE
/// -------------------------------------------------
/// Many hardware devices (FDC, AY chip, etc.) have stateful registers where
/// reading clears flags or advances internal state. Double-reading causes data loss.
///
/// CORRECT PATTERN (for subclasses):
/// ```cpp
/// uint8_t PortDecoder_ModelX::DecodePortIn(uint16_t port, uint16_t pc)
/// {
///     uint8_t result = 0xFF;
///     uint16_t decodedPort = decodePort(port);  // Model-specific decoding
///
///     // 1. Perform hardware I/O (ONCE)
///     result = PeripheralPortIn(decodedPort);
///
///     // 2. Call common handler for breakpoints, tracking, analyzers
///     OnPortInComplete(decodedPort, result, pc);
///
///     return result;
/// }
/// ```
///
/// ANTI-PATTERN (DO NOT DO THIS):
/// ```cpp
/// uint8_t PortDecoder_ModelX::DecodePortIn(uint16_t port, uint16_t pc)
/// {
///     PortDecoder::DecodePortIn(port, pc);  // <-- WRONG! May cause double read
///     result = PeripheralPortIn(decodedPort);
///     return result;
/// }
/// ```
///
/// See: docs/emulator/design/ports/port-decoder-architecture.md
/// ============================================================================

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

    EmulatorState* _state = nullptr;
    Keyboard* _keyboard = nullptr;
    Tape* _tape = nullptr;
    Memory* _memory = nullptr;
    Screen* _screen = nullptr;
    SoundManager* _soundManager = nullptr;
    ModuleLogger* _logger = nullptr;

    // Registered port handlers from external peripheral devices
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
    virtual void reset() = 0;
    virtual uint8_t DecodePortIn(uint16_t addr, uint16_t pc);
    virtual void DecodePortOut(uint16_t addr, uint8_t value, uint16_t pc);

    virtual void SetRAMPage(uint8_t page) { (void)page; /* Intentionally unused */ };
    virtual void SetROMPage(uint8_t page) { (void)page; /* Intentionally unused */ };

    virtual bool IsFEPort(uint16_t port);
    uint8_t Default_Port_FE_In(uint16_t port, uint16_t pc);
    void Default_Port_FE_Out(uint16_t port, uint8_t value, uint16_t pc);

protected:
    /// Called by subclasses AFTER hardware I/O completes.
    /// Handles: breakpoints, port access tracking, analyzer notifications.
    /// @param port The decoded port address that was accessed
    /// @param result The value read from the port
    /// @param pc Program counter of the IN instruction
    void OnPortInComplete(uint16_t port, uint8_t result, uint16_t pc);
    
    /// Called by subclasses AFTER hardware I/O completes.
    /// Handles: breakpoints, port access tracking, analyzer notifications.
    /// @param port The decoded port address that was accessed
    /// @param value The value written to the port
    /// @param pc Program counter of the OUT instruction
    void OnPortOutComplete(uint16_t port, uint8_t value, uint16_t pc);

    virtual std::string GetPCAddressLocator(uint16_t pc);
    /// endregion </Interface methods>


    /// region <Interaction with peripherals>
public:
    bool RegisterPortHandler(uint16_t port, PortDevice* device);
    void UnregisterPortHandler(uint16_t port);

    uint8_t PeripheralPortIn(uint16_t port);
    void PeripheralPortOut(uint16_t port, uint8_t value);
    
    /// Unlock port 7FFD paging for snapshot loading or debug sessions
    /// This bypasses the lock bit check, allowing subsequent port writes to succeed
    void UnlockPaging();
    
    /// Lock port 7FFD paging (for debug sessions only, not used in normal operation)
    void LockPaging();

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