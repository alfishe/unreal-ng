#include "portdecoder.h"

#include <algorithm>
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
#include "emulator/io/mouse/mouse.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/notifications.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/ports/models/portdecoder_atm710.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"
#include "emulator/ports/models/portdecoder_pentagon512.h"
#include "emulator/ports/models/portdecoder_pentagon1024.h"
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
    _mouse = context->pMouse;
    _memory = context->pMemory;
    _screen = context->pScreen;
    _tape = context->pTape;
    _soundManager = context->pSoundManager;
    _logger = context->pModuleLogger;
}

PortDecoder::~PortDecoder()
{
    _portDevices.clear();
    _fullDecodeDevices.clear();
    _fullDecodeLowByteDevices.fill(nullptr);
}
/// endregion </Constructors / Destructors>

/// region <Static methods>

bool PortDecoder::IsModelSupported(MEM_MODEL model)
{
    // Mirrors the switch in GetPortDecoderForModel: true == factory returns
    // a decoder, false == factory throws std::logic_error. Keep both in sync
    // (see the note in portdecoder.h).
    switch (model)
    {
        case MM_SPECTRUM48:
        case MM_PENTAGON:
        case MM_SPECTRUM128:
        case MM_PLUS3:
        case MM_PROFI:
        case MM_SCORP:
        case MM_PROFSCORP:
        case MM_ATM710:
        case MM_ATM3:
            return true;
        default:
            return false;
    }
}

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
            if (ramSize >= 1024)
            {
                // Pentagon 1024K: port #EFF7 for 6-bit bank selection (64 pages)
                result = new PortDecoder_Pentagon1024(context);
            }
            else if (ramSize >= 512)
            {
                // Pentagon 512K: extended bank bits [6:7] of 7FFD (32 pages)
                result = new PortDecoder_Pentagon512(context);
            }
            else
            {
                // Pentagon 128K: standard 3-bit bank selection (8 pages)
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
        case MM_PROFSCORP:
            // ProfROM variant shares the decoder: it branches on
            // mem_model == MM_PROFSCORP for the #7EFD window latch arm
            result = new PortDecoder_Scorpion256(context);
            break;
        case MM_ATM710:
            result = new PortDecoder_ATM710(context);
            break;
        case MM_ATM3:
            result = new PortDecoder_ATM3(context);
            break;
        default:
            // Static method - no _logger member, so MLOGERROR is not available here.
            // Route through the context's module logger with the same gating.
            if (context && context->pModuleLogger)
                context->pModuleLogger->Error(PlatformModulesEnum::MODULE_IO,
                                              PlatformIOSubmodulesEnum::SUBMODULE_IO_GENERIC,
                                              "PortDecoder::GetPortDecoderForModel - Unknown model: %d", model);
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
            bool isHidden = false;
            auto* bp = brk.GetBreakpointById(breakpointID);
            if (bp && (bp->hidden || bp->note == "StepOver" || bp->note == "StepOut" || bp->group == "TemporaryBreakpoints"))
            {
                isHidden = true;
            }

            // Pause emulator (single source of truth)
            emulator.Pause();

            // Broadcast notification - breakpoint triggered (instance-tagged per GDB TDD §6.3)
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            BreakpointTriggeredPayload* payload =
                new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, addr, isHidden);
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
    if (_memory && _memory->_memoryAccessTracker && _context->pCore && _context->pCore->GetZ80())
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
            bool isHidden = false;
            auto* bp = brk.GetBreakpointById(breakpointID);
            if (bp && (bp->hidden || bp->note == "StepOver" || bp->note == "StepOut" || bp->group == "TemporaryBreakpoints"))
            {
                isHidden = true;
            }

            emulator.Pause();
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            BreakpointTriggeredPayload* payload =
                new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, port, isHidden);
            messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);
            emulator.WaitWhilePaused();
        }
    }

    // 2. Port access tracking (caller PC is provided by the CPU: m1_pc of the IN instruction)
    if (_memory && _memory->_memoryAccessTracker)
    {
        _memory->_memoryAccessTracker->TrackPortRead(port, result, pc);
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
            bool isHidden = false;
            auto* bp = brk.GetBreakpointById(breakpointID);
            if (bp && (bp->hidden || bp->note == "StepOver" || bp->note == "StepOut" || bp->group == "TemporaryBreakpoints"))
            {
                isHidden = true;
            }

            emulator.Pause();
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            BreakpointTriggeredPayload* payload =
                new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, addr, isHidden);
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
    if (_memory && _memory->_memoryAccessTracker && _context->pCore && _context->pCore->GetZ80())
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
            bool isHidden = false;
            auto* bp = brk.GetBreakpointById(breakpointID);
            if (bp && (bp->hidden || bp->note == "StepOver" || bp->note == "StepOut" || bp->group == "TemporaryBreakpoints"))
            {
                isHidden = true;
            }

            emulator.Pause();
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            BreakpointTriggeredPayload* payload =
                new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, port, isHidden);
            messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);
            emulator.WaitWhilePaused();
        }
    }

    // 2. Port access tracking (caller PC is provided by the CPU: m1_pc of the OUT instruction)
    if (_memory && _memory->_memoryAccessTracker)
    {
        _memory->_memoryAccessTracker->TrackPortWrite(port, value, pc);
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

    // Scorpion border latch: an OUT the gating arm steered away from the
    // (off-bus) FDC system port drives the border color — reattribute so
    // traces do not blame the FDC for border writes
    if (isOut && disp.wasHandledInline && disp.wasBeta128Gated)
        event.deviceId = PortDeviceId::Border_FF;

    // Full-decode claim override cycles belong to the claiming card, not to
    // "unmapped" - without this the MoonSound traffic the low-byte observer
    // serviced is invisible in traces (attribution pitfall from the
    // moonsound_2.trd diagnosis)
    if (disp.wasFullDecodeClaimed)
        event.deviceId = PortDeviceId::FullDecodeClaim;

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
    if (disp.wasFullDecodeClaimed)
        flags |= PortTraceFlags::kFullDecodeClaimed;
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
            case MM_PROFSCORP:   info.modelName = "Scorpion256Prof"; break;
            default:             info.modelName = "Unknown"; break;
        }
    }

    info.decodeRules = getPortTraceDecodeRules();

    return info;
}

/// endregion </Port trace>

/// region <Port map introspection (GET /ports, P1-5)>

std::vector<PortMapEntry> PortDecoder::getPortMapEntries() const
{
    std::vector<PortMapEntry> entries;

    if (!_context)
        return entries;

    const MEM_MODEL model = _context->config.mem_model;
    const bool scorpion = (model == MM_SCORP || model == MM_PROFSCORP);

    // ---- Universal rows: every decoder on master answers these ----

    // #FE: keyboard / beeper / border / MIC+EAR. The Scorpion decoder qualifies
    // two extra address bits (PortDecoder_Scorpion256::IsPort_FE) - mirrored here.
    if (scorpion)
        entries.push_back({0x00FE, 0x0023, 0x0022, "Keyboard / Beeper / Border / MIC+EAR", nullptr,
                           Tags(PortTag::Keyboard)});
    else
        entries.push_back({0x00FE, 0x0001, 0x0000, "Keyboard / Beeper / Border / MIC+EAR", nullptr,
                           Tags(PortTag::Keyboard)});

    // AY register select / data: A15/A14/A1 qualification, mirrors resolve to the
    // canonical ports (PortDecoder_Spectrum48::DecodePortIn and every other model)
    entries.push_back({0xFFFD, 0xC002, 0xC000, "AY / TurboSound register select (chip select)", nullptr,
                       Tags(PortTag::SoundAy)});
    entries.push_back({0xBFFD, 0xC002, 0x8000, "AY / TurboSound data", nullptr,
                       Tags(PortTag::SoundAy)});

    // ---- Model-specific paging / system latches ----
    switch (model)
    {
        case MM_SPECTRUM128:
            // IsPort_7FFD: A15=0, A2=1, A1=0 (bit 2 qualifier excludes SOUNDRIVE #F1/#F9)
            entries.push_back({0x7FFD, 0x8006, 0x0004, "Memory paging (RAM bank, shadow screen, ROM)", nullptr,
                               Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen, PagingLatch::P7FFD});
            break;
        case MM_PLUS3:
            // IsPort_7FFD / IsPort_1FFD (PortDecoder_Spectrum3)
            entries.push_back({0x7FFD, 0xC002, 0x4000, "Memory paging (RAM bank, shadow screen, ROM)", nullptr,
                               Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen, PagingLatch::P7FFD});
            entries.push_back({0x1FFD, 0xF002, 0x1000, "Disk motor/strobe + special paging", nullptr,
                               Tags(PortTag::Memory) | PortTag::Rom, PagingLatch::P1FFD});
            break;
        case MM_PROFI:
            // IsPort_7FFD / IsPort_DFFD (PortDecoder_Profi): #7FFD = A15=0 & A1=0,
            // #DFFD = A15=1 & A13=0 & A1=0; #DFFD bit 7 selects the 512x240 hi-res
            // mode (Screen::InitVideoMode mode detection)
            entries.push_back({0x7FFD, 0x8002, 0x0000, "Memory paging (RAM bank, shadow screen, ROM14, lock)", nullptr,
                               Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen, PagingLatch::P7FFD});
            entries.push_back({0xDFFD, 0xA002, 0x8000,
                               "Profi extended paging (RAM high bits, SCO, WOROM, CP/M, SCR) + video mode (bit 7)",
                               nullptr, Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen, PagingLatch::PDFFD});
            // Palette write OUT #xx7E (A7=0, A0=0, only while DFFD bit 7 is set); data is in A15:A8
            entries.push_back({0x007E, 0x0081, 0x0000, "Profi palette write (OUT #xx7E, hi-res only)", nullptr,
                               Tags(PortTag::Screen)});
            break;
        case MM_SCORP:
        case MM_PROFSCORP:
            // IsPort_7FFD / IsPort_1FFD / IsPort_7EFD (PortDecoder_Scorpion256)
            entries.push_back({0x7FFD, 0xD027, 0x5025, "Memory paging (incl. extended RAM bits)", nullptr,
                               Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen, PagingLatch::P7FFD});
            entries.push_back({0x1FFD, 0xD027, 0x1025, "Window latch / Shadow Monitor (bit 1)", nullptr,
                               Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen, PagingLatch::P1FFD});
            entries.push_back({0xFF1F, 0xFFFF, 0xFF1F, "Kempston joystick interface",
                               "gives #1F up while TR-DOS selected / Shadow Monitor paged",
                               Tags(PortTag::Joystick)});
            if (model == MM_PROFSCORP)
            {
                entries.push_back({0x7EFD, 0xD127, 0x5025, "ProfROM service-window latch", nullptr,
                                   Tags(PortTag::Memory) | PortTag::Rom | PortTag::System, PagingLatch::P7EFD});
                entries.push_back({0x00BA, 0x00FE, 0x00BA, "SMUC board (EEPROM / RTC / PIC / IDE window)", nullptr,
                                   Tags(PortTag::System)});
            }
            break;
        case MM_PENTAGON:
            // pentagonPortMasksMatches (PortDecoder_Pentagon128/512/1024): paging row keeps
            // the A2=1 qualifier; 512K uses bits [6:7] for 5-bit bank; 1024K adds #EFF7
            if (_context->config.ramsize >= 1024)
            {
                entries.push_back({0x7FFD, 0x8006, 0x0004, "Memory paging (5-bit bank via bits [0:2]+[6:7])", nullptr,
                                   Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen, PagingLatch::P7FFD});
                // #EFF7: bit 2 gates extended memory, bits 0-1,4 control video modes
                entries.push_back({0xEFF7, 0x00FF, 0x00F7, "Pentagon 1024K features (bit2=extmem gate, video modes)", nullptr,
                                   Tags(PortTag::Memory) | PortTag::Screen | PortTag::System, PagingLatch::PEFF7});
            }
            else if (_context->config.ramsize >= 512)
            {
                entries.push_back({0x7FFD, 0x8006, 0x0004, "Memory paging (incl. extended RAM bits [6:7])", nullptr,
                                   Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen, PagingLatch::P7FFD});
            }
            else
            {
                entries.push_back({0x7FFD, 0x8006, 0x0004, "Memory paging (RAM bank, shadow screen, ROM)", nullptr,
                                   Tags(PortTag::Memory) | PortTag::Rom | PortTag::Screen, PagingLatch::P7FFD});
            }
            // Advertise only what dispatch actually wires (SoundManager::
            // attachToPorts): SD=1 registers the full mode-2 quad plus the
            // mode-1 primary quad, CovoxFB=1 alone registers #FB only; with
            // neither flag no device exists
            if (_context->config.sound.sd)
            {
                entries.push_back({0x00FB, 0x00F5, 0x00F1, "SoundDrive quad DAC mode 2 (#F1 L-A, #F3 L-B, #F9 R-A, #FB R-B; #FB doubles as mono Covox)", nullptr,
                                   Tags(PortTag::SoundCovox) | PortTag::SoundSoundDrive});
                // Mode 1 (#0F/#1F/#4F/#5F) aliases the Beta128 FDC's wide
                // mirror decode (bits 0,1=1, bit7=0) - PortDecoder_Pentagon128
                // only routes these to SoundDrive once TR-DOS has released
                // them, so the advertised row states that precedence
                entries.push_back({0x001F, 0x00AF, 0x000F, "SoundDrive quad DAC mode 1 (#0F L-A, #1F L-B, #4F R-A, #5F R-B)",
                                   "!CF_TRDOS (Beta128 FDC not paged in claims these addresses first)",
                                   Tags(PortTag::SoundCovox) | PortTag::SoundSoundDrive});
            }
            else if (_context->config.sound.covoxFB)
            {
                entries.push_back({0x00FB, 0xFFFF, 0x00FB, "Covox (mono #FB)", nullptr,
                                   Tags(PortTag::SoundCovox)});
            }
            break;
        default:
            break;  // MM_SPECTRUM48: no paging / system latches
    }

    // ---- Fitment-conditional peripherals ----

    // Kempston Mouse: standard address decode (mouse design 3.1), rows only while the
    // device is fitted - `present:false` in /mouse/status explains their absence
    if (_mouse && _mouse->IsPresent())
    {
        const char* mouseGate = scorpion
                                    ? "TR-DOS session / Shadow Monitor beta mirrors"
                                    : "!CF_DOSPORTS (TR-DOS ports accessible) + no registered claim";
        entries.push_back({0xFADF, 0x023F, 0x021F, "Kempston mouse buttons (+ wheel)", mouseGate,
                           Tags(PortTag::Mouse)});
        entries.push_back({0xFBDF, 0x023F, 0x021F, "Kempston mouse X axis", mouseGate,
                           Tags(PortTag::Mouse)});
        entries.push_back({0xFFDF, 0x023F, 0x021F, "Kempston mouse Y axis", mouseGate,
                           Tags(PortTag::Mouse)});
    }

    // Beta128 register set while the TR-DOS interface is fitted. On Scorpion the
    // FDC is off the bus outside a TR-DOS session / Shadow Monitor / armed
    // magic-button trigger (DecodePortIn Beta128 gating); elsewhere it answers
    // through the registered device key.
    if (_context->config.trdos_present)
    {
        const char* betaGate = scorpion ? "CF_TRDOS / Shadow Monitor / magic-button trigger"
                               : (model == MM_PROFI) ? "CF_DOSPORTS (DOS latch or CP/M mode)"
                                                     : nullptr;
        // Data registers answer through exact registered device keys (IsBeta128Port /
        // TryBeta128MirrorPort switch on the five low bytes), so the rows are exact
        // matches - which also lets the registered-handler dedupe below absorb them.
        // The system register requires the full low byte 0xFF: partial-decode
        // addresses such as #xxF7 must not reach the FDC (see the Pentagon decode
        // table note on the TR-DOS 5.04T probe reset wedge).
        entries.push_back({0x001F, 0xFFFF, 0x001F, "Beta128 FDC status/command", betaGate,
                           Tags(PortTag::Storage)});
        entries.push_back({0x003F, 0xFFFF, 0x003F, "Beta128 FDC track", betaGate,
                           Tags(PortTag::Storage)});
        entries.push_back({0x005F, 0xFFFF, 0x005F, "Beta128 FDC sector", betaGate,
                           Tags(PortTag::Storage)});
        entries.push_back({0x007F, 0xFFFF, 0x007F, "Beta128 FDC data", betaGate,
                           Tags(PortTag::Storage)});
        entries.push_back({0x00FF, 0x00FF, 0x00FF, "Beta128 system register", betaGate,
                           Tags(PortTag::Storage)});
    }

    // Explicitly registered peripheral devices (RegisterPortHandler) not already
    // covered by a static row's address qualification above
    for (const auto& handler : _portDevices)
    {
        bool covered = false;
        for (const PortMapEntry& entry : entries)
        {
            if ((handler.first & entry.mask) == entry.match)
            {
                covered = true;
                break;
            }
        }
        if (!covered)
        {
            PortTagSet handlerTags = 0;
            auto stored = _portDeviceTags.find(handler.first);
            if (stored != _portDeviceTags.end())
                handlerTags = stored->second;
            entries.push_back({handler.first, 0xFFFF, handler.first, "Registered peripheral device", nullptr,
                              handlerTags});
        }
    }

    return entries;
}

/// region <Tagged port registry (port-tags-paging design §4.2, P1-2 Phase 1)>

std::vector<PortMapEntry> PortDecoder::GetEntriesByTags(PortTagSet categories) const
{
    std::vector<PortMapEntry> result;

    if (categories == 0)
        return result;

    for (const PortMapEntry& entry : getPortMapEntries())
    {
        if ((entry.tags & categories) == categories)
            result.push_back(entry);
    }

    return result;
}

std::vector<PortMapEntry> PortDecoder::GetSoundEntries(PortTag member) const
{
    std::vector<PortMapEntry> result;

    const PortTagSet query = static_cast<PortTagSet>(member) & ~Tags(PortTag::Sound);
    if (query == 0)
        return result;  // bare Sound (or None) is not a member query

    for (const PortMapEntry& entry : getPortMapEntries())
    {
        if ((entry.tags & query) != 0)
            result.push_back(entry);
    }

    return result;
}

bool PortDecoder::HasAnyTaggedPort(PortTagSet categories) const
{
    return !GetEntriesByTags(categories).empty();
}

std::vector<PortMapEntry> PortDecoder::GetPagingLatches(PortTagSet categories) const
{
    std::vector<PortMapEntry> result;

    for (const PortMapEntry& entry : getPortMapEntries())
    {
        if (entry.latch != PagingLatch::None && (entry.tags & categories) == categories)
            result.push_back(entry);
    }

    return result;
}

uint32_t PortDecoder::ReadPagingLatch(PagingLatch latch, const EmulatorState& state)
{
    switch (latch)
    {
        case PagingLatch::P7FFD:  return state.p7FFD;
        case PagingLatch::P1FFD:  return state.p1FFD;
        case PagingLatch::PDFFD:  return state.pDFFD;
        case PagingLatch::PFDFD:  return state.pFDFD;
        case PagingLatch::P7EFD:  return state.p7EFD;
        case PagingLatch::PEFF7:  return state.pEFF7;
        case PagingLatch::PFF77:  return state.pFF77;
        case PagingLatch::AFE:    return state.aFE;
        case PagingLatch::AFB:    return state.aFB;
        case PagingLatch::PFFF7Window0: return state.pFFF7[0];
        case PagingLatch::PFFF7Window1: return state.pFFF7[1];
        case PagingLatch::PFFF7Window2: return state.pFFF7[2];
        case PagingLatch::PFFF7Window3: return state.pFFF7[3];
        // Reserved atm-branch / TSConf members: fields exist but no decoder on
        // master ever binds them yet, so there is nothing truthful to report
        case PagingLatch::PBD:
        case PagingLatch::PTS:
        case PagingLatch::PMEM:
        case PagingLatch::None:
        default:
            return 0;
    }
}

/// endregion </Tagged port registry>

/// region <Tag / latch serialization (single source for every automation surface)>

std::vector<std::string> PortTagSetToStrings(PortTagSet tags)
{
    std::vector<std::string> result;

    if (tags == 0)
        return result;

    if (tags & PortTag::Keyboard)
        result.push_back("keyboard");
    if (tags & PortTag::Memory)
        result.push_back("memory");
    if (tags & PortTag::Rom)
        result.push_back("rom");
    if (tags & PortTag::Screen)
        result.push_back("screen");
    if (tags & PortTag::Storage)
        result.push_back("storage");
    if (tags & PortTag::Mouse)
        result.push_back("mouse");
    if (tags & PortTag::Joystick)
        result.push_back("joystick");
    if (tags & PortTag::System)
        result.push_back("system");

    // Sound members embed the Sound category bit; a row can carry several of
    // them (Pentagon #FB answers both covox and SoundDrive), so each present
    // member is reported - a plain Sound bit without any member degrades to
    // the bare "sound" name
    if (tags & PortTag::Sound)
    {
        bool memberReported = false;
        if ((tags & PortTag::SoundAy) == static_cast<PortTagSet>(PortTag::SoundAy))
        {
            result.push_back("sound_ay");
            memberReported = true;
        }
        if ((tags & PortTag::SoundCovox) == static_cast<PortTagSet>(PortTag::SoundCovox))
        {
            result.push_back("sound_covox");
            memberReported = true;
        }
        if ((tags & PortTag::SoundSoundDrive) == static_cast<PortTagSet>(PortTag::SoundSoundDrive))
        {
            result.push_back("sound_sounddrive");
            memberReported = true;
        }
        if ((tags & PortTag::SoundGs) == static_cast<PortTagSet>(PortTag::SoundGs))
        {
            result.push_back("sound_gs");
            memberReported = true;
        }
        if ((tags & PortTag::SoundMoonsound) == static_cast<PortTagSet>(PortTag::SoundMoonsound))
        {
            result.push_back("sound_moonsound");
            memberReported = true;
        }
        if (!memberReported)
            result.push_back("sound");
    }

    return result;
}

const char* PagingLatchToString(PagingLatch latch)
{
    switch (latch)
    {
        case PagingLatch::P7FFD:  return "p7FFD";
        case PagingLatch::P1FFD:  return "p1FFD";
        case PagingLatch::PDFFD:  return "pDFFD";
        case PagingLatch::PFDFD:  return "pFDFD";
        case PagingLatch::P7EFD:  return "p7EFD";
        case PagingLatch::PEFF7:  return "pEFF7";
        case PagingLatch::PFF77:  return "pFF77";
        case PagingLatch::AFE:    return "aFE";
        case PagingLatch::AFB:    return "aFB";
        case PagingLatch::PFFF7Window0: return "pFFF7_w0";
        case PagingLatch::PFFF7Window1: return "pFFF7_w1";
        case PagingLatch::PFFF7Window2: return "pFFF7_w2";
        case PagingLatch::PFFF7Window3: return "pFFF7_w3";
        case PagingLatch::PBD:    return "pBD";
        case PagingLatch::PTS:    return "pTS";
        case PagingLatch::PMEM:   return "pMEM";
        case PagingLatch::None:
        default:
            return nullptr;
    }
}

std::vector<DecodedLatchField> DecodePagingLatch(PagingLatch latch, uint32_t value, MEM_MODEL model, uint32_t ramSizeKB)
{
    std::vector<DecodedLatchField> result;

    auto intField = [&result](const char* key, int intValue)
    {
        DecodedLatchField field;
        field.key = key;
        field.intValue = intValue;
        result.push_back(field);
    };
    auto boolField = [&result](const char* key, bool boolValue)
    {
        DecodedLatchField field;
        field.key = key;
        field.isBool = true;
        field.boolValue = boolValue;
        result.push_back(field);
    };

    switch (latch)
    {
        case PagingLatch::P7FFD:
        {
            // Pentagon 512K is the only master decoder folding bits [6:7] into
            // the bank index (PortDecoder_Pentagon512::switchRAMPage, 5-bit);
            // every other banked model - Scorpion included, whose #7FFD D6/D7
            // are unused (extensions live in #1FFD) - decodes bits [0:2] only
            int ramBank;
            if (model == MM_PENTAGON && ramSizeKB == 512)
            {
                ramBank = (value & 0x07) | ((value & 0xC0) >> 3);  // 5-bit bank
            }
            else
            {
                ramBank = static_cast<int>(value & 0x07);  // Standard 3-bit bank
            }
            intField("ram_bank", ramBank);
            boolField("shadow_screen", (value & 0x08) != 0);
            intField("rom_select", (value & 0x10) ? 1 : 0);
            boolField("locked", (value & 0x20) != 0);
            break;
        }
        case PagingLatch::P1FFD:
            if (model == MM_SCORP || model == MM_PROFSCORP)
            {
                boolField("shadow_monitor_paged", (value & 0x02) != 0);
            }
            else if (model == MM_PLUS3)
            {
                intField("special_paging", static_cast<int>(value & 0x07));
                boolField("disk_motor", (value & 0x08) != 0);
            }
            break;
        case PagingLatch::PDFFD:
            intField("extended_ram_bank", static_cast<int>(value & 0x07));
            boolField("sco", (value & 0x08) != 0);
            boolField("worom", (value & 0x10) != 0);
            boolField("cpm", (value & 0x20) != 0);
            boolField("scr", (value & 0x40) != 0);
            boolField("video_512x240", (value & 0x80) != 0);
            break;
        case PagingLatch::PEFF7:
            // Pentagon 1024K #EFF7 bit assignments (Born Dead #10):
            // Bit 0: a4b (attribute per byte), Bit 1: 512x192 mode
            // Bit 2: memory above 128K (0=present, 1=absent)
            // Bit 3: read-only cache, Bit 4: GigaScreen, Bit 7: Gluk CMOS
            if (model == MM_PENTAGON && ramSizeKB >= 1024)
            {
                boolField("ext_memory_present", (value & 0x04) == 0);
                boolField("a4b_mode", (value & 0x01) != 0);
                boolField("mode_512x192", (value & 0x02) != 0);
                boolField("gigascreen", (value & 0x10) != 0);
                boolField("gluk_cmos", (value & 0x80) != 0);
            }
            else
            {
                intField("value", static_cast<int>(value));
            }
            break;
        case PagingLatch::PFF77:
            intField("video_mode", static_cast<int>((value >> 1) & 0x07));
            intField("ram_page", static_cast<int>((value >> 4) & 0x0F));
            intField("rom_page", static_cast<int>(value & 0x07));
            break;
        default:
            break;
    }

    return result;
}

/// endregion </Tag / latch serialization>

void PortDecoder::GetMouseRoutingState(bool& decoded, std::string& note) const
{
    decoded = false;

    if (!_mouse || !_mouse->IsPresent())
    {
        note = "mouse not fitted for this config ([INPUT] Mouse=)";
        return;
    }

    if (_state && (_state->flags & CF_DOSPORTS))
    {
        note = "TR-DOS ports accessible (CF_DOSPORTS): only Beta Disk operations answer";
        return;
    }

    // Canonical buttons port; ownership by an explicitly registered peripheral
    // keeps the address away from the mouse (Default_IsPort_KempstonMouse)
    static const uint16_t probePort = 0xFADF;
    if (key_exists(_portDevices, probePort))
    {
        note = StringHelper::Format("port #%04X claimed by a registered peripheral", probePort);
        return;
    }

    uint8_t unusedRegister = 0;
    if (IsPort_KempstonMouse(probePort, unusedRegister))  // virtual: model deviations honored
    {
        decoded = true;
        note = "decoded (standard Kempston address decode)";
    }
    else
    {
        // The virtual gate refused the probe after the base checks passed:
        // model-specific gating (Scorpion TR-DOS session / magic-button trigger,
        // Shadow Monitor beta mirror claim)
        note = "hidden by model-specific decoder gating (TR-DOS session / Shadow Monitor)";
    }
}

/// endregion </Port map introspection>


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
    uint8_t inputEARSignal = _tape->handlePortIn(port) & maskEAR;
    result |= inputEARSignal;

    return result;
}

/// Standard Kempston Mouse decode (Kempston Mouse design §3.1), MiSTer ZX-Spectrum mouse.v /
/// kemp_sel equations:
///   qualify:  A5-A0 = 011111 (#1F, #5F, #9F, #DF low bytes; A7/A6 not decoded), A9 = 1
///   select :  A8 = 0          -> buttons (A10 don't-care: #FADF and #FEDF both answer)
///             A8 = 1, A10 = 0 -> X
///             A8 = 1, A10 = 1 -> Y
/// A15-A11 are mirrors.
bool PortDecoder::Standard_IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister)
{
    if ((port & 0x0200) == 0 || (port & 0x003F) != 0x001F)
        return false;

    if ((port & 0x0100) == 0)
        outRegister = 0;  // buttons (+ wheel)
    else if ((port & 0x0400) == 0)
        outRegister = 1;  // X
    else
        outRegister = 2;  // Y
    return true;
}

/// The mouse answers this address on this machine right now: fitted (config + feature), TR-DOS
/// not selected (common rule: while TR-DOS is selected only Beta Disk operations happen, nothing
/// else answers on any address - CF_DOSPORTS), no registered peripheral owning the exact address
/// (explicit devices keep their ports), and the standard decode matches. Model decoders call this
/// after their own higher-priority arms (keyboard, AY, FDC, joystick).
bool PortDecoder::Default_IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister) const
{
    if (!_mouse || !_mouse->IsPresent())
        return false;
    if (_state && (_state->flags & CF_DOSPORTS))
        return false;
    if (key_exists(_portDevices, port))
        return false;
    return Standard_IsPort_KempstonMouse(port, outRegister);
}

/// Default mouse-port predicate: the gated standard decode. Scorpion overrides this
/// with its TR-DOS / Shadow Monitor deviations; models without a deviation inherit it
bool PortDecoder::IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister) const
{
    return Default_IsPort_KempstonMouse(port, outRegister);
}

uint8_t PortDecoder::Default_Port_KempstonMouse_In(uint16_t port, [[maybe_unused]] uint16_t pc)
{
    uint8_t selectRegister = 0;
    if (_mouse && Standard_IsPort_KempstonMouse(port, selectRegister))
        return _mouse->ReadRegister(selectRegister);
    return 0xFF;
}

/// Default implementation for 'out (#FE)'
/// Bits [0:2]  - Border color
/// Bit  [3]    - MIC output bit
/// Bit  [4]    - EAR output bit
/// See: https://worldofspectrum.org/faq/reference/48kreference.htm
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
    _soundManager->getBeeper().handlePortOut(value, _context->emulatorState.AudioTstate(tState));

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

/// Whether a decoded port value belongs to the Beta128 FDC register set
/// (#1F status/cmd, #3F track, #5F sector, #7F data, #FF system) — hoisted
/// from PortDecoder_Pentagon128 so the Scorpion decoder shares it
bool PortDecoder::IsBeta128Port(uint16_t decodedPort) const
{
    switch (decodedPort)
    {
        case 0x001F:
        case 0x003F:
        case 0x005F:
        case 0x007F:
        case 0x00FF:
            return true;
        default:
            return false;
    }
}

/// Whether a decoded port value belongs to the General Sound host mailbox
/// (#33/#B3/#BB) — see the header doc comment for why model decoders gate
/// these on card presence.
bool PortDecoder::IsGsPort(uint16_t decodedPort)
{
    switch (decodedPort)
    {
        case 0x0033:
        case 0x00B3:
        case 0x00BB:
            return true;
        default:
            return false;
    }
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
bool PortDecoder::RegisterPortHandler(uint16_t port, PortDevice* device, PortTagSet tags)
{
    bool result = false;

    if (device)
    {
        if (!key_exists(_portDevices, port))
        {
            _portDevices.insert({port, device});
            _portDeviceTags.insert_or_assign(port, tags);
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
        _portDeviceTags.erase(port);
    }
}

bool PortDecoder::RegisterFullDecodePort(uint16_t port, PortDevice* device)
{
    bool result = false;

    if (device)
    {
        if (!key_exists(_fullDecodeDevices, port))
        {
            _fullDecodeDevices.insert({port, device});
            result = true;
        }
        else
        {
            MLOGWARNING("PortDecoder::RegisterFullDecodePort - observer for port: #%04X already registered", port);
        }
    }

    return result;
}

void PortDecoder::UnregisterFullDecodePort(uint16_t port, PortDevice* device)
{
    auto it = _fullDecodeDevices.find(port);
    if (it != _fullDecodeDevices.end() && it->second == device)
    {
        _fullDecodeDevices.erase(it);
    }
}

bool PortDecoder::RegisterFullDecodeLowBytePort(uint8_t port, PortDevice* device)
{
    bool result = false;

    if (device)
    {
        if (!_fullDecodeLowByteDevices[port])
        {
            _fullDecodeLowByteDevices[port] = device;
            result = true;
        }
        else
        {
            MLOGWARNING("PortDecoder::RegisterFullDecodeLowBytePort - observer for port low byte: #%02X already registered", port);
        }
    }

    return result;
}

void PortDecoder::UnregisterFullDecodeLowBytePort(uint8_t port, PortDevice* device)
{
    if (_fullDecodeLowByteDevices[port] == device)
    {
        _fullDecodeLowByteDevices[port] = nullptr;
    }
}

/// Model-decode claim override: see portdecoder.h. Called from each model's
/// DecodePortIn/Out right after decodePortEx, before any inline handler runs -
/// the claiming observer was already serviced by the Z80 I/O funnel tap.
bool PortDecoder::OverrideDecodeForFullDecodeClaim(uint16_t rawPort, uint16_t& decodedPort,
                                                   PortDecodeDisposition& disp, bool isRead)
{
    if (decodedPort == 0x0000)
        return false;

    PortDevice* observer = _fullDecodeLowByteDevices[rawPort & 0xFF];
    if (observer == nullptr)
        return false;

    // A read only stands the model decode down when the card actually drives
    // this port's data (portDeviceClaimsRead) - a registered-but-silent low
    // byte (e.g. a write-only address latch) never asserts its output buffer,
    // so the real underlying device (ULA/AY/FDC) must answer exactly as if
    // the card were not attached. Writes have no such ambiguity: every OUT to
    // a registered low byte stands the model decode down unconditionally.
    if (isRead && !observer->portDeviceClaimsRead(rawPort))
        return false;

    // Beta-128 registers keep their TR-DOS session arbitration (R6): the FDC
    // owns #1F/#3F/#5F/#7F/#FF while a DOS session is open, card or no card
    if (IsBeta128Port(decodedPort))
        return false;

    decodedPort = 0x0000;
    disp.decodedPort = 0x0000;
    disp.decodeRuleIndex = PortTraceRule::kNoMatch;
    disp.wasDecoded = false;
    disp.wasHandledInline = false;
    disp.wasFullDecodeClaimed = true;

    return true;
}

std::vector<PortDecodeClash> PortDecoder::FindFullDecodeClashes() const
{
    std::vector<PortDecodeClash> result;

    const std::vector<PortTraceDecodeRule> rules = getPortTraceDecodeRules();

    for (size_t i = 0; i < rules.size(); i++)
    {
        const PortTraceDecodeRule& rule = rules[i];

        // A low byte L overlaps the rule when its constrained bits satisfy the
        // rule's low-byte half (the high half is always satisfiable because a
        // low-byte claim spans every high-byte alias)
        const uint16_t ruleMaskLow = rule.mask & 0x00FF;
        const uint16_t ruleMatchLow = rule.match & 0x00FF;

        for (uint16_t low = 0; low <= 0xFF; low++)
        {
            const PortDevice* device = _fullDecodeLowByteDevices[low];
            if (device == nullptr || (low & ruleMaskLow) != ruleMatchLow)
                continue;

            PortDecodeClash clash;
            clash.ruleIndex = static_cast<uint8_t>(i);
            clash.ruleMask = rule.mask;
            clash.ruleMatch = rule.match;
            clash.rulePort = rule.port;
            clash.claimLowByte = static_cast<uint8_t>(low);
            clash.device = _fullDecodeLowByteDevices[low];
            // Required high bits come from the match value (don't-care bits stay 0)
            clash.sampleAddress = static_cast<uint16_t>((rule.match & 0xFF00) | low);
            clash.fdcProtected = IsBeta128Port(rule.port);
            result.push_back(clash);
        }
    }

    return result;
}

std::vector<PortDecodeRuleOverride> PortDecoder::FindFullDecodeRuleResolutions() const
{
    std::vector<PortDecodeRuleOverride> result;

    const std::vector<PortTraceDecodeRule> rules = getPortTraceDecodeRules();
    const std::vector<PortDecodeClash> clashes = FindFullDecodeClashes();

    for (size_t i = 0; i < rules.size(); i++)
    {
        PortDecodeRuleOverride resolution;
        resolution.ruleIndex = static_cast<uint8_t>(i);
        resolution.rulePort = rules[i].port;

        for (const PortDecodeClash& clash : clashes)
        {
            if (clash.ruleIndex == i)
                resolution.clashingClaims.push_back(clash.claimLowByte);
        }

        if (resolution.clashingClaims.empty())
            continue;

        // FDC-protected rules resolve through their own session gate, never
        // through mask tightening (the Beta-128 decode is deliberately loose)
        bool protectedRule = false;
        for (const PortDecodeClash& clash : clashes)
        {
            if (clash.ruleIndex == i && clash.fdcProtected)
                protectedRule = true;
        }

        if (!protectedRule)
        {
            // Minimal separating bits: low address bits the rule does not
            // decode yet, where the canonical port differs from at least one
            // claim. A subset S separates when every claim low byte disagrees
            // with the canonical low byte in at least one bit of S - adding S
            // to the mask (with canonical bit values) then keeps the canonical
            // port decoding while excluding the whole claim set.
            const uint8_t canonicalLow = static_cast<uint8_t>(rules[i].port & 0x00FF);
            const uint8_t ruleMaskLow = static_cast<uint8_t>(rules[i].mask & 0x00FF);

            uint8_t candidates = 0;
            for (uint8_t bit = 0; bit < 8; bit++)
            {
                const uint8_t mask = static_cast<uint8_t>(1u << bit);
                if (ruleMaskLow & mask)
                    continue;

                for (uint8_t claim : resolution.clashingClaims)
                {
                    if ((claim ^ canonicalLow) & mask)
                    {
                        candidates |= mask;
                        break;
                    }
                }
            }

            // Smallest separating subset of at most 3 candidate bits
            constexpr uint8_t kMaxSeparatingBits = 3;
            uint8_t bestSubset = 0;
            uint8_t bestPopcount = 0;
            bool found = false;

            for (uint8_t subset = 1; subset != 0; subset++)
            {
                if (static_cast<uint8_t>(subset & ~candidates) != 0)
                    continue;

                // Portable popcount (MSVC has no __builtin_popcount)
                uint8_t popcount = 0;
                for (uint8_t bits = subset; bits != 0; bits >>= 1)
                    popcount = static_cast<uint8_t>(popcount + (bits & 1));
                if (popcount > kMaxSeparatingBits || (found && popcount >= bestPopcount))
                    continue;

                bool separates = true;
                for (uint8_t claim : resolution.clashingClaims)
                {
                    if ((claim & subset) == (canonicalLow & subset))
                    {
                        separates = false;
                        break;
                    }
                }

                if (separates)
                {
                    bestSubset = subset;
                    bestPopcount = popcount;
                    found = true;
                }
            }

            if (found)
            {
                resolution.separatingMask = bestSubset;
                resolution.separatingMatch = static_cast<uint8_t>(canonicalLow & bestSubset);
                resolution.separatingBitCount = bestPopcount;
            }
        }

        resolution.precedenceRequired = !protectedRule && resolution.separatingBitCount == 0;
        // FDC-protected rules also report precedence - the runtime override
        // keeps the FDC in charge instead of the claiming card
        if (protectedRule)
            resolution.precedenceRequired = true;

        result.push_back(resolution);
    }

    return result;
}

/// Z80 OUT tap: forward the RAW port write to the full-decode observer (if any).
/// Called from Z80::out() before the model decode - the observer sees the cycle
/// no matter which device the model decode attributes it to.
void PortDecoder::NotifyFullDecodeOut(uint16_t port, uint8_t value)
{
    auto it = _fullDecodeDevices.find(port);
    if (it != _fullDecodeDevices.end() && it->second)
    {
        it->second->portDeviceOutMethod(port, value);
        return;
    }

    // Low-byte CPLD decode: every high-byte alias of a registered low byte
    // reaches the card (guest `out (n),a` forms put A in the high byte).
    if (PortDevice* lowByteObserver = _fullDecodeLowByteDevices[port & 0xFF])
    {
        lowByteObserver->portDeviceOutMethod(port, value);
    }
}

/// Z80 IN tap: query the full-decode observer for the RAW port read.
/// Returns the observer's bus value (0xFF when none) and sets handled=true
/// when an observer drives the bus, claimsBus=true when it claims the read
/// over a model-decoded device (armed card, portDeviceClaimsRead). Z80::in()
/// applies the value with legacy-device priority unless the observer claims
/// the bus, and suppresses the floating bus override for handled ports.
uint8_t PortDecoder::NotifyFullDecodeIn(uint16_t port, bool& handled, bool& claimsBus)
{
    uint8_t result = 0xFF;
    handled = false;
    claimsBus = false;

    auto it = _fullDecodeDevices.find(port);
    if (it != _fullDecodeDevices.end() && it->second)
    {
        result = it->second->portDeviceInMethod(port);
        handled = true;
        claimsBus = it->second->portDeviceClaimsRead(port);
    }
    else if (PortDevice* lowByteObserver = _fullDecodeLowByteDevices[port & 0xFF])
    {
        // Low-byte CPLD decode: every high-byte alias of a registered low
        // byte reaches the card, with the same claim semantics as above.
        result = lowByteObserver->portDeviceInMethod(port);
        handled = true;
        claimsBus = lowByteObserver->portDeviceClaimsRead(port);
    }

    // Cache the observer's bus value for the claim override inside
    // DecodePortIn (single read of a stateful card register - see
    // _lastFullDecodeInValue)
    _lastFullDecodeInPort = port;
    _lastFullDecodeInValue = result;

    return result;
}

bool PortDecoder::RegisterSelfDecodingDevice(PortDevice* device)
{
    bool result = false;

    if (device && std::find(_selfDecodingDevices.begin(), _selfDecodingDevices.end(), device) == _selfDecodingDevices.end())
    {
        _selfDecodingDevices.push_back(device);
        result = true;
    }

    return result;
}

void PortDecoder::UnregisterSelfDecodingDevice(PortDevice* device)
{
    auto it = std::find(_selfDecodingDevices.begin(), _selfDecodingDevices.end(), device);
    if (it != _selfDecodingDevices.end())
    {
        _selfDecodingDevices.erase(it);
    }
}

bool PortDecoder::DispatchSelfDecodingOut(uint16_t rawPort, uint8_t value)
{
    for (PortDevice* device : _selfDecodingDevices)
    {
        if (device->tryClaimOut(rawPort, value))
            return true;
    }

    return false;
}

bool PortDecoder::DispatchSelfDecodingIn(uint16_t rawPort, uint8_t& outValue)
{
    for (PortDevice* device : _selfDecodingDevices)
    {
        if (device->tryClaimIn(rawPort, outValue))
            return true;
    }

    return false;
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