#include "portdecoder.h"

#include <cassert>

#include "base/featuremanager.h"
#include "common/collectionhelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelmanager.h"  // Phase 4 — RecordIoWrite hot-path call
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/notifications.h"
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

            // Broadcast notification - breakpoint triggered (instance-tagged per GDB TDD §6.3)
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            BreakpointTriggeredPayload* payload =
                new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, addr);
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

    // Port trace: this legacy path bypasses OnPortInComplete, so a Ghost-Byte double
    // read through it would otherwise be invisible. Record with viaLegacyBasePath
    // so both reads of a ghost pair are visible and distinguishable (use case 4.1)
    if (_portTraceFeatureCache) [[unlikely]]
    {
        PortDecodeDisposition disp;
        disp.decodedPort = addr;  // Legacy path performs no decoding: identity
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.wasDecoded = _lastPortDecoded;
        disp.viaLegacyBasePath = true;
        RecordPortTrace(/*isOut=*/false, addr, result, pc, disp);
    }

    return result;
}

/// Called by subclasses AFTER hardware read completes.
/// Handles breakpoints, tracking, port trace capture, and future analyzer notifications.
void PortDecoder::OnPortInComplete(uint16_t port, uint8_t result, [[maybe_unused]] uint16_t pc,
                                   const PortDecodeDisposition& disp)
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
            BreakpointTriggeredPayload* payload =
                new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, port);
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

    // 3. Port trace capture (runtime feature "porttrace"; single cached-bool test when off)
    if (_portTraceFeatureCache) [[unlikely]]
    {
        RecordPortTrace(/*isOut=*/false, port, result, pc, disp);
    }

    // 4. Future: Analyzer notifications can be added here
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
            BreakpointTriggeredPayload* payload =
                new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, addr);
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

    // Port trace: legacy path bypasses OnPortOutComplete — see DecodePortIn note
    if (_portTraceFeatureCache) [[unlikely]]
    {
        PortDecodeDisposition disp;
        disp.decodedPort = addr;  // Legacy path performs no decoding: identity
        disp.decodeRuleIndex = PortTraceRule::kNoTable;
        disp.viaLegacyBasePath = true;
        RecordPortTrace(/*isOut=*/true, addr, value, pc, disp);
    }
}

/// Called by subclasses AFTER hardware write completes.
/// Handles breakpoints, tracking, port trace capture, and future analyzer notifications.
void PortDecoder::OnPortOutComplete(uint16_t port, uint8_t value, [[maybe_unused]] uint16_t pc,
                                    const PortDecodeDisposition& disp)
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
            BreakpointTriggeredPayload* payload =
                new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, port);
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

    // 3. Phase 4 — IO write journal (TDD §9.3) + access probe (§9.2).
    // OnPortOutComplete is the single common path called by ALL subclass
    // DecodePortOut overrides after the hardware write completes.
    if (_context->pTimeTravelManager != nullptr)
    {
        _context->pTimeTravelManager->RecordIoWrite(port, value, pc);
    }
    if (_context->ttdProbe.IsArmed())
    {
        if (_context->ttdProbe.Matches(port, ttd::TTDAccessType::Io, value, pc))
        {
            const auto& st = _context->emulatorState;
            const uint16_t tin = _context->pCore ? _context->pCore->GetZ80()->t : 0;
            const ttd::TTDTimePoint tp{st.frame_counter, tin};
            _context->ttdProbe.RecordHit(tp, pc, value, /*physPage=*/0,
                                          ttd::TTDAccessType::Io);
        }
    }

    // 4. Port trace capture (runtime feature "porttrace"; single cached-bool test when off)
    if (_portTraceFeatureCache) [[unlikely]]
    {
        RecordPortTrace(/*isOut=*/true, port, value, pc, disp);
    }
}

/// region <Port trace (runtime feature "porttrace")>

/// Re-read the porttrace feature flag and instantiate/release the recorder.
/// Called from FeatureManager::onFeatureChanged (control path, never the hot path).
void PortDecoder::UpdateFeatureCache()
{
    FeatureManager* fm = _context ? _context->pFeatureManager : nullptr;
    bool enabled = fm && fm->isEnabled(Features::kPortTrace);

    if (enabled && !_portTrace)
    {
        // Feature turned on: instantiate the recorder lazily (buffer memory is
        // allocated only now, never while the feature is off)
        _portTrace = std::make_unique<PortDiagnosticRecorder>();
        _activitySummary.reset(static_cast<uint32_t>(_state->frame_counter));
    }
    else if (!enabled && _portTrace)
    {
        // Feature turned off: stop capture and release the buffer memory
        _portTrace->stop();
        _portTrace.reset();
    }

    _portTraceFeatureCache = enabled;
}

/// Build and push exactly one PortTraceEvent per Z80 I/O operation.
/// Only reached when the porttrace feature is on (_portTraceFeatureCache).
void PortDecoder::RecordPortTrace(bool isOut, uint16_t rawPort, uint8_t value, uint16_t pc,
                                  const PortDecodeDisposition& disp)
{
    uint32_t frame = static_cast<uint32_t>(_state->frame_counter);

    // Frame-scoped counters update even without an active capture session
    _activitySummary.onEvent(frame, isOut, disp);

    if (!_portTrace || !_portTrace->isCapturing())
        return;

    PortTraceEvent event;

    // Absolute T-state: frame_counter * tStatesPerFrame + t-in-frame.
    // pCore may be absent in unit-test contexts — degrade to frame-start timestamp.
    uint32_t tInFrame = (_context->pCore != nullptr) ? _context->pCore->GetZ80()->t : 0;
    event.timestamp = _state->frame_counter * static_cast<uint64_t>(_context->config.frame) + tInFrame;
    event.frameNumber = frame;
    event.rawPort = rawPort;
    event.decodedPort = disp.decodedPort;
    event.pc = pc;
    event.value = value;
    event.decodeRuleIndex = disp.decodeRuleIndex;
    event.deviceId = PortDiagnosticRecorder::ResolveDeviceId(disp.decodedPort);

    bool hadHandler = (disp.decodedPort != 0x0000) && key_exists(_portDevices, disp.decodedPort);

    uint8_t flags = 0;
    if (isOut)
        flags |= PortTraceFlags::kDirectionOut;
    if (disp.wasDecoded)
        flags |= PortTraceFlags::kWasDecoded;
    if (hadHandler)
        flags |= PortTraceFlags::kHadHandler;
    if (disp.wasBeta128Gated)
        flags |= PortTraceFlags::kBeta128Gated;
    if (disp.wasHandledInline)
        flags |= PortTraceFlags::kHandledInline;
    if (_state->flags & CF_TRDOS)
        flags |= PortTraceFlags::kCfTrdosActive;
    if (disp.viaLegacyBasePath)
        flags |= PortTraceFlags::kViaLegacyBasePath;
    event.flags = flags;

    _portTrace->record(event);
}

PortTraceSessionInfo PortDecoder::getPortTraceSessionInfo() const
{
    PortTraceSessionInfo info;

    if (_context)
    {
        info.emulatorId = _context->emulatorId.toString();
        info.tStatesPerFrame = _context->config.frame;

        switch (_context->config.mem_model)
        {
            case MM_PENTAGON:    info.modelName = "Pentagon"; break;
            case MM_SPECTRUM48:  info.modelName = "Spectrum48"; break;
            case MM_SPECTRUM128: info.modelName = "Spectrum128"; break;
            case MM_PLUS3:       info.modelName = "SpectrumPlus3"; break;
            case MM_PROFI:       info.modelName = "Profi"; break;
            case MM_SCORP:       info.modelName = "Scorpion256"; break;
            default:             info.modelName = "Unknown"; break;
        }
    }

    info.decodeRules = getPortTraceDecodeRules();

    return info;
}

/// endregion </Port trace>


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

    // Sync border_attr with pFE bits 0-2 (TTD capture reads this field)
    _context->emulatorState.border_attr = borderColor;

    // Pass value to the tape and beeper sound generator
    _tape->handlePortOut(value);
    _soundManager->getBeeper().handlePortOut(value, tState);

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

    // Memory may be absent in unit-test contexts
    if (_memory == nullptr)
        return result;

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
            result = true;  // Fix: return true on successful registration
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
            _lastPortDecoded = true;
        }
    }
    else
    {
        // No peripheral to handle this port IN available

        // Determine RAM/ROM page where code executed from
        // (pCore may be absent in unit-test contexts)
        uint16_t pc = _context->pCore ? _context->pCore->GetZ80()->m1_pc : 0;  // Use IN command PC, not the next one (z80->pc)
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
        // (pCore may be absent in unit-test contexts)
        uint16_t pc = _context->pCore ? _context->pCore->GetZ80()->m1_pc : 0;  // Use OUT command PC, not the next one (z80->pc)
        std::string currentMemoryPage = GetPCAddressLocator(pc);
        MLOGWARNING("[Out] [PC:%04X%s] Port: %02X; Value: %02X - no peripheral device to handle", pc,
                    currentMemoryPage.c_str(), port, value);
    }
}

/// endregion </Interaction with peripherals>

/// region <Privileged operations for snapshot loading / debug>

/// Unlock port 7FFD paging for snapshot loading or debug sessions
/// Clears both the emulatorState.p7FFD lock bit AND the hardware latch (_7FFD_Locked)
/// This ensures subsequent port writes via DecodePortOut() will be accepted
void PortDecoder::UnlockPaging()
{
    // Clear the hardware latch so Port_7FFD_Out() will accept writes
    _7FFD_Locked = false;

    if (_state)
    {
        _state->p7FFD &= ~PORT_7FFD_LOCK;
        MLOGINFO("Port 7FFD paging unlocked for snapshot/debug");
    }
}

/// Lock port 7FFD paging (for emulation accuracy or testing)
/// Sets both the emulatorState.p7FFD lock bit AND the hardware latch (_7FFD_Locked)
void PortDecoder::LockPaging()
{
    // Set the hardware latch to match the lock bit
    _7FFD_Locked = true;

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