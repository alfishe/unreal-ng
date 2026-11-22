#pragma once

/// @file espapphost.h
/// @brief The VDAC4 ESP32-S3 firmware's app runtime, high-level: app ELFs run on the Xtensa interpreter, the
/// firmware around them (zm_core_t, FreeRTOS tasks, heap_caps, the PCM player, the FT812 host) is native.
///
/// Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §8; ABI facts: research-app-abi.md.
///
/// Execution model
///   - One interpreter runs all guest tasks. Emulated time advances in 1 ms slices (Advance). In each slice the
///     two highest-priority ready tasks get one core each: EspMhz * 1000 instructions per core (one instruction =
///     one cycle). A task that blocks gives the rest of its core's slice to the next ready task.
///   - Firmware threads that call into the app are host tasks: "loop" (Arduino loopTask: main, start, command,
///     stop; priority 1) and "net" (zifi-net: poll every 2 ms; priority 2). A host task runs one guest call at a
///     time (BeginCall into a sentinel trap) and is idle between calls.
///   - App tasks (task_start) enter through the ROM's zm_rom_task_entry(fn, arg) and end at zm_host_task_exit.
///   - Blocking firmware calls (delay, mutex_lock, signal_wait, pcm_write on a full ring, ft_open's waits) park the
///     task with a pending return value; it is delivered when the task runs again.
///
/// The libc / libm / libgcc the firmware exports run as guest code from the HLE ROM (esprom.h); the traps there
/// (malloc family, newlib system calls, __getreent) and every zm_core_t entry are served here.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "espcpu.h"
#include "espelfloader.h"
#include "espmemory.h"

namespace vdac4
{

/// The board around the ESP, as the runtime needs it
struct EspAppHostPorts
{
    /// core->reply (only inside a command) and core->event: a zm result each
    std::function<int(uint8_t command, const uint8_t* data, size_t size)> reply;
    std::function<int(uint8_t code, const uint8_t* data, size_t size)> event;
    /// main returned the app's declaration (name, version, command range): before start() runs
    std::function<void(const std::string& name, const std::string& version, uint8_t first, uint8_t last)> declared;
    /// core->link_media; a zm result. Unset: kept here only
    std::function<int(uint8_t first, uint8_t last)> mediaRange;
    /// core->app_done inside a command. Unset: the host stops the app itself after the command
    std::function<void()> appDone;
    /// core->ft_open succeeded
    std::function<void()> ftOpened;
    /// Tang LINK register file (32-bit words at byte addresses)
    std::function<uint32_t(uint32_t address)> linkRead;
    std::function<void(uint32_t address, uint32_t value)> linkWrite;
    /// LINK FT_ADDR, then FT_DATA per word (little-endian bytes), in one call. Unset: through linkWrite
    std::function<void(uint32_t ftAddress, const uint8_t* words, size_t count)> linkFtWrite;
    /// Played PCM: interleaved int16 stereo at `rate`
    std::function<void(const int16_t* frames, size_t count, uint32_t rate)> audio;
    /// zm_core_t::log lines and runtime diagnostics
    std::function<void(const std::string& line)> log;
};

class EspAppHost
{
public:
    static constexpr uint32_t kDefaultMhz = 240;
    static constexpr int kMaxAppTasks = 8;    // module_app.hpp kMaxTasks
    static constexpr int kMaxSync = 16;       // kMaxSync: mutexes + signals
    static constexpr uint32_t kIdleStopMs = 5000;

    /// zm_core_t function slots, in struct order (SDK zm_app.h)
    enum ZmSlot : uint32_t
    {
        ZmAlloc, ZmRealloc, ZmFree, ZmLog, ZmMillis, ZmDelay, ZmTaskStart, ZmMutexCreate, ZmMutexLock,
        ZmMutexUnlock, ZmSignalCreate, ZmSignalGive, ZmSignalWait, ZmReply, ZmEvent, ZmPcmOpen, ZmPcmWrite,
        ZmPcmQueuedMs, ZmPcmFlush, ZmPcmClose, ZmPcmPause, ZmSetVolume, ZmVolume, ZmPcmFaults, ZmModuleOpen,
        ZmModuleClose, ZmResource, ZmResourceRelease, ZmTemperature, ZmNetOpen, ZmNetHeader, ZmNetRead,
        ZmNetClose, ZmFtOpen, ZmFtWrite, ZmFtRead, ZmFtHostCmd, ZmFtClose, ZmCoreLoad, ZmCoreBack, ZmLinkRead,
        ZmLinkWrite, ZmSockOpen, ZmSockSend, ZmSockRecv, ZmSockClose, ZmNetResolve, ZmWifiInfo, ZmUiAvailable,
        ZmAppDone, ZmSdList, ZmSdRead, ZmSdWrite, ZmSdOpen, ZmSdPread, ZmSdPwrite, ZmSdClose, ZmSdCreate,
        ZmWifiRssi, ZmSockOpenTls, ZmLinkMedia, ZmHandoffPut, ZmHandoffTake, ZmSd2Open, ZmSd2Read, ZmSd2Stream,
        ZmSd2Close, ZmLinkBurst, ZmPcmLowLatency,
        ZmSlotCount
    };
    /// sizeof(zm_core_t) on the board: three u16, pad, firmware pointer, then the slots
    static constexpr uint32_t kZmCoreSize = 12 + 4 * ZmSlotCount;
    static constexpr uint16_t kAbiMinor = 16;

    EspAppHost(std::unique_ptr<IEspCpu> cpu, EspAppHostPorts ports, uint32_t mhz = kDefaultMhz);
    ~EspAppHost();

    /// Power-on / SYS_RESET: everything stopped and freed, time 0
    void Reset();

    /// Run the ESP up to `nowUs` (emulated microseconds since power-on)
    void Advance(uint64_t nowUs);
    /// The app's code translated to host code (the JIT) or interpreted, from the next instruction run on; any thread
    void SetJit(bool on) { _cpu->SetJit(on); }
    uint64_t NowUs() const { return _nowUs; }

    /// region <The firmware's app commands (ModuleApp)>

    /// APP_START with the module's bytes: loads, runs main and start on the loop task; `done` gets the zm result
    /// and the app's name and version once start returned (the firmware's EC reply payload)
    void StartApp(const std::string& name, std::vector<uint8_t> elf,
                  std::function<void(int result, const std::string& name, const std::string& version)> done);
    /// APP_STOP and the forced cleanup (module_app.cpp stopApp)
    void StopApp();
    bool IsRunning() const { return _app.address != 0 && _app.started; }
    const std::string& AppName() const { return _app.name; }
    /// A Z80 command in the app's range (handleCommand); replies come through ports.reply
    bool OwnsCommand(uint8_t command) const;
    void Command(uint8_t command, const uint8_t* data, size_t size);
    /// app->command's result of the last finished command
    int LastCommandResult() const { return _lastCommandResult; }
    /// Run the ESP in small steps until the loop task has nothing to do (main / start / commands finished), at
    /// most `limitUs` of ESP time; false when it is still busy then
    bool RunUntilLoopIdle(uint64_t limitUs);
    bool LoopIdle() const;
    /// Commands queued or running on the loop task
    size_t PendingCommands() const;
    /// The media range (link_media), 0..0 when none
    uint8_t MediaFirst() const { return _mediaFirst; }
    uint8_t MediaLast() const { return _mediaLast; }

    /// endregion

    /// Guest faults and diagnostics so far (a stopped app explains itself here)
    const std::string& LastError() const { return _lastError; }
    EspMemory& Memory() { return _memory; }
    const EspMemory& Memory() const { return _memory; }
    uint64_t Instructions() const { return _instructions; }
    /// A byte of the running app at an address of its ELF image (EspElfImage::MapSymbol: the address a symbol has in
    /// the file), no side effects; false with no app loaded or outside its sections
    bool PeekAppByte(uint32_t elfAddress, uint8_t& value) const;
    /// The running app's sections: ELF address, size and the D-bus address each was loaded at (none: no app)
    const EspElfImage* AppImage() const { return _app.address != 0 ? &_app.image : nullptr; }

private:
    struct HostCall
    {
        uint32_t pc = 0;
        uint32_t args[4] = {};
        int count = 0;
        std::function<void(uint32_t result)> done;
        /// app->command: the payload goes to the command buffer when the call starts (commands run in order)
        std::shared_ptr<std::vector<uint8_t>> payload;
    };

    enum class TaskState : uint8_t
    {
        Ready,
        Sleeping,      ///< until wakeUs
        WaitMutex,     ///< on waitObject
        WaitSignal,    ///< on waitObject, or until wakeUs (timeout)
        WaitPcm,       ///< ring full; retry at wakeUs
        HostIdle,      ///< a host task with no call running
        Done,
    };

    struct Task
    {
        uint32_t id = 0;
        std::string name;
        uint8_t priority = 1;
        bool host = false;
        TaskState state = TaskState::Ready;
        std::vector<uint8_t> cpu;
        uint32_t stack = 0;       ///< block start (heap)
        uint32_t stackSize = 0;
        uint64_t wakeUs = 0;
        uint32_t waitObject = 0;
        bool hasPending = false;  ///< deliver pendingReturn via ReturnFromTrap when it runs
        uint32_t pendingReturn = 0;
        uint64_t readySerial = 0; ///< round robin among equal priorities
        std::deque<HostCall> calls;
        bool callRunning = false;
        /// pcm_write in progress: frames still to take
        uint32_t pcmFrames = 0;
        uint32_t pcmAddress = 0;
        uint32_t pcmAccepted = 0;
        uint64_t pcmDeadlineUs = 0;
    };

    struct SyncObject
    {
        bool used = false;
        bool mutex = false;
        uint32_t handle = 0;  ///< the guest-visible handle
        uint32_t owner = 0;   ///< mutex: task id, 0 = free
        bool given = false;   ///< signal state
    };

    struct AppState
    {
        std::string name;
        uint32_t address = 0;  ///< zm_app_t in guest memory
        bool started = false;
        uint8_t commandFirst = 0;
        uint8_t commandLast = 0;
        uint32_t start = 0, stop = 0, command = 0, poll = 0;
        EspElfImage image;
        std::vector<uint32_t> allocations;  ///< coreAlloc blocks (header addresses)
        bool done = false;
        bool ftOpened = false;
        uint64_t lastCommandUs = 0;
    };

    // Firmware boot: zm_core_t, strings, host tasks
    void BuildCore();
    uint32_t TrapAddress(const char* romTrap) const;
    uint32_t RomSymbol(const char* name) const;

    // Scheduler
    Task* NewTask(const std::string& name, uint8_t priority, bool host, uint32_t stackSize, bool psramStack);
    Task* FindTask(uint32_t id);
    Task* PickReady(const Task* except);
    void RunCore(uint64_t budget, Task*& running, const Task* other);
    void WakeDue();
    void Block(Task& task, TaskState state);
    void MakeReady(Task& task);
    void QueueHostCall(Task& task, HostCall call);
    bool StartNextHostCall(Task& task);
    /// BeginCall of task.calls.front() on the loaded CPU state (places a command payload first)
    void BeginHostCall(Task& task);
    /// Drop ended app tasks (outside RunCore only: a trap handler may still hold the task)
    void Reap();
    uint32_t CurrentMs() const;

    // Traps: returns false when the task blocked or ended (the CPU state is not to be resumed now)
    bool HandleTrap(Task& task, uint32_t pc);
    bool HandleRomTrap(Task& task, uint32_t index);
    bool HandleZm(Task& task, uint32_t slot);
    void Fault(Task& task, const IEspCpu::Result& result);

    // zm_core_t pieces
    uint32_t CoreAlloc(uint32_t size, uint32_t caps);
    void CoreFree(uint32_t pointer);
    uint32_t CoreRealloc(uint32_t pointer, uint32_t size);
    int TaskStart(uint32_t fn, uint32_t arg, uint32_t stackBytes, uint8_t priority);
    uint32_t SyncCreate(bool mutex);
    SyncObject* Sync(uint32_t handle);
    void SyncRelease(SyncObject& object);
    bool PcmWriteStep(Task& task);
    void PcmPlay(uint64_t fromUs, uint64_t toUs);
    void FtWrite(uint32_t address, uint32_t words, uint32_t count);

    void Log(const std::string& line);
    void LoadApp(const std::string& name, const std::vector<uint8_t>& elf,
                 std::function<void(int, const std::string&, const std::string&)> done);
    void ForceCleanup();

    std::unique_ptr<IEspCpu> _cpu;
    EspAppHostPorts _ports;
    uint32_t _mhz;
    EspMemory _memory;

    uint64_t _nowUs = 0;
    uint64_t _sliceUsedCycles = 0;  ///< cycles the running core used in this slice (millis inside a slice)
    uint64_t _instructions = 0;
#ifdef UNREAL_XTENSA_PROFILE
    // POC 053: where the ESP's host time goes; dumped every emulated second (UNREAL_XTENSA_PROFILE=<prefix>)
    struct Profile
    {
        uint64_t budget[2] = {};      ///< cycles offered per core
        uint64_t used[2] = {};        ///< cycles run per core (instructions + window handlers)
        uint64_t runs = 0;            ///< Cpu::Run calls
        uint64_t runNs = 0;           ///< host time inside Cpu::Run
        uint64_t trapNs = 0;          ///< host time in HandleTrap
        uint64_t switchNs = 0;        ///< LoadState / SaveState
        uint64_t advanceNs = 0;       ///< host time in Advance
        uint64_t pcmNs = 0;
        std::map<uint32_t, std::pair<uint64_t, uint64_t>> traps;  ///< trap PC: count, host ns
    };
    Profile _profile;
    uint64_t _profileNextUs = 0;
    int _profileCore = 0;
    void DumpProfile();
#endif
    uint64_t _readySerial = 0;
    uint32_t _nextTaskId = 1;
    std::vector<std::unique_ptr<Task>> _tasks;
    Task* _loop = nullptr;
    Task* _net = nullptr;
    uint64_t _nextPollUs = 0;
    SyncObject _sync[kMaxSync];
    AppState _app;
    uint32_t _core = 0;          ///< zm_core_t in guest memory
    uint32_t _commandBuffer = 0; ///< command payload for app->command (8 KB + 16 bytes)
    uint32_t _argvBlock = 0;     ///< argv, name and app pointer for main
    uint32_t _sentinel = 0;
    uint32_t _romTrapBase = 0;
    uint8_t _mediaFirst = 0;
    uint8_t _mediaLast = 0;
    std::string _lastError;
    bool _inCommand = false;
    int _lastCommandResult = 0;

    // PCM player (audio_player.cpp): ring of int16 stereo at the open rate
    struct Pcm
    {
        bool open = false;
        bool paused = false;
        uint32_t rate = 44100;
        uint8_t channels = 2;
        uint8_t volume = 255;
        uint16_t faults = 0;
        std::vector<int16_t> ring;  ///< stereo frames
        size_t head = 0;            ///< next write
        size_t queued = 0;          ///< frames in the ring
        uint64_t playedFraction = 0;  ///< sub-frame remainder in rate * us units
    } _pcm;
};

}  // namespace vdac4
