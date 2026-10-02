#include "core.h"
#include "emulator/io/network/networkmanager.h"

#include <algorithm>
#include <array>
#include <cassert>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "emulator/io/fdc/diskautostart.h"
#include "emulator/io/fdc/diskfastload.h"
#include "emulator/io/fdc/upd765.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/tape/tapefastload.h"
#include "emulator/io/tape/tapeturbocontroller.h"
#include "emulator/memory/scorpion/scorpionmemory.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/memory/tsconf/tsconfmemory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/videocontroller.h"
#include "emulator/video/zx/screenzx.h"
#include "3rdparty/message-center/messagecenter.h"
#include "stdafx.h"

// Instantiate Core tables as static (only one instance per process)
CPUTables Core::_cpuTables;

/// region <Constructors / Destructors>

Core::Core(EmulatorContext* context)
{
    _context = context;
    _state = &_context->emulatorState;
    _config = &_context->config;
    _logger = _context->pModuleLogger;
}

Core::~Core()
{
    Release();

    _context = nullptr;
}

/// endregion </Constructors / Destructors>

/// region <Initialization>
bool Core::Init()
{
    bool result = false;

    // Instantiation sequence
    // Step 1       - Memory()

    // Step N - 1   - Z80()
    // Step N       - PortDecoder()

    // Register itself in context
    _context->pCore = this;

    /// region <Frequency>

    uint32_t baseFrequency = 3'500'000;  // Make 3.5MHz by default

    // See: https://k1.spdns.de/Develop/Projects/zxsp-osx/Info/nocash%20Sinclair%20ZX%20Specs.html
    // See: https://worldofspectrum.org/faq/reference/128kreference.htm
    switch (_context->config.mem_model)
    {
        case MM_SPECTRUM48:
            baseFrequency = 3'500'000;
            break;
        case MM_SPECTRUM128:
        case MM_PLUS2:
        case MM_PLUS2A:
        case MM_PLUS3:
            // The Sinclair / Amstrad 128 machines share the 17.7345 MHz / 5 clock
            baseFrequency = 3'546'900;
            break;
        default:
            baseFrequency = 3'500'000;
            break;
    }

    _state->base_z80_frequency = baseFrequency;
    _state->current_z80_frequency = baseFrequency;
    _state->current_z80_frequency_multiplier = 1;
    _state->next_z80_frequency_multiplier = 1;  // Initialize queued multiplier
    _state->scorpion_turbo = 0;                 // Turbo flip-flop cleared at power-on (hardware-reference 13)
    _state->hw_turbo_ratio = 1;                 // No hardware turbo engaged at power-on (model-neutral)
    _state->hw_turbo_ratio_applied = 1;
    _state->scorpionDosTrigger = 0;            // Magic-button DOS trigger cleared at power-on (hardware-reference §9)

    // Initialize speed multiplier from configuration
    if (_config->speed_multiplier > 0 && _config->speed_multiplier <= 16)
    {
        _state->current_z80_frequency_multiplier = _config->speed_multiplier;
        _state->next_z80_frequency_multiplier = _config->speed_multiplier;
        _state->current_z80_frequency = baseFrequency * _config->speed_multiplier;
    }

    /// endregion </Frequency>

    /// region <Memory>

    // Create memory subsystem (allocates all RAM/ROM regions). Scorpion
    // models get the derived class that owns their latch-to-bank translation
    // and ProfROM bus-cycle silicon, TS-Conf the one that maps its windows
    // from TsConfState and models its CPU cache; everything else stays on the
    // generic one
    if (_config->mem_model == MM_SCORP || _config->mem_model == MM_PROFSCORP)
        _memory = new ScorpionMemory(_context);
    else if (_config->mem_model == MM_TSL)
        _memory = new TsConfMemory(_context);
    else if (_config->mem_model == MM_SPRINTER)
        _memory = new SprinterMemory(_context);
    else
        _memory = new Memory(_context);
    if (_memory)
    {
        _context->pMemory = _memory;

        result = true;
    }

    /// endregion </Memory>

    /// region <ROM>

    if (result)
    {
        result = false;

        // Instantiate ROM implementation
        _rom = new ROM(_context);
        if (_rom)
        {
            result = true;
        }
    }

    /// endregion </ROM>

    /// region <Keyboard>

    if (result)
    {
        result = false;

        // Instantiate Keyboard implementation
        _keyboard = new Keyboard(_context);
        if (_keyboard)
        {
            _context->pKeyboard = _keyboard;

            result = true;
        }
    }

    /// endregion </Keyboard>

    /// region <Mouse>

    if (result)
    {
        result = false;

        _mouse = new Mouse(_context);
        if (_mouse)
        {
            _context->pMouse = _mouse;
            result = true;
        }
    }

    /// endregion </Mouse>

    /// region <Joystick>

    if (result)
    {
        result = false;

        _joystick = new Joystick(_context);
        if (_joystick)
        {
            _context->pJoystick = _joystick;
            result = true;
        }
    }

    /// endregion </Joystick>

    /// region <Tape>

    if (result)
    {
        result = false;

        // Instantiate Tape interface implementation
        _tape = new Tape(_context);
        if (_tape)
        {
            _context->pTape = _tape;

            result = true;
        }
    }

    /// endregion </Tape>

    /// region <Fast tape loading>

    if (result)
    {
        result = false;

        // Instantiate fast tape loading trap (wraps the LD-BYTES ROM entry)
        _tapeFastLoad = new TapeFastLoad(_context, *_tape);
        if (_tapeFastLoad)
        {
            _context->pTapeFastLoad = _tapeFastLoad;

            result = true;
        }
    }

    /// endregion </Fast tape loading>

    /// region <Turbo tape loading>

    if (result)
    {
        result = false;

        // Instantiate turbo tape loading controller (auto-warp while the
        // signal path plays — design 2026-09-04-turbo-tape-loading §6.1)
        _tapeTurboController = new TapeTurboController(_context, *_tape);
        if (_tapeTurboController)
        {
            _context->pTapeTurboController = _tapeTurboController;

            result = true;
        }
    }

    /// endregion </Turbo tape loading>

    /// region <BetaDisk128 Interface>

    if (result)
    {
        result = false;

        // Instantiate BDI interface implementation
        _betaDisk = new WD1793(_context);
        if (_betaDisk)
        {
            _context->pBetaDisk = _betaDisk;

            result = true;
        }
    }

    /// endregion </BetaDisk128 Interface>

    /// region <+3 floppy controller>

    // The uPD765A drives the FDDs the WD1793 created (coreState.diskDrives), so it comes after it
    if (result && _context->config.mem_model == MM_PLUS3)
    {
        _upd765 = new UPD765(_context);
        _context->pUPD765 = _upd765;

        // The +3's 3" drives are 40-track mechanics (48 tpi): a +3 disk's
        // cylinder n is head position n, and the head stops at cylinder 42
        for (FDD* drive : {_context->coreState.diskDrives[0], _context->coreState.diskDrives[1]})
        {
            if (drive)
                drive->setDriveCylinders(40);
        }
    }

    /// endregion </+3 floppy controller>

    /// region <Fast disk loading>

    if (result)
    {
        result = false;

        // Instantiate fast disk loading trap
        _diskFastLoad = new DiskFastLoad(_context);
        if (_diskFastLoad)
        {
            _context->pDiskFastLoad = _diskFastLoad;

            // TR-DOS disk autostart service
            _diskAutostart = new DiskAutostart(_context);
            _context->pDiskAutostart = _diskAutostart;

            result = true;
        }
    }

    /// endregion </Fast disk loading>

    /// region <Sound manager>

    if (result)
    {
        result = false;

        // Instantiate sound manager
        _sound = new SoundManager(_context);

        if (_sound)
        {
            _context->pSoundManager = _sound;

            result = true;
        }
    }

    /// endregion </Sound manager>

    /// region <Recording manager>

#ifdef ENABLE_RECORDING
    if (result)
    {
        result = false;

        // Instantiate recording manager
        _recordingManager = new RecordingManager(_context);

        if (_recordingManager)
        {
            _context->pRecordingManager = _recordingManager;
            _recordingManager->Init();

            // Recordings must be stamped with the resolved core audio rate
            // (multirate plan phase 6) - never the 44100 default
            _recordingManager->SetAudioSampleRate(static_cast<uint32_t>(_sound->getCoreRate()));

            result = true;
        }
    }
#endif

    /// endregion </Recording manager>

    /// region <IDE>

    // The board from [HDD] Scheme; its unit slots register with the media manager
    if (result)
    {
        _ide = new IdeController(_context);
        _context->pIdeController = _ide;
    }

    /// endregion </IDE>

    /// region <Z80>

    if (result)
    {
        result = false;

        // Create main Core core instance (Z80)
        // Note: Z80 must be created before Video controller so that Screen
        // can capture the Z80 pointer during construction
        _z80 = new Z80(_context);
        if (_z80)
        {
            SelectMemoryInterface();  // Fast until the debugger / contention say otherwise

            result = true;
        }
    }

    /// endregion </Z80>

    /// region <ULA Contention>

    if (result)
    {
        result = false;

        // Create standalone ULA contention component
        // Must be created after Z80 (needs cpu pointer) and Memory
        _ulaContention = new UlaContention();
        if (_ulaContention)
        {
            _ulaContention->SetDependencies(_z80, _memory, _context);
            _context->pUlaContention = _ulaContention;
            _memory->SetContentionDependencies(_z80, _ulaContention);

            result = true;
        }
    }

    /// endregion </ULA Contention>

    /// region <Video controller>

    if (result)
    {
        result = false;

        // The renderer of the model's family (VideoController::CreateScreen), in M_ZX48 at start
        _screen = VideoController::CreateScreen(_config->mem_model, _context);
        if (_screen)
        {
            _context->pScreen = _screen;

            result = true;
        }
    }

    /// endregion </Video controller>

    /// region <Ports decoder>

    if (result)
    {
        result = false;

        // Instantiate ports decoder
        // As ports decoder should know and control all peripherals - instantiate it as last step
        MEM_MODEL model = _context->config.mem_model;
        _ports = new Ports(_context);
        if (_ports)
        {
            _portDecoder = PortDecoder::GetPortDecoderForModel(model, _context);
            if (_portDecoder)
            {
                _context->pPortDecoder = _portDecoder;
                _state->ttd_clock_units = _portDecoder->TtdClockUnits();

                // The board decides how the WD1793 is clocked ([Beta128] TurboVG= can override it)
                if (_betaDisk)
                {
                    _betaDisk->SetClockPolicy(WD1793::ResolveClockPolicy(_portDecoder->DefaultFdcClockPolicy(),
                                                                         _context->config.fdcTurboVg));
                }

                // Prime the porttrace feature cache: the decoder is created after
                // FeatureManager loaded features.ini, so a persisted porttrace=on
                // state would otherwise not take effect until the next toggle
                _portDecoder->UpdateFeatureCache();

                result = true;
            }
            else
            {
                LOGERROR("Core::Core - Unable to create port decoder for model %d", model);
                throw std::logic_error("No port decoder");
            }
        }
    }

    // endregion </Ports decoder>

    /// region <Activate IO devices>
    if (_sound)
    {
        _sound->attachToPorts();
    }

    if (_betaDisk)
    {
        _betaDisk->attachToPorts();
    }

    // Network adapters: the card claims its ports on the decoder created above
    if (result)
    {
        _networkManager = new NetworkManager(_context);
        _networkManager->ApplyConfiguration();
    }

    /// endregion </Activate IO devices>

    // Release all allocated object in case of at least single failure
    if (!result)
    {
        Release();
    }

    return result;
}

void Core::Release()
{
    // Unregister itself from context
    _context->pCore = nullptr;

    // Network adapters first: the card releases its port claim while the decoder exists
    delete _networkManager;
    _networkManager = nullptr;
    _context->pPortDecoder = nullptr;

    _context->pSoundManager = nullptr;
    if (_sound != nullptr)
    {
        // Detach sound chips from the PortDecoder
        _sound->detachFromPorts();

        delete _sound;
        _sound = nullptr;
    }

#ifdef ENABLE_RECORDING
    _context->pRecordingManager = nullptr;
    if (_recordingManager != nullptr)
    {
        delete _recordingManager;
        _recordingManager = nullptr;
    }
#endif

    _context->pScreen = nullptr;
    if (_screen != nullptr)
    {
        delete _screen;
        _screen = nullptr;
    }

    // Before the media manager goes (Emulator deletes it after Core): the unit slots unregister
    _context->pIdeController = nullptr;
    delete _ide;
    _ide = nullptr;

    _context->pUPD765 = nullptr;
    delete _upd765;
    _upd765 = nullptr;

    _context->pBetaDisk = nullptr;
    if (_betaDisk != nullptr)
    {
        _betaDisk->detachFromPorts();

        delete _betaDisk;
        _betaDisk = nullptr;
    }

    _context->pDiskAutostart = nullptr;
    if (_diskAutostart != nullptr)
    {
        delete _diskAutostart;
        _diskAutostart = nullptr;
    }

    _context->pDiskFastLoad = nullptr;
    if (_diskFastLoad != nullptr)
    {
        delete _diskFastLoad;
        _diskFastLoad = nullptr;
    }

    _context->pTapeFastLoad = nullptr;
    if (_tapeFastLoad != nullptr)
    {
        delete _tapeFastLoad;
        _tapeFastLoad = nullptr;
    }

    _context->pTapeTurboController = nullptr;
    if (_tapeTurboController != nullptr)
    {
        delete _tapeTurboController;
        _tapeTurboController = nullptr;
    }

    _context->pTape = nullptr;
    if (_tape != nullptr)
    {
        delete _tape;
        _tape = nullptr;
    }

    _context->pKeyboard = nullptr;
    if (_keyboard != nullptr)
    {
        delete _keyboard;
        _keyboard = nullptr;
    }

    _context->pMouse = nullptr;
    if (_mouse != nullptr)
    {
        delete _mouse;
        _mouse = nullptr;
    }

    _context->pJoystick = nullptr;
    if (_joystick != nullptr)
    {
        delete _joystick;
        _joystick = nullptr;
    }

    if (_rom != nullptr)
    {
        delete _rom;
        _rom = nullptr;
    }

    _context->pMemory = nullptr;
    if (_memory != nullptr)
    {
        delete _memory;
        _memory = nullptr;
    }

    _context->pUlaContention = nullptr;
    if (_ulaContention != nullptr)
    {
        delete _ulaContention;
        _ulaContention = nullptr;
    }

    if (_z80 != nullptr)
    {
        delete _z80;
        _z80 = nullptr;
    }

    // The PortDecoder must outlive every device that registered port handlers: the devices keep
    // their own PortDecoder pointer and their detachFromPorts() (SoundManager, WD1793, ...) calls
    // UnregisterPortHandler(), which mutates the decoder's handler map. Deleting it earlier made
    // those calls a heap-use-after-free on every emulator teardown.
    if (_portDecoder != nullptr)
    {
        delete _portDecoder;
        _portDecoder = nullptr;
    }

    if (_ports != nullptr)
    {
        delete _ports;
        _ports = nullptr;
    }
}
/// endregion </Initialization>

// Configuration methods
/// Three independent inputs (core.h): debug, contention in effect, the host
/// bus overlay. With neither contention nor an overlay the plain FastMemIf /
/// DbgMemIf are selected - the very same interfaces as before either
/// existed. Order matters for the overlay functions, which read Memory's
/// overlay pointer without a lock: it is set before an overlay interface is
/// selected, and cleared only after a plain one is.
void Core::SelectMemoryInterface()
{
    if (!_z80)
        return;

    std::lock_guard<std::mutex> lock(_memIfMutex);
    const bool debug = _z80->isDebugMode;
    const bool contended = IsContentionEffective();
    if (_busOverlay)
    {
        _memory->SetBusOverlay(_busOverlay);
        if (debug)
            _z80->MemIf = contended ? _z80->OverlayDbgContendedMemIf : _z80->OverlayDbgMemIf;
        else
            _z80->MemIf = contended ? _z80->OverlayFastContendedMemIf : _z80->OverlayFastMemIf;
    }
    else
    {
        if (debug)
            _z80->MemIf = contended ? _z80->DbgContendedMemIf : _z80->DbgMemIf;
        else
            _z80->MemIf = contended ? _z80->FastContendedMemIf : _z80->FastMemIf;
        _memory->SetBusOverlay(nullptr);
    }

    // The +2A/+3 gate array contends memory cycles only
    _z80->ioContention = (contended && !_ulaContention->IsGateArray()) ? _ulaContention : nullptr;
    _z80->idleContention = _z80->ioContention;  // the ULA contends internal cycles too, the gate array does not
}

bool Core::AddBusOverlay(HostBusOverlay* overlay)
{
    if (!overlay)
        return false;
    {
        std::lock_guard<std::mutex> lock(_memIfMutex);
        for (size_t i = 0; i < _busOverlayCount; i++)
        {
            if (_busOverlays[i] == overlay)
                return true;
        }
        if (_busOverlayCount == HostBusOverlayChain::kMaxOverlays)
        {
            MLOGERROR("Core::AddBusOverlay - %zu host bus overlays are installed already; refused", _busOverlayCount);
            return false;
        }
        _busOverlays[_busOverlayCount++] = overlay;
        UpdateEffectiveBusOverlay();
    }
    SelectMemoryInterface();
    return true;
}

void Core::RemoveBusOverlay(HostBusOverlay* overlay)
{
    {
        std::lock_guard<std::mutex> lock(_memIfMutex);
        size_t kept = 0;
        for (size_t i = 0; i < _busOverlayCount; i++)
        {
            if (_busOverlays[i] != overlay)
                _busOverlays[kept++] = _busOverlays[i];
        }
        if (kept == _busOverlayCount)
            return;
        for (size_t i = kept; i < _busOverlayCount; i++)
            _busOverlays[i] = nullptr;
        _busOverlayCount = kept;
        UpdateEffectiveBusOverlay();
    }
    SelectMemoryInterface();
}

void Core::ClearBusOverlays()
{
    {
        std::lock_guard<std::mutex> lock(_memIfMutex);
        for (auto& overlay : _busOverlays)
            overlay = nullptr;
        _busOverlayCount = 0;
        UpdateEffectiveBusOverlay();
    }
    SelectMemoryInterface();
}

bool Core::IsBusOverlayInstalled(const HostBusOverlay* overlay) const
{
    for (size_t i = 0; i < _busOverlayCount; i++)
    {
        if (_busOverlays[i] == overlay)
            return true;
    }
    return false;
}

/// Under _memIfMutex. One overlay is called directly (no chain cost); two or
/// more go through the chain. SelectMemoryInterface then hands the result to
/// Memory
void Core::UpdateEffectiveBusOverlay()
{
    _busOverlayChain.Assign(_busOverlays, _busOverlayCount);
    if (_busOverlayCount == 0)
        _busOverlay = nullptr;
    else if (_busOverlayCount == 1)
        _busOverlay = _busOverlays[0];
    else
        _busOverlay = &_busOverlayChain;
}

bool Core::IsContentionEffective() const
{
    return _contentionSwitch && _ulaContention && _ulaContention->IsContentionEnabled();
}

bool Core::IsSlotContended(uint8_t slot) const
{
    return IsContentionEffective() && _ulaContention->IsSlotContended(slot);
}

const char* Core::GetMemoryInterfaceName() const
{
    if (!_z80)
        return "none";
    const MemoryInterface* m = _z80->MemIf;
    if (m == _z80->OverlayDbgContendedMemIf)
        return "debug_contended_overlay";
    if (m == _z80->OverlayFastContendedMemIf)
        return "fast_contended_overlay";
    if (m == _z80->OverlayDbgMemIf)
        return "debug_overlay";
    if (m == _z80->OverlayFastMemIf)
        return "fast_overlay";
    if (m == _z80->DbgContendedMemIf)
        return "debug_contended";
    if (m == _z80->FastContendedMemIf)
        return "fast_contended";
    if (m == _z80->DbgMemIf)
        return "debug";
    return "fast";
}

void Core::Reset()
{
    Reset(static_cast<ROMModeEnum>(_config->reset_rom));
}

void Core::Reset(ROMModeEnum mode)
{
    // Set default ROM according to config settings (can be overriden for advanced platforms like TS-Conf and ATM)
    _mode = mode;

    // Reset EmulatorState fields that are not covered by individual peripheral resets
    // These must be cleared BEFORE peripheral resets so PortDecoder::reset() can set
    // model-specific defaults without stale values interfering
    _state->t_states = 0;       // Cumulative T-State counter (used for tape/peripheral timing)
    _state->frame_counter = 0;  // Frame counter
    _state->flags = 0x00;       // Execution flags (CF_TRDOS, CF_DOSPORTS, CF_LEAVEDOSRAM, etc.)
    _state->border_attr = 0x07; // Default border color (white)
    _state->active_ay = 0;      // Default AY chip (first)
    _state->ulaplus_mode = 0;   // Disable ULA+ palette
    _state->ulaplus_reg = 0;    // Reset ULA+ register selector

    // Reset main Z80 Core and all peripherals
    _z80->Reset();               // Main Z80
    _memory->Reset();            // Memory
    _keyboard->Reset();          // Keyboard
    if (_mouse)
        _mouse->ApplyConfiguration();  // Kempston Mouse fitting (Mouse=, Wheel=); counters are power-on
                                       // only - RESET does not reach the interface (MiSTer mouse.v: cold_reset)
    if (_joystick)
        _joystick->ApplyConfiguration();  // Kempston joystick fitting and keys; held buttons mirror the physical stick
    _sound->reset();             // All sound devices (AY(s), COVOX, MoonSound, GS) and sound subsystem
    _screen->Reset();            // Reset all video subsystem
    _tape->reset();              // Reset tape loader state
    _betaDisk->reset();          // BetaDisk floppy controller
    if (_upd765)
        _upd765->reset();        // +3 floppy controller
    _ide->Reset();               // IDE units: the machine's reset line (IDE design §3.3)
    if (_networkManager)
        _networkManager->Reset();  // ZX-Bus /RESET: card registers to 0, W5300 held in reset
    _portDecoder->reset();       // Reset peripheral port decoder (sets model-specific port defaults)

    // Apply the model-specific boot register defaults for the RESET= mode (port
    // of the original reset(mode) ATM block: RM_DOS installs the FF77/pFFF7
    // memory-manager defaults, other modes leave the manager disabled so all
    // windows read the last ROM page - the ATM BIOS)
    _portDecoder->ApplyBootROMDefaults(_mode);

    // Apply the ROM mode requested by the RESET= config directive (port of the
    // original set_mode(conf.reset_rom) performed at the end of m_reset()).
    // The decoder reset above establishes the model defaults (p7FFD = 0, 128K
    // ROM selected); the configured mode is layered on top: BASIC -> 48K BASIC,
    // DOS -> TR-DOS, MENU -> 128K menu, SYS -> service ROM. For models with
    // their own memory manager (ATM) UpdateZ80Banks() re-runs the manager
    // mapping instead of the generic ZX bank layout
    _memory->SetROMMode(_mode);
#ifdef ENABLE_RECORDING
    if (_recordingManager)
        _recordingManager->Reset();  // Reset recording manager (stops active recording, clears counters)
#endif

    // Single NC_SYSTEM_RESET notification per reset, posted only after the reset completes:
    // observers (HUD reset toast, FPS re-arm) must see exactly one event. An earlier
    // 'started' + 'finished' pair here surfaced as duplicate notifications on snapshot
    // load, where the loader's core.Reset() is the only reset that runs.
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    int topicID = messageCenter.RegisterTopic(NC_SYSTEM_RESET);
    messageCenter.Post(topicID, new SimpleTextPayload("Core reset finished"));
}

//
// Set main Z80 Core clock speed
// Multplier from 3.5MHz
//
void Core::SetCPUClockSpeed(uint8_t multiplier)
{
    if (multiplier == 0)
    {
        LOGERROR("Core::SetCPUClockSpeed - Z80 clock frequency multiplier cannot be 0");
        assert(false);
    }

    _z80->rate = (256 / multiplier);
}

uint32_t Core::GetBaseCPUFrequency()
{
    return _state->base_z80_frequency;
}

uint32_t Core::GetCPUFrequency()
{
    return _state->current_z80_frequency;
}

uint16_t Core::GetCPUFrequencyMultiplier()
{
    return _state->current_z80_frequency_multiplier;
};

//
// Set speed multiplier for emulation (1x, 2x, 4x, 8x, 16x)
// This scales the number of t-states executed per frame
//
bool Core::CanSetSpeedMultiplier(uint8_t multiplier) const
{
    // A TTD recording captures the machine at real speed: only 1x while it is
    // active (TimeTravelManager forces 1x on entry and restores the setting on exit)
    return multiplier == 1 || !_context->pFeatureManager || !_context->pFeatureManager->isTtdRecordingActive();
}

bool Core::SetSpeedMultiplier(uint8_t multiplier)
{
    // Validate multiplier is one of allowed values (use static list)
    static const std::array<uint8_t, 5> allowedMultipliers = {1, 2, 4, 8, 16};
    if (std::find(allowedMultipliers.begin(), allowedMultipliers.end(), multiplier) == allowedMultipliers.end())
    {
        LOGERROR("Core::SetSpeedMultiplier - Speed multiplier must be one of {1,2,4,8,16} (got %d)", multiplier);
        assert(false);
        return false;
    }

    if (!CanSetSpeedMultiplier(multiplier))
    {
        MLOGWARNING("Core::SetSpeedMultiplier - %dx refused: TTD recording is active (only 1x allowed)", multiplier);
        return false;
    }

    // Queue the multiplier change - it will be applied at the start of the next frame
    // This prevents mid-frame inconsistencies in timing calculations
    _state->next_z80_frequency_multiplier = multiplier;

    MLOGINFO("Core::SetSpeedMultiplier - Speed multiplier queued to %dx (will apply at next frame)", multiplier);

    // Notify consumers (HUD speed indicator, status bar)
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_SPEED_CHANGED,
        new SpeedChangedPayload(_context->emulatorId, multiplier, _context->config.turbo_mode));
    return true;
}

uint8_t Core::GetSpeedMultiplier() const
{
    return _state->current_z80_frequency_multiplier;
}

uint8_t Core::GetHostSpeedMultiplier() const
{
    return _state->next_z80_frequency_multiplier;
}

//
// Enable turbo/max speed mode - runs emulation as fast as possible
// withAudio: if true, continue generating audio samples (at increased pitch)
//
void Core::EnableTurboMode(bool withAudio)
{
    _context->config.turbo_mode = true;
    _context->config.turbo_mode_audio = withAudio;

    // Always mute audible output in turbo mode to avoid chipmunk sounds
    // Audio generation may still occur if withAudio=true (for recording)
    // Drop to the low-quality DSP path as well: HQ is pure CPU cost at turbo speed.
    // The user's soundhq setting is not modified - it comes back when turbo ends.
    if (_context->pSoundManager)
    {
        _context->pSoundManager->mute();
        _context->pSoundManager->setTurboLowQualityOverride(true);
    }

    MLOGINFO("Core::EnableTurboMode - Turbo mode enabled (audio generation: %s, audible: MUTED)",
             withAudio ? "ON" : "OFF");

    // Notify consumers (HUD speed indicator, status bar)
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_SPEED_CHANGED,
        new SpeedChangedPayload(_context->emulatorId, _state->current_z80_frequency_multiplier, true));
}

//
// Disable turbo mode and return to normal speed
//
void Core::DisableTurboMode()
{
    _context->config.turbo_mode = false;

    // Restore audible output and the previous DSP quality
    if (_context->pSoundManager)
    {
        _context->pSoundManager->unmute();
        _context->pSoundManager->setTurboLowQualityOverride(false);
    }

    MLOGINFO("Core::DisableTurboMode - Turbo mode disabled, audio unmuted");

    // Notify consumers (HUD speed indicator, status bar)
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_SPEED_CHANGED,
        new SpeedChangedPayload(_context->emulatorId, _state->current_z80_frequency_multiplier, false));
}

//
// Check if turbo mode is currently active
//
bool Core::IsTurboMode() const
{
    return _context->config.turbo_mode;
}

void Core::CPUFrameCycle()
{
    // Debug (instrumented) or fast memory access, contended or not, with the
    // bus overlay or not - see SelectMemoryInterface
    SelectMemoryInterface();
    if (_z80->isDebugMode && _ulaContention)
        _ulaContention->OnFrameStart();  // contention statistics are per frame (debugger only)
    _z80->Z80FrameCycle();

    FinishCPUFrame();
}

void Core::FinishCPUFrame()
{
    AdjustFrameCounters();

    // Sync memory content to disk (if shared memory mapping is enabled)
    _memory->SyncToDisk();
}

/// Perform corrections after each frame rendered
void Core::AdjustFrameCounters()
{
    /// region <Input parameters validation>
    // Calculate scaled frame limit based on speed multiplier
    uint32_t scaledFrame = _config->frame * _state->current_z80_frequency_multiplier;

    if (_z80->t < scaledFrame)
        return;
    /// endregion </Input parameters validation>

    // Update frame stats
    _state->frame_counter++;

    // Re-adjust Core frame t-state counter and interrupt position
    _z80->t -= scaledFrame;

    // The machine engine rebases its frame-relative positions (IMachineStepHook)
    if (IMachineStepHook* hook = _z80->GetMachineStepHook()) [[unlikely]]
        hook->OnMachineFrameRollover(scaledFrame);

    // Drop any stale INT request latched near the frame edge. The ULA INT line
    // is only asserted inside [intstart, intstart+intlen); ProcessInterrupts
    // clears int_pending via "t >= int_end", but when an instruction (typically
    // the INT acceptance itself) carries t across the frame boundary that clear
    // never fires. The stale flag would then deliver a SECOND interrupt in the
    // new frame as soon as the program executes EI (observed as 1.5-2x music
    // speedup in EI:HALT-synced IM2 demos, e.g. Insult megademo). Windows that
    // legitimately wrap (int_end >= frame) are re-armed at the start of the
    // next Z80FrameCycle, so unconditional clearing here is hardware-correct.
    _z80->int_pending = false;
}

void Core::UpdateScreen()
{
    GetZ80()->OnCPUStep();
}

void Core::ApplyNetworkConfiguration()
{
    if (_networkManager)
        _networkManager->ApplyConfiguration();
}

void Core::OnNetworkFrame()
{
    if (_networkManager)
        _networkManager->OnFrame();
}

void Core::RefitIde()
{
    _context->pIdeController = nullptr;
    delete _ide;
    _ide = new IdeController(_context);
    _context->pIdeController = _ide;
    if (_portDecoder)
        _portDecoder->GetIdeAdapter().Reset();
}
