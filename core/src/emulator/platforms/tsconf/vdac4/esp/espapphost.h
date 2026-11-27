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
    /// A module's ELF from the firmware's cache (module_open); a zm result. Unset: no module is found
    std::function<int(const std::string& name, std::vector<uint8_t>& bytes)> moduleBytes;
    /// data_open: the data file `name` (upper case 8.3) of app `app` the Z80 program registered (MOD_QUERY flag 4
    /// with a file number): kZmOk with its number at the Z80 and its length, else kZmNotFound. Unset: none
    std::function<int(const std::string& app, const std::string& name, uint8_t& index, uint32_t& size)> dataLookup;
    /// core_load: the held FPGA core `name` onto the Tang; kZmOk with the core's LINK VERSION word. Unset: not found
    std::function<int(const std::string& name, uint32_t& version)> coreLoad;
    /// core_back (and the app's unload after a core_load): the Tang back to slot 0 (TS-Conf); a zm result
    std::function<int()> coreBack;
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
        ZmSd2Close, ZmLinkBurst, ZmPcmLowLatency, ZmCodeRam, ZmModuleOpenEx, ZmDataOpen, ZmDataRead, ZmDataClose,
        ZmLinkReadChecked,
        ZmSlotCount
    };
    /// sizeof(zm_core_t) on the board: three u16, pad, firmware pointer, then the slots
    static constexpr uint32_t kZmCoreSize = 12 + 4 * ZmSlotCount;
    static constexpr uint16_t kAbiMinor = 21;
    /// zm_core_t::firmware: the vdac4-A.B.C form apps parse (Quake II wants 0.1.166+), then who made it
    static constexpr const char* kFirmware = "vdac4-0.1.174-unreal-ng-hle";
    /// The trap a module's entry returns to (module_open runs it nested in the caller's trap)
    static constexpr uint32_t kModuleReturnTrap = EspMemory::kZmTrapBase + 4 * ZmSlotCount;
    /// module_app.hpp kMaxModules (0.1.159)
    static constexpr int kMaxModules = 8;
    /// ZM_CODE_RAM_MAX, and what must stay free in internal RAM after the piece (module_app.cpp kCodeRamKeep)
    static constexpr uint32_t kCodeRamMax = 48u << 10;
    static constexpr uint32_t kCodeRamKeep = 12u << 10;
    /// app_data_files.hpp: open data files, the Z80's sector, the block of short reads, the largest request
    static constexpr int kMaxDataFiles = 8;
    static constexpr uint32_t kDataSectorBytes = 512;
    static constexpr uint32_t kDataBlockBytes = 8192;
    static constexpr uint32_t kDataRequestMax = 32768;
    /// module_app.cpp remoteFetch: a request and two repeats, 4 s each; sendEvent waits up to 1 s for room
    static constexpr uint32_t kDataAttempts = 3;
    static constexpr uint64_t kDataWaitUs = 4'000'000;
    static constexpr uint64_t kDataEventWaitUs = 1'000'000;
    /// protocol.hpp kEventDataRead: the ESP asks the Z80 for a piece of a data file
    static constexpr uint8_t kEventDataRead = 0x6E;
    /// PSRAM the board's firmware keeps for itself beyond what the HLE takes: 1440 KB, the figure the game's author
    /// measured (PROGRESS.md 24.11.2026: with it his emulator had 4192 KB free before Qcommon_Init, as the board).
    /// Here it leaves 4336 KB; 1584 KB gives exactly 4192 but Quake II then runs out of PSRAM loading base2: this
    /// HLE takes about 220 KB more during Qcommon_Init than his emulator did (quake2.md §8). UNREAL_VDAC4_PSRAM_RESERVE_KB
    static constexpr uint32_t kDefaultPsramReserveKb = 1440;

    EspAppHost(std::unique_ptr<IEspCpu> cpu, EspAppHostPorts ports, uint32_t mhz = kDefaultMhz);
    EspAppHost(const EspAppHost&) = delete;
    EspAppHost& operator=(const EspAppHost&) = delete;
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
    /// The app or a module it opened is `name` (a new copy of it makes the next APP_START restart the app)
    bool UsesModule(const std::string& name) const;
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
    /// A 0x6F piece of a data file from the Z80 ([request][offset LE32][data], module_app.cpp handleDataPiece):
    /// taken by the data_read waiting for it, else ignored. Between Advance calls
    void DataPiece(const uint8_t* data, size_t size);
    /// The firmware's table of data files changed (MOD_QUERY flag 4): the block of short reads is stale
    void DataFilesChanged();
    /// The app holds an FPGA core it loaded (core_load), released by core_back or its unload
    bool CoreLoaded() const { return _app.coreLoaded; }

    /// Data files (data_open / data_read) of the running app, for the automation surfaces
    struct DataStats
    {
        uint32_t openFiles = 0;
        bool reading = false;        ///< a data_read waits for the Z80 now
        uint64_t requests = 0;       ///< 0x6E requests answered whole
        uint64_t repeats = 0;        ///< requests asked again (no answer in 4 s, or the Z80 could not read)
        uint64_t failures = 0;       ///< data_read calls that failed after three attempts
        uint64_t pieces = 0;         ///< 0x6F pieces taken
        uint64_t bytes = 0;          ///< bytes the requests brought
        uint32_t longestMs = 0;      ///< the slowest request, ESP ms
    };
    DataStats GetDataStats() const;
    /// PSRAM free now (the guest heap), bytes
    uint32_t PsramFree() const { return _memory.Psram().FreeBytes(); }

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

    /// region <State (TTD)>

    /// Everything but the memory bytes: scheduler, tasks with their CPU states and queued calls, sync objects,
    /// the app, PCM, the heaps' block tables. Plain little-endian bytes. Taken between Advance calls (no slice
    /// running); a StartApp's `done` is not part of it (StartApp is synchronous)
    void SaveState(std::vector<uint8_t>& out) const;
    /// Restore SaveState's bytes; the memory (Memory().PsramBytes() / SramBytes() / RomBytes()) is the caller's,
    /// before or after. Drops the CPU's predecoded and translated code, unless HoldCode() says it may stay.
    /// False: malformed, the host is Reset
    bool LoadState(const uint8_t* data, size_t size);
    /// A restore that replaces the memory and then calls LoadState, with the memory in place by then: called
    /// before the memory changes, it lets that LoadState keep the CPU's code when the bytes under it came back
    /// the same (a seek inside one run of an app). Covers the next LoadState only
    void HoldCode();
    /// Drop predecoded and translated code of all guest memory (its bytes were replaced)
    void InvalidateAllCode();

    /// region <Input journal (TTD)>

    /// Everything the ESP takes in, in order: the calls into the host (Advance, StartApp, StopApp, Command,
    /// RunUntilLoopIdle, Reset; the outermost only) and what its ports answered (LINK reads, reply / event /
    /// mediaRange results). Run on the state it started from, the journal brings the host to the same state:
    /// the ports are not asked then and what goes out is dropped (the rest of the machine is restored apart).
    /// LINK reads that return the last word read at that address are counted, not stored (polling loops)
    enum class JournalMode : uint8_t
    {
        Off,
        Record,
        Replay,
    };
    /// Start a new journal from the state now (a keyframe)
    void JournalBegin();
    void JournalStop();
    JournalMode Journal() const { return _journal.mode; }
    /// The journal so far, the read run in progress closed (call between host calls)
    const std::vector<uint8_t>& JournalBytes();
    /// The journal as the last JournalBytes left it (no run closed)
    const std::vector<uint8_t>& JournalData() const { return _journal.bytes; }
    /// Run `data` (a journal begun at the state the host has now) to its end, then record on from there (the
    /// journal is `data`). False: it does not fit this state (`error` says where); the host is wherever it got
    bool JournalReplay(const uint8_t* data, size_t size, std::string& error);
    /// A part of it: the bytes [from, to) of `data`, a whole journal. `from` > 0 goes on from a replay that stopped
    /// there (the host, its memory and JournalReadMap as they were then); `to` < size stops at a boundary between
    /// host calls (a capture's journal length) and stays in replay, `to` = size records on as JournalReplay does
    bool JournalReplay(const uint8_t* data, size_t size, size_t from, size_t to, std::string& error);
    /// The last word read at each LINK address so far: what a replay stopped at a boundary goes on with
    const std::map<uint32_t, uint32_t>& JournalReadMap() const { return _journal.lastRead; }
    void SetJournalReadMap(const std::map<uint32_t, uint32_t>& reads) { _journal.lastRead = reads; }

    /// endregion

private:
    enum class HostCallKind : uint8_t
    {
        None = 0,     ///< net poll: nothing on return
        Main = 1,     ///< the app's main: check the interface, queue start
        Start = 2,    ///< app->start: the app runs
        Command = 3,  ///< app->command: the command's result
    };

    struct HostCall
    {
        uint32_t pc = 0;
        uint32_t args[4] = {};
        int count = 0;
        /// What its return does (data, not a closure: a host task's queue is part of the saved state)
        HostCallKind kind = HostCallKind::None;
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
        WaitData,      ///< data_read: a piece from the Z80, room for the request event, or the reads' turn
        HostIdle,      ///< a host task with no call running
        Done,
    };

    /// data_read of one task: the call's arguments and where its walk over the file stands (readRemote)
    struct DataRead
    {
        enum Phase : uint8_t
        {
            None = 0,     ///< not reading
            Turn,         ///< another task's read runs (reads go one at a time)
            Next,         ///< take the next part: from the block, or a new request
            Send,         ///< the request event waits for room in the event queue (up to 1 s)
            Wait,         ///< the request is out: pieces come, up to 4 s
        };
        Phase phase = None;
        uint8_t file = 0;          ///< handle slot
        uint32_t offset = 0;
        uint32_t dest = 0;         ///< guest buffer
        uint32_t count = 0;
        uint32_t got = 0;          ///< guest uint32_t* (0: none)
        uint32_t done = 0;
        uint32_t attempt = 0;
        uint64_t sendUs = 0;       ///< when the event first waited for room
        uint64_t beganUs = 0;      ///< the request's first attempt
    };

    /// A module_open waiting for the module's entry to return: where its outputs go
    struct ModuleOpening
    {
        uint32_t slot = 0;
        uint32_t handle = 0;  ///< void** handle
        uint32_t result = 0;  ///< int* entry_result
        uint32_t placed = 0;  ///< uint32_t* placed (module_open_ex), 0 if none
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
        /// module_open calls whose entry runs now (nested in the caller's trap), innermost last
        std::vector<ModuleOpening> opening;
        /// data_read in progress (DataReadStep)
        DataRead data;
    };

    /// A module the app opened (ModuleApp::LoadedModule)
    struct ModuleState
    {
        bool used = false;
        std::string name;
        EspElfImage image;
        bool inCodeRam = false;
        uint32_t record = 0;  ///< the loader's esp_elf_t (116 bytes of PSRAM, 0.1.160)
    };

    /// The app's code_ram piece (ModuleApp::CodeRam): internal SRAM, code runs from its I-bus mirror
    struct CodeRamState
    {
        uint32_t address = 0;  ///< D-bus; 0 = none
        uint32_t size = 0;
        uint32_t used = 0;     ///< module code placed so far
        uint32_t modules = 0;  ///< modules with code in it
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
        bool coreLoaded = false;   ///< core_load succeeded: the unload sends the Tang back (core_back)
        uint64_t lastCommandUs = 0;
    };

    /// The data files (app_data_files.cpp AppDataFiles with the module_app.cpp remote reads)
    struct DataFile
    {
        bool used = false;
        uint8_t index = 0;         ///< the file's number at the Z80 program
        uint32_t size = 0;
    };
    struct DataState
    {
        DataFile files[kMaxDataFiles];
        uint32_t owner = 0;        ///< the task whose data_read runs (0: none)
        // AppDataRequest: the request in flight
        bool active = false;
        bool failed = false;
        bool complete = false;
        uint8_t seq = 0;
        uint8_t index = 0;
        uint32_t at = 0;           ///< file offset of the request
        uint32_t count = 0;
        uint32_t received = 0;
        bool toBlock = false;      ///< into the block (a short read), else into the guest buffer at `dest`
        uint32_t dest = 0;
        uint64_t deadlineUs = 0;
        // The block of short reads
        std::vector<uint8_t> block;
        int blockIndex = -1;
        uint32_t blockAt = 0;
        uint32_t blockLength = 0;
        // Totals for the log line at unload and the state
        uint64_t requests = 0, repeats = 0, failures = 0, pieces = 0, bytes = 0, ms = 0;
        uint32_t longestMs = 0;
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
    /// The time inside the running slice (the trap's caller has used part of it)
    uint64_t SliceNowUs() const { return _nowUs + _sliceUsedCycles / _mhz; }
    int DataOpen(uint32_t name, uint32_t file, uint32_t size);
    /// data_read at its trap: false when the task waits
    bool DataReadCall(Task& task);
    /// The walk of a data_read (readRemote with remoteFetch): true when it ended (the return is pending)
    bool DataReadStep(Task& task);
    void DataReadFinish(Task& task, bool ok);
    void DataClose(uint32_t file);
    void DataCloseAll();
    int CoreLoad(uint32_t name, uint32_t version);
    /// The PSRAM the board's firmware holds (UNREAL_VDAC4_PSRAM_RESERVE_KB, default kDefaultPsramReserveKb)
    static uint32_t PsramReserveBytes();

    void Log(const std::string& line);
    /// module_open / module_open_ex at its trap: load, then run the entry nested (true: the task runs on)
    bool ModuleOpen(Task& task, bool ex);
    /// The entry returned to kModuleReturnTrap: the outputs, then module_open returns
    bool ModuleEntryReturned(Task& task);
    void ModuleClose(uint32_t handle);
    void CloseAllModules();
    int CodeRam(uint32_t bytes, uint32_t granted);
    void LoadApp(const std::string& name, const std::vector<uint8_t>& elf);
    void ForceCleanup();
    /// A host call returned `result`: what its kind does
    void CallDone(HostCallKind kind, uint32_t result);
    /// StartApp's `done`, taken once (it may be gone: cleanup clears it)
    void StartDone(int result, const std::string& name, const std::string& version);

    // Journal tokens (one byte, then varints)
    enum JournalToken : uint8_t
    {
        TokAdvance = 1,  ///< microseconds past the host's time
        TokStartApp,     ///< name, ELF
        TokStopApp,
        TokCommand,      ///< command, size, bytes
        TokRunIdle,      ///< limit, microseconds
        TokReset,
        TokRead,         ///< a LINK word unlike the last one read at its address
        TokReadSame,     ///< count of LINK reads giving the last word read at their address
        TokResult,       ///< a port's result, zigzag
        TokModule,       ///< a module's bytes from the cache: result zigzag, size, bytes
        TokDataPiece,    ///< a 0x6F piece: size, bytes
        TokDataChanged,  ///< the firmware's data file table changed
    };
    struct JournalState
    {
        JournalMode mode = JournalMode::Off;
        std::vector<uint8_t> bytes;
        std::map<uint32_t, uint32_t> lastRead;  ///< address -> last word read there
        uint64_t sameRun = 0;                   ///< reads pending as TokReadSame (record) / left of one (replay)
        size_t position = 0;                    ///< replay: next byte
        bool diverged = false;                  ///< replay: the stream did not fit
        std::string error;
        int depth = 0;                          ///< host calls in progress (only the outermost is journaled)
    };
    /// RAII: a host entry point; true for the outermost one while recording
    class JournalEntry
    {
    public:
        explicit JournalEntry(EspAppHost& host)
            : _host(host), _record(host._journal.depth++ == 0 && host._journal.mode == JournalMode::Record)
        {
        }
        ~JournalEntry() { --_host._journal.depth; }
        bool Record() const { return _record; }

    private:
        EspAppHost& _host;
        bool _record;
    };
    void JournalPutToken(uint8_t token);
    void JournalPutVarint(uint64_t value);
    void JournalPutBytes(const uint8_t* data, size_t size);
    void JournalFlushRun();
    bool JournalTakeToken(uint8_t expected);
    uint64_t JournalTakeVarint();
    void JournalDiverged(const char* what);
    /// The ports' inputs through the journal
    uint32_t JournalLinkRead(uint32_t address);
    /// A port result: the live one recorded, or the recorded one replayed (`live` is not called then)
    int JournalResult(const std::function<int()>& live);
    /// The moduleBytes port through the journal
    int JournalModule(const std::string& name, std::vector<uint8_t>& bytes);
    bool Replaying() const { return _journal.mode == JournalMode::Replay; }
    void InstallPorts();

    std::unique_ptr<IEspCpu> _cpu;
    EspAppHostPorts _outer;  ///< the owner's ports
    EspAppHostPorts _ports;  ///< what the host calls: _outer through the journal
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
    ModuleState _modules[kMaxModules];
    CodeRamState _codeRam;
    DataState _data;
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
    JournalState _journal;
    /// StartApp's `done` while its main / start calls are queued (not saved: StartApp is synchronous)
    std::function<void(int, const std::string&, const std::string&)> _startDone;

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
