#pragma once
#include <atomic>
#include <mutex>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/cpu/cputables.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/ports/ports.h"
#include "emulator/sound/soundmanager.h"
#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
#endif
#include "emulator/video/screen.h"
#include "emulator/video/ulacontention.h"
#include "stdafx.h"


class ModuleLogger;
class MessageCenter;
class Z80;
class PortDecoder;
class WD1793;
class UPD765;
class IdeController;
class TapeFastLoad;
class TapeTurboController;
class DiskFastLoad;
class DiskAutostart;
class HostBusOverlay;
class NetworkManager;
class SlotManager;

class Core
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    ModuleLogger* _logger = nullptr;
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
    CONFIG* _config = nullptr;

    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    Ports* _ports = nullptr;
    PortDecoder* _portDecoder = nullptr;
    ROM* _rom = nullptr;
    Keyboard* _keyboard = nullptr;
    Mouse* _mouse = nullptr;
    MouseManager* _mouseManager = nullptr;
    Joystick* _joystick = nullptr;
    Tape* _tape = nullptr;
    TapeFastLoad* _tapeFastLoad = nullptr;
    TapeTurboController* _tapeTurboController = nullptr;
    // VG93* _betaDisk = nullptr;
    WD1793* _betaDisk = nullptr;
    UPD765* _upd765 = nullptr;  // +3 only
    DiskFastLoad* _diskFastLoad = nullptr;
    DiskAutostart* _diskAutostart = nullptr;
    SoundManager* _sound = nullptr;
    /// The host audio hold of the current turbo span (reason Turbo): engaged by EnableTurboMode, released by
    /// DisableTurboMode - one per span however often either is called, and given back before _sound goes
    SoundManager::HostOutputHold _turboHostHold;
    /// Turbo is switched from the UI / automation threads and by the tape turbo controller on the machine's own
    std::mutex _turboHostHoldMutex;
#ifdef ENABLE_RECORDING
    RecordingManager* _recordingManager = nullptr;
#endif
    IdeController* _ide = nullptr;
    NetworkManager* _networkManager = nullptr;  // network adapters (ZXNETUSB); empty unless fitted
    SlotManager* _slotManager = nullptr;        // the slot set, planned before any card is built (ZX-bus slots SL-4)
    VideoControl* _video = nullptr;
    Screen* _screen = nullptr;
    UlaContention* _ulaContention = nullptr;
    bool _contentionSwitch = true;  // 'contention' feature (SetContentionSwitch)

    ROMModeEnum _mode = ROMModeEnum::RM_NOCHANGE;

    // Memory interface selection (neogs-zxdma-design.md §5.3): Z80::MemIf is
    // only ever written by SelectMemoryInterface, under this lock, from the
    // debug flag, the contention in effect and the installed bus overlays
    std::mutex _memIfMutex;
    HostBusOverlay* _busOverlays[HostBusOverlayChain::kMaxOverlays] = {};
    size_t _busOverlayCount = 0;
    HostBusOverlay* _busOverlay = nullptr;  // what Memory calls: none, the only overlay, or _busOverlayChain
    HostBusOverlayChain _busOverlayChain;
    void UpdateEffectiveBusOverlay();  // under _memIfMutex
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Core() = delete;                 // Disable default constructor. C++ 11 feature
    Core(EmulatorContext* context);  // Only constructor with context param is allowed
    virtual ~Core();
    /// endregion </Constructors / Destructors

    /// region <Initialization>
    [[nodiscard]] bool Init();
    void Release();
    /// endregion </Initialization>

    /// region <Peripherals>
    /// Build the IDE board again from the current config ([HDD] Scheme, CDn):
    /// media of units that come back are attached again, the others park.
    /// The emulator must not be running
    void RefitIde();

    /// Fit or unplug the network adapters to match [NETWORK] Card= and the
    /// "network" feature (deferred to the next frame boundary while the
    /// machine runs on another thread)
    void ApplyNetworkConfiguration();

    /// Frame boundary work of the network adapters (machine thread): the devices' own work before the TTD
    /// checkpoint (OnNetworkFrameDevices), the host's journaled answers after it (OnNetworkFrame)
    void OnNetworkFrameDevices();
    void OnNetworkFrame();

    NetworkManager* GetNetworkManager() { return _networkManager; }
    SlotManager* GetSlotManager() { return _slotManager; }
    /// endregion </Peripherals>

    /// region <Properties>
    Z80* GetZ80()
    {
        return _z80;
    }
    Memory* GetMemory()
    {
        return _memory;
    }
    Ports* GetPorts()
    {
        return _ports;
    }
    ROM* GetROM()
    {
        return _rom;
    }

    /// endregion </Properties>

    // Configuration methods
public:
    /// The one place that decides which memory interface the CPU runs on, from three independent inputs:
    /// Fast or Debug by the debugger (Z80::isDebugMode), plain or contended by whether the machine's video
    /// contention is in effect (UlaContention::IsContentionEnabled; with it the I/O contention rule,
    /// Z80::ioContention), and with or without an installed host bus overlay (AddBusOverlay). The plain
    /// Fast / Debug interfaces are selected whenever there is neither contention nor an overlay. Called
    /// whenever an input changes (debug mode, video mode / model via Screen::InitRaster, an overlay) and at
    /// every frame start; cheap (a lock and a few loads and stores). Any thread
    void SelectMemoryInterface();

    /// Install a host bus overlay (hostbusoverlay.h). Several devices may
    /// install one each (a machine's bus logic and a card's); they are called
    /// in install order. Installing one already installed is a no-op. Returns
    /// false, and changes nothing, when HostBusOverlayChain::kMaxOverlays are
    /// already installed. Call on the emulation thread or with the emulation
    /// paused.
    bool AddBusOverlay(HostBusOverlay* overlay);
    /// Remove an installed overlay (a no-op for one not installed)
    void RemoveBusOverlay(HostBusOverlay* overlay);
    /// Remove every overlay (machine teardown, test fixtures)
    void ClearBusOverlays();
    bool IsBusOverlayInstalled(const HostBusOverlay* overlay) const;
    size_t GetBusOverlayCount() const { return _busOverlayCount; }
    /// What the memory interface calls: nullptr, the only overlay, or the
    /// chain that forwards to all of them
    HostBusOverlay* GetBusOverlay() const { return _busOverlay; }

    /// The 'contention' feature (FeatureManager::onFeatureChanged): off runs a contended machine uncontended.
    /// Re-selects the interface
    void SetContentionSwitch(bool on)
    {
        _contentionSwitch = on;
        SelectMemoryInterface();
    }
    bool IsContentionSwitchOn() const { return _contentionSwitch; }

    /// Contention in effect: the machine has a rule and the switch is on
    bool IsContentionEffective() const;

    /// Whether the CPU waits for the video logic on accesses to a 16K slot (0-3) right now: contention in
    /// effect and a contended page mapped there. The one answer every memory map reports
    bool IsSlotContended(uint8_t slot) const;

    /// Name of the selected memory interface: "fast", "debug", "fast_contended", "debug_contended", and
    /// "..._overlay" for each with the host bus overlay installed
    const char* GetMemoryInterfaceName() const;

    // Z80 Core-related methods
public:
    void Reset();
    /// Reset with an explicit ROM mode instead of the configured RESET= mode (e.g. RM_DOS: reset into TR-DOS)
    void Reset(ROMModeEnum mode);

    void SetCPUClockSpeed(uint8_t);
    uint32_t GetBaseCPUFrequency();
    uint32_t GetCPUFrequency();
    uint16_t GetCPUFrequencyMultiplier();

    // Speed multiplier control: 1x (default), 2x, 4x, 8x, 16x
    /// @return false when refused: while TTD is recording only 1x is accepted
    bool SetSpeedMultiplier(uint8_t multiplier);
    bool CanSetSpeedMultiplier(uint8_t multiplier) const;
    uint8_t GetSpeedMultiplier() const;
    /// Host speed control setting (1x..16x) without the emulated hardware turbo
    uint8_t GetHostSpeedMultiplier() const;

    // Turbo/Max speed mode control
    void EnableTurboMode(bool withAudio = false);
    void DisableTurboMode();
    bool IsTurboMode() const;

    void CPUFrameCycle();

    /// CPU-side close of a frame whose last instruction reached the frame
    /// limit: rebase the in-frame counters (AdjustFrameCounters) and sync
    /// shared memory. Shared by the main loop and every Emulator::Run* path;
    /// the lifecycle hooks follow in MainLoop::CompleteFrame
    void FinishCPUFrame();
    void AdjustFrameCounters();

    // Event handlers
public:
    void UpdateScreen();
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing /
// benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class CoreCUT : public Core
{
public:
    CoreCUT(EmulatorContext* context) : Core(context) {};

    using Core::_z80;
    using Core::_memory;
};
#endif  // _CODE_UNDER_TEST