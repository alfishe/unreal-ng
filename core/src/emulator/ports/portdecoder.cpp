#include "portdecoder.h"

#include <cassert>

#include "common/collectionhelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_pentagon512.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/ports/models/portdecoder_spectrum128.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"
#include "emulator/ports/models/portdecoder_spectrum48.h"
#include "emulator/sound/beeper.h"
#include "stdafx.h"

/// region <Constructors / Destructors>
PortDecoder::PortDecoder(EmulatorContext* context)
{
    _context = context;

    _state = &context->emulatorState;
    _keyboard = context->pKeyboard;
    _memory = context->pMemory;
    _screen = context->pScreen;
    _tape = context->pTape;
    _soundManager = context->pSoundManager;
    _logger = context->pModuleLogger;
}

PortDecoder::~PortDecoder()
{
    _portDevices.clear();
}
/// endregion </Constructors / Destructors>

/// region <Static methods>

PortDecoder* PortDecoder::GetPortDecoderForModel(MEM_MODEL model, EmulatorContext* context)
{
    PortDecoder* result = nullptr;
    CONFIG& config = context->config;
    uint32_t ramSize = config.ramsize;

    switch (model)
    {
        case MM_SPECTRUM48:
            result = new PortDecoder_Spectrum48(context);
            break;
        case MM_PENTAGON:
            if (ramSize == 512)
            {
                result = new PortDecoder_Pentagon512(context);
            }
            else
            {
                // Make 128k port decoder default
                result = new PortDecoder_Pentagon128(context);
            }
            break;
        case MM_SPECTRUM128:
            result = new PortDecoder_Spectrum128(context);
            break;
        case MM_PLUS3:
            result = new PortDecoder_Spectrum3(context);
            break;
        case MM_PROFI:
            result = new PortDecoder_Profi(context);
            break;
        case MM_SCORP:
            result = new PortDecoder_Scorpion256(context);
            break;
        default:
            LOGERROR("PortDecoder::GetPortDecoderForModel - Unknown model: %d", model);
            throw std::logic_error(
                StringHelper::Format("PortDecoder::GetPortDecoderForModel - unknown model %d", model));
            break;
    }

    return result;
}

/// endregion </Static methods>

/// region <Interface methods>

uint8_t PortDecoder::DecodePortIn(uint16_t addr, [[maybe_unused]] uint16_t pc)
{
    uint8_t result = 0xFF;

    /// region <Port In breakpoint logic>

    if (_context->pDebugManager != nullptr)
    {
        Emulator& emulator = *_context->pEmulator;
        BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();

        uint16_t breakpointID = brk.HandlePortIn(addr);
        if (breakpointID != BRK_INVALID)
        {
            // Pause emulator (single source of truth)
            emulator.Pause();

            // Broadcast notification - breakpoint triggered
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleNumberPayload* payload = new SimpleNumberPayload(breakpointID);
            messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);

            // Wait until emulator resumed externally
            emulator.WaitWhilePaused();
        }
    }

    /// endregion </Port In breakpoint logic>

    // NOTE: Hardware I/O is handled by subclass via PeripheralPortIn().
    // This base implementation is for legacy compatibility only.
    // Subclasses should call OnPortInComplete() after performing I/O.
    result = PeripheralPortIn(addr);

    // Track port read access
    if (_memory && _memory->_memoryAccessTracker)
    {
        uint16_t callerAddress = _context->pCore->GetZ80()->m1_pc;
        _memory->_memoryAccessTracker->TrackPortRead(addr, result, callerAddress);
    }

    return result;
}

/// Called by subclasses AFTER hardware read completes.
/// Handles breakpoints, tracking, and future analyzer notifications.
void PortDecoder::OnPortInComplete(uint16_t port, uint8_t result, [[maybe_unused]] uint16_t pc)
{
    // 1. Breakpoint handling
    if (_context->pDebugManager != nullptr)
    {
        Emulator& emulator = *_context->pEmulator;
        BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();

        uint16_t breakpointID = brk.HandlePortIn(port);
        if (breakpointID != BRK_INVALID)
        {
            emulator.Pause();
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleNumberPayload* payload = new SimpleNumberPayload(breakpointID);
            messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);
            emulator.WaitWhilePaused();
        }
    }

    // 2. Port access tracking
    if (_memory && _memory->_memoryAccessTracker)
    {
        uint16_t callerAddress = _context->pCore->GetZ80()->m1_pc;
        _memory->_memoryAccessTracker->TrackPortRead(port, result, callerAddress);
    }

    // 3. Future: Analyzer notifications can be added here
}

void PortDecoder::DecodePortOut(uint16_t addr, [[maybe_unused]] uint8_t value, [[maybe_unused]] uint16_t pc)
{
    /// region <Port Out breakpoint logic>

    if (_context->pDebugManager != nullptr)
    {
        Emulator& emulator = *_context->pEmulator;
        BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();

        uint16_t breakpointID = brk.HandlePortOut(addr);
        if (breakpointID != BRK_INVALID)
        {
            emulator.Pause();
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleNumberPayload* payload = new SimpleNumberPayload(breakpointID);
            messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);
            emulator.WaitWhilePaused();
        }
    }

    /// endregion </Port Out breakpoint logic>

    // NOTE: Hardware I/O is handled by subclass via PeripheralPortOut().
    // This base implementation is for legacy compatibility only.
    // Subclasses should call OnPortOutComplete() after performing I/O.
    PeripheralPortOut(addr, value);

    // Track port write access
    if (_memory && _memory->_memoryAccessTracker)
    {
        uint16_t callerAddress = _context->pCore->GetZ80()->m1_pc;
        _memory->_memoryAccessTracker->TrackPortWrite(addr, value, callerAddress);
    }
}

/// Called by subclasses AFTER hardware write completes.
/// Handles breakpoints, tracking, and future analyzer notifications.
void PortDecoder::OnPortOutComplete(uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc)
{
    // 1. Breakpoint handling
    if (_context->pDebugManager != nullptr)
    {
        Emulator& emulator = *_context->pEmulator;
        BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();

        uint16_t breakpointID = brk.HandlePortOut(port);
        if (breakpointID != BRK_INVALID)
        {
            emulator.Pause();
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleNumberPayload* payload = new SimpleNumberPayload(breakpointID);
            messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);
            emulator.WaitWhilePaused();
        }
    }

    // 2. Port access tracking
    if (_memory && _memory->_memoryAccessTracker)
    {
        uint16_t callerAddress = _context->pCore->GetZ80()->m1_pc;
        _memory->_memoryAccessTracker->TrackPortWrite(port, value, callerAddress);
    }

    // 3. Future: Analyzer notifications can be added here
}


/// Keyboard ports:
/// #FEFE
/// #FDFE
/// #FBFE
/// #F7FE
/// #EFFE
/// #DFFE
/// #BFFE
/// #7FFE
/// \param port Port to check for match
/// \return If port matched as #FE
bool PortDecoder::IsFEPort(uint16_t port)
{
    /// region <Override submodule>
    [[maybe_unused]]
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
    /// endregion </Override submodule>

    // Any even port will be decoded as #FE
    static const uint16_t port_FE_full = 0b0000'0000'1111'1110;
    static const uint16_t port_FE_mask = 0b0000'0000'0000'0001;
    static const uint16_t port_FE_match = 0b0000'0000'0000'0000;

    // Compile-time check
    static_assert((port_FE_full & port_FE_mask) == port_FE_match && "Mask pattern incorrect");

    bool result = (port & port_FE_mask) == port_FE_match;

    return result;
}

/// Default implementation for 'in (#FE)'
/// Bits [0:4] - Keyboard selected half-row buttons state
/// Bit  [6]   - MIC In
/// \param port
/// \param pc
/// \return
uint8_t PortDecoder::Default_Port_FE_In(uint16_t port, [[maybe_unused]] uint16_t pc)
{
    uint8_t result = 0xFF;

    result = _keyboard->HandlePortIn(port);

    // Only bit 6 (EAR) of port #FE is affected by tape input signal
    static const uint8_t maskEAR = 0b0100'0000;
    static const uint8_t invMaskEAR = 0b1011'1111;

    result &= invMaskEAR;
    uint8_t inputEARSignal = _tape->handlePortIn() & maskEAR;
    result |= inputEARSignal;

    return result;
}

/// Default implementation for 'out (#FE)'
/// Bits [0:2]  - Border color
/// Bit  [3]    - MIC output bit
/// Bit  [4]    - EAR output bit
/// See: https://worldofspectrum.org/faq/reference/48kreference.htm
/// \param port
/// \param value
/// \param pc
/// \return
void PortDecoder::Default_Port_FE_Out(uint16_t port, uint8_t value, uint16_t pc)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT;
    /// endregion </Override submodule>

    [[maybe_unused]] const uint32_t tState = _context->pCore->GetZ80()->t;

    // Persist output value
    _context->emulatorState.pFE = value;

    uint8_t borderColor = value & 0b000'00111;
    [[maybe_unused]] bool micBit = (value & 0b0000'1000) > 0;
    [[maybe_unused]] bool beeperBit = (value & 0b0001'0000) > 0;

    // Pass value to the tape and beeper sound generator
    _tape->handlePortOut(value);
    //_soundManager->getBeeper().handlePortOut(value, tState);

    // Set border color
    _screen->SetBorderColor(borderColor);

    /// region <Debug logging>

    // Treat all FE ports as one for logging purposes
    if ((port & 0x00FE) == 0x00FE)
        port = 0x00FE;

    if (!key_exists(_loggingMutePorts, port))
    {
        MLOGDEBUG(DumpPortValue(0xFE, port, value, pc, Dump_FE_value(value).c_str()));
    }
    /// endregion </Debug logging>
}

std::string PortDecoder::GetPCAddressLocator(uint16_t pc)
{
    std::string result;

    if (pc < 0x4000)
    {
        if (_memory->IsBank0ROM())
        {
            uint8_t romPage = _memory->GetROMPage();
            result = StringHelper::Format(" ROM_%d", romPage);
        }
        else
        {
            uint8_t ramPage = _memory->GetRAMPageForBank0();
            result = StringHelper::Format(" RAM_%d", ramPage);
        }
    }
    else if (pc >= 0xC000)
    {
        uint8_t ramPage = _memory->GetRAMPageForBank3();
        result = StringHelper::Format(" RAM_%d", ramPage);
    }

    return result;
}

/// endregion </Interface methods>

/// region <Interaction with peripherals>
bool PortDecoder::RegisterPortHandler(uint16_t port, PortDevice* device)
{
    bool result = false;

    if (device)
    {
        if (!key_exists(_portDevices, port))
        {
            _portDevices.insert({port, device});
        }
        else
        {
            MLOGWARNING("PortDecoder::registerPortHandler - handler for port: #%04X already registered", port);
        }
    }

    return result;
}

void PortDecoder::UnregisterPortHandler(uint16_t port)
{
    if (key_exists(_portDevices, port))
    {
        _portDevices.erase(port);
    }
}

/// Pass port IN operation to the peripheral device registered to handle specified port
/// \param port Specified port address
/// \return Value for the specified port returned by peripheral device (if exists). Otherwise #FF
uint8_t PortDecoder::PeripheralPortIn(uint16_t port)
{
    uint8_t result = 0xFF;

    if (key_exists(_portDevices, port))
    {
        // Peripheral registered to handle port event found
        PortDevice* device = _portDevices.at(port);
        if (device)
        {
            result = device->portDeviceInMethod(port);
        }
    }
    else
    {
        // No peripheral to handle this port IN available

        // Determine RAM/ROM page where code executed from
        uint16_t pc = _context->pCore->GetZ80()->m1_pc;  // Use IN command PC, not the next one (z80->pc)
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGWARNING("[In] [PC:%04X%s] Port: %02X - no peripheral device to handle", pc, currentMemoryPage.c_str(),
                    port);
    }

    return result;
}

/// Pass port OUT operation to the peripheral device registered to handle specified port
/// \param port Specified port address
/// \param value Value to output into specified port
void PortDecoder::PeripheralPortOut(uint16_t port, uint8_t value)
{
    if (key_exists(_portDevices, port))
    {
        // Peripheral registered to handle port event found
        PortDevice* device = _portDevices.at(port);
        if (device)
        {
            device->portDeviceOutMethod(port, value);
        }
    }
    else
    {
        // No peripheral to handle this port OUT available

        // Determine RAM/ROM page where code executed from
        uint16_t pc = _context->pCore->GetZ80()->m1_pc;  // Use OUT command PC, not the next one (z80->pc)
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGWARNING("[Out] [PC:%04X%s] Port: %02X; Value: %02X - no peripheral device to handle", pc,
                    currentMemoryPage.c_str(), port, value);
    }
}

/// endregion </Interaction with peripherals>

/// region <Privileged operations for snapshot loading / debug>

/// Unlock port 7FFD paging for snapshot loading or debug sessions
/// Clears the lock bit (bit 5) in emulatorState.p7FFD, allowing subsequent port writes
void PortDecoder::UnlockPaging()
{
    if (_state)
    {
        _state->p7FFD &= ~PORT_7FFD_LOCK;
        MLOGINFO("Port 7FFD paging unlocked for snapshot/debug");
    }
}

/// Lock port 7FFD paging (for emulation accuracy or testing)
/// Sets the lock bit (bit 5) in emulatorState.p7FFD
void PortDecoder::LockPaging()
{
    if (_state)
    {
        _state->p7FFD |= PORT_7FFD_LOCK;
        MLOGINFO("Port 7FFD paging locked");
    }
}

/// endregion </Privileged operations for snapshot loading / debug>

/// region <Debug information>

void PortDecoder::MuteLoggingForPort(uint16_t port)
{
    _loggingMutePorts.insert(port);
}

void PortDecoder::UnmuteLoggingForPort(uint16_t port)
{
    auto item = _loggingMutePorts.find(port);

    if (item != _loggingMutePorts.end())
    {
        _loggingMutePorts.erase(item);
    }
}

std::string PortDecoder::DumpPortValue(uint16_t refPort, uint16_t port, uint8_t value, uint16_t pc, const char* comment)
{
    std::string result;

    std::string pcString;
    if (pc == 0x0000)
    {
        // Port triggered during reset / debug
        pcString = "<Init>";
    }
    else
    {
        // Determine RAM/ROM page where code executed from
        std::string currentMemoryPage = GetPCAddressLocator(pc);

        pcString = StringHelper::Format("PC:0x%04X%s", pc, currentMemoryPage.c_str());
    }

    if (comment != nullptr)
    {
        result = StringHelper::Format("[Out] [%s] Port #%04X, decoded as #%04X value: 0x%02X (%s)", pcString.c_str(),
                                      port, refPort, value, comment);
    }
    else
    {
        result = StringHelper::Format("[Out] [%s] Port #%04X, decoded as #%04X value: 0x%02X", pcString.c_str(), port,
                                      refPort, value);
    }

    return result;
}

std::string PortDecoder::Dump_FE_value(uint8_t value)
{
    uint8_t borderColor = value & 0b000'00111;
    bool beeperBit = value & 0b0001'0000;
    std::string colorText = Screen::GetColorName(borderColor);

    std::string result =
        StringHelper::Format("Border color: %d (%s); Beeper: %d", borderColor, colorText.c_str(), beeperBit);

    return result;
}

/// endregion </Debug information>