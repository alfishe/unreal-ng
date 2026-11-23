#include "espapphost.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "esprom.h"

namespace vdac4
{

namespace
{

constexpr int kZmOk = 0;
constexpr int kZmError = -1;
constexpr int kZmNoMemory = -2;
constexpr int kZmNotFound = -3;
constexpr int kZmBusy = -4;
constexpr int kZmTimeout = -5;

constexpr uint32_t kZmMemInternal = 0x02;
constexpr uint32_t kTaskStackPsram = 0x80000000u;
constexpr uint32_t kAllocMagic = 0x5A4D414C;
constexpr uint32_t kAllocHeader = 16;  // AllocHeader {next, prev, caps, magic}

constexpr uint32_t kLinkFtAddr = 0x54;
constexpr uint32_t kLinkFtData = 0x58;
constexpr uint32_t kLinkFtStat = 0x5C;
constexpr uint32_t kLinkFtHost = 0xE8;

constexpr uint32_t kPcmRingFrames = (256u << 10) / 4;  // audio_player.cpp: 256 KB PSRAM ring of int16 stereo
constexpr uint32_t kPcmChunk = 256;
constexpr uint64_t kPcmWaitUs = 2000;
constexpr uint64_t kPcmGiveUpUs = 500'000;            // kPcmWriteWaitMs
constexpr uint64_t kPollPeriodUs = 2000;

constexpr uint32_t kLoopStack = 8192;
constexpr uint32_t kNetStack = 16384;
constexpr uint8_t kLoopPriority = 1;
constexpr uint8_t kNetPriority = 2;

const char* const kFirmware = "unreal-ng-hle-0.1.122";

/// ROM traps the host serves, by name (traps.txt)
enum class RomTrap : uint8_t
{
    Unknown, GetReent, Exit, Write, Sbrk, Read, Lseek, Kill, GetTimeOfDay, GetPid, Fstat, Close, IsATty,
    Malloc, Calloc, Realloc, Free, MallocR, CallocR, ReallocR, FreeR, HeapCapsMalloc, HeapCapsCalloc,
    HeapCapsFree, Usleep, Sleep, ClockGetTime, PthreadAny, LogLine, TaskExit, Return, LinkRead, LinkWrite,
    Delay, Millis, FtRead, FtWrite, FtOpened, DivSf3, SqrtF,
};

RomTrap RomTrapByName(const char* name)
{
    static const struct
    {
        const char* name;
        RomTrap trap;
    } kTable[] = {
        {"__getreent", RomTrap::GetReent}, {"_exit", RomTrap::Exit}, {"exit", RomTrap::Exit},
        {"_write", RomTrap::Write}, {"_sbrk", RomTrap::Sbrk}, {"_read", RomTrap::Read}, {"_lseek", RomTrap::Lseek},
        {"_kill", RomTrap::Kill}, {"_gettimeofday", RomTrap::GetTimeOfDay}, {"_getpid", RomTrap::GetPid},
        {"_fstat", RomTrap::Fstat}, {"_close", RomTrap::Close}, {"close", RomTrap::Close},
        {"_isatty", RomTrap::IsATty}, {"malloc", RomTrap::Malloc}, {"calloc", RomTrap::Calloc},
        {"realloc", RomTrap::Realloc}, {"free", RomTrap::Free}, {"_malloc_r", RomTrap::MallocR},
        {"_calloc_r", RomTrap::CallocR}, {"_realloc_r", RomTrap::ReallocR}, {"_free_r", RomTrap::FreeR},
        {"heap_caps_malloc", RomTrap::HeapCapsMalloc}, {"heap_caps_calloc", RomTrap::HeapCapsCalloc},
        {"heap_caps_free", RomTrap::HeapCapsFree}, {"usleep", RomTrap::Usleep}, {"sleep", RomTrap::Sleep},
        {"clock_gettime", RomTrap::ClockGetTime}, {"pthread_create", RomTrap::PthreadAny},
        {"pthread_attr_init", RomTrap::PthreadAny}, {"pthread_attr_setstacksize", RomTrap::PthreadAny},
        {"pthread_detach", RomTrap::PthreadAny}, {"pthread_join", RomTrap::PthreadAny},
        {"pthread_exit", RomTrap::Exit}, {"zm_host_log_line", RomTrap::LogLine},
        {"zm_host_task_exit", RomTrap::TaskExit}, {"zm_host_return", RomTrap::Return},
        {"zm_host_link_read", RomTrap::LinkRead}, {"zm_host_link_write", RomTrap::LinkWrite},
        {"zm_host_delay", RomTrap::Delay}, {"zm_host_millis", RomTrap::Millis},
        {"zm_host_ft_read", RomTrap::FtRead}, {"zm_host_ft_write", RomTrap::FtWrite},
        {"zm_host_ft_opened", RomTrap::FtOpened}, {"__divsf3", RomTrap::DivSf3},
        {"__ieee754_sqrtf", RomTrap::SqrtF},
    };
    for (const auto& entry : kTable)
    {
        if (std::strcmp(entry.name, name) == 0)
            return entry.trap;
    }
    return RomTrap::Unknown;
}

/// The trap kind of each ROM trap table entry, resolved once (the guest calls some of them, `__divsf3`, a million
/// times in a few seconds: a name lookup per call was 7 % of the ESP's host time)
RomTrap RomTrapAt(uint32_t index)
{
    static const std::vector<RomTrap> kByIndex = [] {
        std::vector<RomTrap> table(kEspRomTrapCount);
        for (size_t i = 0; i < kEspRomTrapCount; ++i)
            table[i] = RomTrapByName(kEspRomTraps[i].name);
        return table;
    }();
    return kByIndex[index];
}

int16_t PcmSample(float value)
{
    value = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
    return static_cast<int16_t>(value * 32767.0f);
}

float FloatFromBits(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

/// A float result as the board's FPU leaves it: any NaN is the default quiet NaN (as the interpreter's FPU)
uint32_t BitsFromFloat(float value)
{
    if (value != value)
        return 0x7FC00000u;
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

/// The board's ELF host: firmware tables only (zm_* glue names are not exports)
class ElfHost : public IEspElfHost
{
public:
    explicit ElfHost(EspMemory& memory) : _memory(memory) {}
    uint32_t AllocPsram(uint32_t size) override { return _memory.HeapCapsMalloc(size, EspMemory::kCapSpiram); }
    void FreePsram(uint32_t address) override { _memory.HeapCapsFree(address); }
    uint8_t* Bytes(uint32_t address, uint32_t size) override { return _memory.Bytes(address, size); }
    uint32_t FindSymbol(const std::string& name) override
    {
        if (name.compare(0, 3, "zm_") == 0)
            return 0;
        for (size_t i = 0; i < kEspRomExportCount; i++)
        {
            if (name == kEspRomExports[i].name)
                return kEspRomExports[i].address;
        }
        for (size_t i = 0; i < kEspRomTrapCount; i++)
        {
            if (name == kEspRomTraps[i].name)
                return kEspRomTraps[i].address;
        }
        return 0;
    }

private:
    EspMemory& _memory;
};

}  // namespace

/// region <Construction, boot>

EspAppHost::EspAppHost(std::unique_ptr<IEspCpu> cpu, EspAppHostPorts ports, uint32_t mhz)
    : _cpu(std::move(cpu)), _ports(std::move(ports)), _mhz(mhz ? mhz : kDefaultMhz)
{
    _cpu->Map(EspMemory::kPsramDbus, EspMemory::kPsramSize, _memory.PsramBytes().data(), true);
    _cpu->Map(EspMemory::kPsramIbus, EspMemory::kPsramSize, _memory.PsramBytes().data(), true);
    _cpu->Map(EspMemory::kSramBase, EspMemory::kSramSize, _memory.SramBytes().data(), true);
    _cpu->Map(kEspRomBase, kEspRomSize, _memory.RomBytes().data(), true);
    _cpu->SetTrapRange(EspMemory::kTrapPage, EspMemory::kTrapPageSize);
    _romTrapBase = kEspTrapBase;
    Reset();
}

EspAppHost::~EspAppHost() = default;

uint32_t EspAppHost::TrapAddress(const char* romTrap) const
{
    for (size_t i = 0; i < kEspRomTrapCount; i++)
    {
        if (std::strcmp(kEspRomTraps[i].name, romTrap) == 0)
            return kEspRomTraps[i].address;
    }
    return 0;
}

uint32_t EspAppHost::RomSymbol(const char* name) const
{
    for (size_t i = 0; i < kEspRomExportCount; i++)
    {
        if (std::strcmp(kEspRomExports[i].name, name) == 0)
            return kEspRomExports[i].address;
    }
    return 0;
}

void EspAppHost::Reset()
{
    _tasks.clear();
    _loop = _net = nullptr;
    for (SyncObject& object : _sync)
        object = SyncObject{};
    _app = AppState{};
    _pcm = Pcm{};
    _nowUs = 0;
    _nextPollUs = 0;
    _instructions = 0;
    _readySerial = 0;
    _nextTaskId = 1;
    _mediaFirst = _mediaLast = 0;
    _inCommand = false;
    _lastError.clear();
    _memory.Reset();
    _cpu->InvalidateCode(EspMemory::kPsramDbus, EspMemory::kPsramSize);
    _cpu->InvalidateCode(EspMemory::kPsramIbus, EspMemory::kPsramSize);
    _cpu->InvalidateCode(EspMemory::kSramBase, EspMemory::kSramSize);
    _cpu->InvalidateCode(kEspRomBase, kEspRomSize);
    BuildCore();
}

void EspAppHost::BuildCore()
{
    _sentinel = TrapAddress("zm_host_return");

    const uint32_t firmware = _memory.HeapCapsMalloc(static_cast<uint32_t>(std::strlen(kFirmware) + 1),
                                                     EspMemory::kCapInternal);
    _memory.WriteString(firmware, kFirmware);

    _core = _memory.HeapCapsMalloc(kZmCoreSize, EspMemory::kCapInternal);
    uint8_t* core = _memory.Bytes(_core, kZmCoreSize);
    std::memset(core, 0, kZmCoreSize);
    core[0] = static_cast<uint8_t>(kZmCoreSize);
    core[1] = static_cast<uint8_t>(kZmCoreSize >> 8);
    core[2] = 1;  // abi_major
    core[4] = static_cast<uint8_t>(kAbiMinor);
    _memory.Write32(_core + 8, firmware);
    for (uint32_t slot = 0; slot < ZmSlotCount; slot++)
        _memory.Write32(_core + 12 + 4 * slot, EspMemory::kZmTrapBase + 4 * slot);
    _memory.Write32(_core + 12 + 4 * ZmLog, RomSymbol("zm_rom_log"));
    _memory.Write32(_core + 12 + 4 * ZmFtOpen, RomSymbol("zm_rom_ft_open"));

    _commandBuffer = _memory.HeapCapsMalloc(8192 + 16, EspMemory::kCapInternal);
    _argvBlock = _memory.HeapCapsMalloc(64, EspMemory::kCapInternal);

    _loop = NewTask("loopTask", kLoopPriority, true, kLoopStack, false);
    _net = NewTask("zifi-net", kNetPriority, true, kNetStack, false);
}

/// endregion

/// region <Scheduler>

EspAppHost::Task* EspAppHost::NewTask(const std::string& name, uint8_t priority, bool host, uint32_t stackSize,
                                      bool psramStack)
{
    const uint32_t stack =
        _memory.HeapCapsMalloc(stackSize, psramStack ? EspMemory::kCapSpiram : EspMemory::kCapInternal);
    if (stack == 0)
        return nullptr;
    auto task = std::make_unique<Task>();
    task->id = _nextTaskId++;
    task->name = name;
    task->priority = priority;
    task->host = host;
    task->stack = stack;
    task->stackSize = stackSize;
    task->state = host ? TaskState::HostIdle : TaskState::Ready;
    task->readySerial = ++_readySerial;
    _cpu->ResetState();
    _cpu->SaveState(task->cpu);
    _tasks.push_back(std::move(task));
    return _tasks.back().get();
}

EspAppHost::Task* EspAppHost::FindTask(uint32_t id)
{
    for (auto& task : _tasks)
    {
        if (task->id == id)
            return task.get();
    }
    return nullptr;
}

EspAppHost::Task* EspAppHost::PickReady(const Task* except)
{
    Task* best = nullptr;
    for (auto& task : _tasks)
    {
        if (task.get() == except || task->state != TaskState::Ready)
            continue;
        if (best == nullptr || task->priority > best->priority ||
            (task->priority == best->priority && task->readySerial < best->readySerial))
            best = task.get();
    }
    return best;
}

void EspAppHost::MakeReady(Task& task)
{
    task.state = TaskState::Ready;
    task.readySerial = ++_readySerial;
}

void EspAppHost::Block(Task& task, TaskState state)
{
    task.state = state;
}

uint32_t EspAppHost::CurrentMs() const
{
    return static_cast<uint32_t>((_nowUs + _sliceUsedCycles / _mhz) / 1000);
}

void EspAppHost::WakeDue()
{
    for (auto& owned : _tasks)
    {
        Task& task = *owned;
        if (task.wakeUs > _nowUs)
            continue;
        switch (task.state)
        {
            case TaskState::Sleeping:
                MakeReady(task);
                break;
            case TaskState::WaitSignal:
                if (task.wakeUs != UINT64_MAX)
                {
                    task.pendingReturn = static_cast<uint32_t>(kZmTimeout);
                    task.hasPending = true;
                    MakeReady(task);
                }
                break;
            case TaskState::WaitPcm:
                if (PcmWriteStep(task))
                    MakeReady(task);
                break;
            default:
                break;
        }
    }
}

void EspAppHost::QueueHostCall(Task& task, HostCall call)
{
    task.calls.push_back(std::move(call));
    if (task.state == TaskState::HostIdle)
        StartNextHostCall(task);
}

bool EspAppHost::StartNextHostCall(Task& task)
{
    if (task.calls.empty())
    {
        task.callRunning = false;
        task.state = TaskState::HostIdle;
        return false;
    }
    _cpu->LoadState(task.cpu);
    BeginHostCall(task);
    _cpu->SaveState(task.cpu);
    MakeReady(task);
    return true;
}

void EspAppHost::BeginHostCall(Task& task)
{
    const HostCall& call = task.calls.front();
    if (call.payload)
    {
        const auto& bytes = *call.payload;
        if (!bytes.empty())
            std::memcpy(_memory.Bytes(_commandBuffer, static_cast<uint32_t>(bytes.size())), bytes.data(), bytes.size());
        _inCommand = true;
    }
    _cpu->BeginCall(call.pc, call.args, call.count, (task.stack + task.stackSize - 16) & ~15u, _sentinel);
    task.callRunning = true;
}

void EspAppHost::Reap()
{
    _tasks.erase(std::remove_if(_tasks.begin(), _tasks.end(),
                                [](const std::unique_ptr<Task>& task) {
                                    return !task->host && task->state == TaskState::Done;
                                }),
                 _tasks.end());
}

#ifdef UNREAL_XTENSA_PROFILE
namespace
{
uint64_t ProfileNs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}
}  // namespace

void EspAppHost::DumpProfile()
{
    const char* prefix = std::getenv("UNREAL_XTENSA_PROFILE");
    if (!prefix || !*prefix)
        return;
    const uint64_t second = _nowUs / 1'000'000;
    char path[1024];
    std::snprintf(path, sizeof(path), "%s.%p.%05llu.host", prefix, static_cast<void*>(this),
                  static_cast<unsigned long long>(second));
    if (FILE* f = std::fopen(path, "w"))
    {
        const Profile& p = _profile;
        std::fprintf(f, "us %llu instructions %llu\n", static_cast<unsigned long long>(_nowUs),
                     static_cast<unsigned long long>(_instructions));
        std::fprintf(f, "budget %llu %llu used %llu %llu\n", static_cast<unsigned long long>(p.budget[0]),
                     static_cast<unsigned long long>(p.budget[1]), static_cast<unsigned long long>(p.used[0]),
                     static_cast<unsigned long long>(p.used[1]));
        std::fprintf(f, "runs %llu run_ns %llu trap_ns %llu switch_ns %llu pcm_ns %llu advance_ns %llu\n",
                     static_cast<unsigned long long>(p.runs), static_cast<unsigned long long>(p.runNs),
                     static_cast<unsigned long long>(p.trapNs), static_cast<unsigned long long>(p.switchNs),
                     static_cast<unsigned long long>(p.pcmNs), static_cast<unsigned long long>(p.advanceNs));
        for (const auto& kv : p.traps)
            std::fprintf(f, "trap %08x %llu %llu\n", kv.first, static_cast<unsigned long long>(kv.second.first),
                         static_cast<unsigned long long>(kv.second.second));
        std::fclose(f);
    }
    std::snprintf(path, sizeof(path), "%s.%p.%05llu.blocks", prefix, static_cast<void*>(this),
                  static_cast<unsigned long long>(second));
    _cpu->DumpProfile(path);
}
#endif

void EspAppHost::Advance(uint64_t nowUs)
{
#ifdef UNREAL_XTENSA_PROFILE
    const uint64_t advanceStart = ProfileNs();
#endif
    while (_nowUs < nowUs)
    {
        const uint64_t sliceEnd = std::min<uint64_t>(nowUs, (_nowUs / 1000 + 1) * 1000);
        Reap();
        WakeDue();

        // zifi-net: poll every 2 ms while an app runs (pollNetworkCore skips when the app lock is busy)
        if (_app.started && _app.poll != 0 && _nowUs >= _nextPollUs && _net->state == TaskState::HostIdle &&
            _loop->state == TaskState::HostIdle)
        {
            HostCall call;
            call.pc = _app.poll;
            call.args[0] = static_cast<uint32_t>(_nowUs / 1000);
            call.count = 1;
            QueueHostCall(*_net, std::move(call));
            _nextPollUs = _nowUs + kPollPeriodUs;
        }

        const uint64_t budget = (sliceEnd - _nowUs) * _mhz;
        Task* first = nullptr;
#ifdef UNREAL_XTENSA_PROFILE
        _profileCore = 0;
        _profile.budget[0] += budget;
        _profile.budget[1] += budget;
#endif
        RunCore(budget, first, nullptr);
        Task* second = nullptr;
#ifdef UNREAL_XTENSA_PROFILE
        _profileCore = 1;
#endif
        RunCore(budget, second, first);
        _sliceUsedCycles = 0;

#ifdef UNREAL_XTENSA_PROFILE
        const uint64_t pcmStart = ProfileNs();
#endif
        PcmPlay(_nowUs, sliceEnd);
#ifdef UNREAL_XTENSA_PROFILE
        _profile.pcmNs += ProfileNs() - pcmStart;
#endif
        _nowUs = sliceEnd;
#ifdef UNREAL_XTENSA_PROFILE
        if (_nowUs >= _profileNextUs)
        {
            _profile.advanceNs += ProfileNs() - advanceStart;
            DumpProfile();
            _profile.advanceNs -= ProfileNs() - advanceStart;
            _profileNextUs = (_nowUs / 1'000'000 + 1) * 1'000'000;
        }
#endif

        // An app that hears nothing from the Z80 for 5 s is stopped (module_app.cpp:451-456)
        // (inside the firmware model, Vdac4EspFirmware owns that rule: ports.appDone is set there)
        if (!_ports.appDone && _app.started && !_inCommand && _loop->calls.empty() &&
            _nowUs - _app.lastCommandUs > kIdleStopMs * 1000)
        {
            Log("app " + _app.name + ": no commands for 5 s, stopping");
            StopApp();
        }
    }
#ifdef UNREAL_XTENSA_PROFILE
    _profile.advanceNs += ProfileNs() - advanceStart;
#endif
}

void EspAppHost::RunCore(uint64_t budget, Task*& running, const Task* other)
{
    uint64_t used = 0;
    Task* task = PickReady(other);
    while (task != nullptr && used < budget)
    {
#ifdef UNREAL_XTENSA_PROFILE
        uint64_t mark = ProfileNs();
#endif
        _cpu->LoadState(task->cpu);
        if (task->hasPending)
        {
            _cpu->ReturnFromTrap(task->pendingReturn);
            task->hasPending = false;
        }
        bool live = true;
        while (used < budget)
        {
            _sliceUsedCycles = used;
#ifdef UNREAL_XTENSA_PROFILE
            const uint64_t runStart = ProfileNs();
            _profile.switchNs += runStart - mark;
#endif
            const IEspCpu::Result result = _cpu->Run(budget - used);
#ifdef UNREAL_XTENSA_PROFILE
            mark = ProfileNs();
            _profile.runNs += mark - runStart;
            ++_profile.runs;
            _profile.used[_profileCore] += result.executed;
#endif
            used += result.executed;
            _instructions += result.executed;
            _sliceUsedCycles = used;
            if (result.reason == IEspCpu::Stop::Budget)
                break;
            if (result.reason == IEspCpu::Stop::Trap)
            {
#ifdef UNREAL_XTENSA_PROFILE
                const bool handled = HandleTrap(*task, result.pc);
                const uint64_t trapEnd = ProfileNs();
                auto& trap = _profile.traps[result.pc];
                ++trap.first;
                trap.second += trapEnd - mark;
                _profile.trapNs += trapEnd - mark;
                mark = trapEnd;
                if (!handled)
#else
                if (!HandleTrap(*task, result.pc))
#endif
                {
                    live = task->state != TaskState::Done;
                    break;
                }
                continue;
            }
            Fault(*task, result);
            live = false;
            break;
        }
        if (live)
            _cpu->SaveState(task->cpu);
#ifdef UNREAL_XTENSA_PROFILE
        _profile.switchNs += ProfileNs() - mark;
#endif
        running = task;
        if (task->state == TaskState::Ready)
        {
            if (used >= budget)
                break;
            continue;
        }
        task = PickReady(other);
    }
}

void EspAppHost::Fault(Task& task, const IEspCpu::Result& result)
{
    static const char* const kReasons[] = {"budget", "trap", "break", "illegal instruction", "unimplemented instruction",
                                           "memory fault", "privileged instruction"};
    char text[160];
    std::snprintf(text, sizeof(text), "task %s: %s at PC %08X (address %08X)", task.name.c_str(),
                  kReasons[static_cast<int>(result.reason)], result.pc, result.faultAddress);
    _lastError = text;
    Log(text);
    // The board would panic and reboot; the HLE stops the app and keeps the firmware running. A host task fails
    // its running call (main / start / command report the error) and goes idle.
    if (task.host)
    {
        HostCall call;
        if (!task.calls.empty())
        {
            call = std::move(task.calls.front());
            task.calls.pop_front();
        }
        task.callRunning = false;
        task.hasPending = false;
        task.state = TaskState::HostIdle;
        _cpu->ResetState();
        _cpu->SaveState(task.cpu);
        if (call.done)
            call.done(static_cast<uint32_t>(kZmError));
    }
    else
    {
        task.state = TaskState::Done;
    }
    if (_app.address != 0 || _app.image.textBlock != 0)
        ForceCleanup();
}

/// endregion

/// region <Traps>

bool EspAppHost::HandleTrap(Task& task, uint32_t pc)
{
    if (pc >= EspMemory::kZmTrapBase)
    {
        const uint32_t slot = (pc - EspMemory::kZmTrapBase) / 4;
        if (slot < ZmSlotCount)
            return HandleZm(task, slot);
    }
    else if (pc >= _romTrapBase)
    {
        const uint32_t index = (pc - _romTrapBase) / 4;
        if (index < kEspRomTrapCount)
            return HandleRomTrap(task, index);
    }
    IEspCpu::Result result;
    result.reason = IEspCpu::Stop::MemoryFault;
    result.pc = pc;
    result.faultAddress = pc;
    Fault(task, result);
    return false;
}

bool EspAppHost::HandleRomTrap(Task& task, uint32_t index)
{
    const RomTrap trap = RomTrapAt(index);
    IEspCpu& cpu = *_cpu;
    auto sleepUs = [&](uint64_t us) {
        task.wakeUs = _nowUs + _sliceUsedCycles / _mhz + std::max<uint64_t>(us, 1000);
        task.pendingReturn = 0;
        task.hasPending = true;
        Block(task, TaskState::Sleeping);
        return false;
    };
    switch (trap)
    {
        case RomTrap::GetReent:
            cpu.ReturnFromTrap(kEspRomImpureData);
            return true;
        case RomTrap::Exit:
        {
            IEspCpu::Result result;
            result.reason = IEspCpu::Stop::Break;
            result.pc = kEspRomTraps[index].address;
            _lastError = "exit() called";
            Fault(task, result);
            return false;
        }
        case RomTrap::Write:
        {
            const uint32_t length = cpu.Arg(2);
            const uint8_t* bytes = _memory.Bytes(cpu.Arg(1), length);
            if (bytes != nullptr)
                Log(std::string(reinterpret_cast<const char*>(bytes), length));
            cpu.ReturnFromTrap(length);
            return true;
        }
        case RomTrap::Sbrk:
        case RomTrap::Lseek:
        case RomTrap::Kill:
        case RomTrap::Fstat:
        case RomTrap::Close:
        case RomTrap::PthreadAny:
            cpu.ReturnFromTrap(static_cast<uint32_t>(-1));
            return true;
        case RomTrap::Read:
            cpu.ReturnFromTrap(0);
            return true;
        case RomTrap::GetPid:
        case RomTrap::IsATty:
            cpu.ReturnFromTrap(1);
            return true;
        case RomTrap::GetTimeOfDay:
        case RomTrap::ClockGetTime:
        {
            const uint64_t us = _nowUs + _sliceUsedCycles / _mhz;
            const uint32_t out = trap == RomTrap::GetTimeOfDay ? cpu.Arg(0) : cpu.Arg(1);
            _memory.Write32(out, static_cast<uint32_t>(us / 1'000'000));
            _memory.Write32(out + 4, 0);  // time_t is 64-bit in this newlib
            _memory.Write32(out + 8, static_cast<uint32_t>(trap == RomTrap::GetTimeOfDay ? us % 1'000'000
                                                                                         : (us % 1'000'000) * 1000));
            cpu.ReturnFromTrap(0);
            return true;
        }
        case RomTrap::Malloc:
            cpu.ReturnFromTrap(_memory.HeapCapsMalloc(cpu.Arg(0), 0));
            return true;
        case RomTrap::MallocR:
            cpu.ReturnFromTrap(_memory.HeapCapsMalloc(cpu.Arg(1), 0));
            return true;
        case RomTrap::HeapCapsMalloc:
            cpu.ReturnFromTrap(_memory.HeapCapsMalloc(cpu.Arg(0), cpu.Arg(1)));
            return true;
        case RomTrap::Calloc:
        case RomTrap::CallocR:
        case RomTrap::HeapCapsCalloc:
        {
            const int at = trap == RomTrap::CallocR ? 1 : 0;
            const uint64_t size = static_cast<uint64_t>(cpu.Arg(at)) * cpu.Arg(at + 1);
            uint32_t block = 0;
            if (size <= UINT32_MAX)
            {
                block = _memory.HeapCapsMalloc(static_cast<uint32_t>(size),
                                               trap == RomTrap::HeapCapsCalloc ? cpu.Arg(2) : 0);
                if (block != 0)
                    std::memset(_memory.Bytes(block, static_cast<uint32_t>(size)), 0, static_cast<size_t>(size));
            }
            cpu.ReturnFromTrap(block);
            return true;
        }
        case RomTrap::Realloc:
        case RomTrap::ReallocR:
        {
            const int at = trap == RomTrap::ReallocR ? 1 : 0;
            const uint32_t old = cpu.Arg(at);
            const uint32_t size = cpu.Arg(at + 1);
            uint32_t fresh = 0;
            if (old == 0)
                fresh = _memory.HeapCapsMalloc(size, 0);
            else if (size == 0)
                _memory.HeapCapsFree(old);
            else
            {
                fresh = _memory.HeapCapsMalloc(size, 0);
                if (fresh != 0)
                {
                    const uint32_t keep = std::min(_memory.AllocatedSize(old), size);
                    std::memmove(_memory.Bytes(fresh, keep), _memory.Bytes(old, keep), keep);
                    _memory.HeapCapsFree(old);
                }
            }
            cpu.ReturnFromTrap(fresh);
            return true;
        }
        case RomTrap::Free:
        case RomTrap::HeapCapsFree:
            _memory.HeapCapsFree(cpu.Arg(0));
            cpu.ReturnFromTrap(0);
            return true;
        case RomTrap::FreeR:
            _memory.HeapCapsFree(cpu.Arg(1));
            cpu.ReturnFromTrap(0);
            return true;
        case RomTrap::Usleep:
            return sleepUs(cpu.Arg(0));
        case RomTrap::Sleep:
            return sleepUs(static_cast<uint64_t>(cpu.Arg(0)) * 1'000'000);
        case RomTrap::Delay:
            return sleepUs(static_cast<uint64_t>(std::max<uint32_t>(cpu.Arg(0), 1)) * 1000);
        case RomTrap::Millis:
            cpu.ReturnFromTrap(CurrentMs());
            return true;
        case RomTrap::LogLine:
            Log(_app.name + ": " + _memory.ReadString(cpu.Arg(0), 136));
            cpu.ReturnFromTrap(0);
            return true;
        case RomTrap::TaskExit:
            task.state = TaskState::Done;
            _memory.HeapCapsFree(task.stack);
            return false;
        case RomTrap::Return:
        {
            const uint32_t result = cpu.CallResult();
            if (!task.calls.empty())
            {
                HostCall call = std::move(task.calls.front());
                task.calls.pop_front();
                task.callRunning = false;
                if (call.done)
                    call.done(result);
            }
            if (task.state == TaskState::Done || task.calls.empty())
            {
                if (task.state != TaskState::Done)
                    task.state = TaskState::HostIdle;
                _cpu->SaveState(task.cpu);
                return false;
            }
            // The next queued call on this host task
            BeginHostCall(task);
            return true;
        }
        case RomTrap::LinkRead:
            cpu.ReturnFromTrap(_ports.linkRead ? _ports.linkRead(cpu.Arg(0)) : 0);
            return true;
        case RomTrap::LinkWrite:
            if (_ports.linkWrite)
                _ports.linkWrite(cpu.Arg(0), cpu.Arg(1));
            cpu.ReturnFromTrap(0);
            return true;
        case RomTrap::FtRead:
        {
            const uint32_t address = cpu.Arg(0);
            const uint32_t out = cpu.Arg(1);
            if ((address & 3) != 0 || _memory.Bytes(out, 4) == nullptr || !_ports.linkWrite)
            {
                cpu.ReturnFromTrap(static_cast<uint32_t>(kZmError));
                return true;
            }
            _ports.linkWrite(kLinkFtAddr, address);
            _memory.Write32(out, _ports.linkRead(kLinkFtData));
            cpu.ReturnFromTrap(kZmOk);
            return true;
        }
        case RomTrap::FtWrite:
            FtWrite(cpu.Arg(0), cpu.Arg(1), cpu.Arg(2));
            cpu.ReturnFromTrap(kZmOk);
            return true;
        case RomTrap::FtOpened:
            _app.ftOpened = true;
            if (_ports.ftOpened)
                _ports.ftOpened();
            cpu.ReturnFromTrap(0);
            return true;
        // The board computes these with the FPU's DIV0 / NEXP01 / DIVN and SQRT0 sequences, which round correctly:
        // the host's IEEE single division and square root give the same bits
        case RomTrap::DivSf3:
            cpu.ReturnFromTrap(BitsFromFloat(FloatFromBits(cpu.Arg(0)) / FloatFromBits(cpu.Arg(1))));
            return true;
        case RomTrap::SqrtF:
            cpu.ReturnFromTrap(BitsFromFloat(std::sqrt(FloatFromBits(cpu.Arg(0)))));
            return true;
        case RomTrap::Unknown:
        default:
        {
            _lastError = std::string("unserved firmware function ") + kEspRomTraps[index].name;
            IEspCpu::Result result;
            result.reason = IEspCpu::Stop::Unimplemented;
            result.pc = kEspRomTraps[index].address;
            Fault(task, result);
            return false;
        }
    }
}

void EspAppHost::FtWrite(uint32_t address, uint32_t words, uint32_t count)
{
    if (!_ports.linkWrite || count == 0)
        return;
    const uint8_t* bytes = _memory.Bytes(words, count * 4);
    if (bytes == nullptr)
        return;
    if (_ports.linkFtWrite)
    {
        _ports.linkFtWrite(address, bytes, count);
        return;
    }
    _ports.linkWrite(kLinkFtAddr, address);
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t* p = bytes + 4 * i;
        _ports.linkWrite(kLinkFtData, static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                          (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
    }
}

bool EspAppHost::HandleZm(Task& task, uint32_t slot)
{
    IEspCpu& cpu = *_cpu;
    auto ret = [&](uint32_t value) {
        cpu.ReturnFromTrap(value);
        return true;
    };
    auto retInt = [&](int value) { return ret(static_cast<uint32_t>(value)); };
    switch (slot)
    {
        case ZmAlloc:
            return ret(CoreAlloc(cpu.Arg(0), cpu.Arg(1)));
        case ZmRealloc:
            return ret(CoreRealloc(cpu.Arg(0), cpu.Arg(1)));
        case ZmFree:
            CoreFree(cpu.Arg(0));
            return ret(0);
        case ZmMillis:
            return ret(CurrentMs());
        case ZmDelay:
        {
            const uint32_t ms = std::max<uint32_t>(cpu.Arg(0), 1);
            task.wakeUs = _nowUs + _sliceUsedCycles / _mhz + static_cast<uint64_t>(ms) * 1000;
            task.pendingReturn = 0;
            task.hasPending = true;
            Block(task, TaskState::Sleeping);
            return false;
        }
        case ZmTaskStart:
            return retInt(TaskStart(cpu.Arg(0), cpu.Arg(1), cpu.Arg(2), static_cast<uint8_t>(cpu.Arg(3))));
        case ZmMutexCreate:
            return ret(SyncCreate(true));
        case ZmSignalCreate:
            return ret(SyncCreate(false));
        case ZmMutexLock:
        {
            SyncObject* object = Sync(cpu.Arg(0));
            if (object == nullptr || !object->mutex)
                return ret(0);
            if (object->owner == 0)
            {
                object->owner = task.id;
                return ret(0);
            }
            task.waitObject = object->handle;
            task.pendingReturn = 0;
            task.hasPending = true;
            Block(task, TaskState::WaitMutex);
            return false;
        }
        case ZmMutexUnlock:
        {
            SyncObject* object = Sync(cpu.Arg(0));
            if (object != nullptr && object->mutex && object->owner == task.id)
                SyncRelease(*object);
            return ret(0);
        }
        case ZmSignalGive:
        {
            SyncObject* object = Sync(cpu.Arg(0));
            if (object != nullptr && !object->mutex)
                SyncRelease(*object);
            return ret(0);
        }
        case ZmSignalWait:
        {
            SyncObject* object = Sync(cpu.Arg(0));
            const uint32_t ms = cpu.Arg(1);
            if (object == nullptr || object->mutex)
                return retInt(kZmError);
            if (object->given)
            {
                object->given = false;
                return retInt(kZmOk);
            }
            if (ms == 0)
                return retInt(kZmTimeout);
            task.waitObject = object->handle;
            task.wakeUs = ms == UINT32_MAX ? UINT64_MAX
                                           : _nowUs + _sliceUsedCycles / _mhz + static_cast<uint64_t>(ms) * 1000;
            task.pendingReturn = static_cast<uint32_t>(kZmTimeout);
            task.hasPending = true;
            Block(task, TaskState::WaitSignal);
            return false;
        }
        case ZmReply:
        {
            if (!_inCommand)
                return retInt(kZmError);
            const uint32_t length = cpu.Arg(2) & 0xFFFF;
            const uint8_t* data = _memory.Bytes(cpu.Arg(1), length);
            if (data == nullptr && length != 0)
                return retInt(kZmError);
            return retInt(_ports.reply ? _ports.reply(static_cast<uint8_t>(cpu.Arg(0)), data, length) : kZmOk);
        }
        case ZmEvent:
        {
            const uint32_t length = cpu.Arg(2) & 0xFFFF;
            const uint8_t* data = _memory.Bytes(cpu.Arg(1), length);
            if (data == nullptr && length != 0)
                return retInt(kZmError);
            return retInt(_ports.event ? _ports.event(static_cast<uint8_t>(cpu.Arg(0)), data, length) : kZmOk);
        }
        case ZmPcmOpen:
        {
            const uint32_t format = cpu.Arg(0);
            if (_memory.Bytes(format, 5) == nullptr)
                return retInt(kZmError);
            const uint32_t rate = _memory.Read32(format);
            const uint8_t channels = *_memory.Bytes(format + 4, 1);
            if (rate < 8000 || rate > 96000 || (channels != 1 && channels != 2))
                return retInt(kZmError);
            _pcm.open = true;
            _pcm.paused = false;
            _pcm.rate = rate;
            _pcm.channels = channels;
            _pcm.ring.assign(static_cast<size_t>(kPcmRingFrames) * 2, 0);
            _pcm.head = _pcm.queued = 0;
            _pcm.playedFraction = 0;
            return retInt(kZmOk);
        }
        case ZmPcmWrite:
            task.pcmAddress = cpu.Arg(0);
            task.pcmFrames = cpu.Arg(1);
            task.pcmAccepted = 0;
            task.pcmDeadlineUs = _nowUs + _sliceUsedCycles / _mhz + kPcmGiveUpUs;
            if (PcmWriteStep(task))
            {
                task.hasPending = false;
                return ret(task.pcmAccepted);
            }
            return false;
        case ZmPcmQueuedMs:
            return ret(_pcm.open ? static_cast<uint32_t>(static_cast<uint64_t>(_pcm.queued) * 1000 / _pcm.rate) : 0);
        case ZmPcmFlush:
            _pcm.queued = 0;
            return ret(0);
        case ZmPcmClose:
            _pcm.open = false;
            _pcm.queued = 0;
            return ret(0);
        case ZmPcmPause:
            _pcm.paused = cpu.Arg(0) != 0;
            return ret(0);
        case ZmSetVolume:
            _pcm.volume = static_cast<uint8_t>(cpu.Arg(0));
            return ret(0);
        case ZmVolume:
            return ret(_pcm.volume);
        case ZmPcmFaults:
            return ret(_pcm.faults);
        case ZmModuleOpen:
        case ZmResource:
        case ZmCoreLoad:
            return retInt(kZmNotFound);
        case ZmModuleClose:
        case ZmResourceRelease:
        case ZmNetClose:
        case ZmSockClose:
        case ZmSdClose:
        case ZmSd2Close:
            return ret(0);
        case ZmTemperature:
            return ret(450);  // 45.0 °C
        case ZmNetOpen:
        case ZmNetHeader:
        case ZmHandoffTake:
            return ret(0);
        case ZmFtWrite:
        {
            const uint32_t address = cpu.Arg(0);
            const uint32_t words = cpu.Arg(1);
            const uint32_t count = cpu.Arg(2);
            if (words == 0 || (address & 3) != 0 || (count != 0 && _memory.Bytes(words, count * 4) == nullptr))
                return retInt(kZmError);
            FtWrite(address, words, count);
            return retInt(kZmOk);
        }
        case ZmFtRead:
        {
            const uint32_t address = cpu.Arg(0);
            const uint32_t out = cpu.Arg(1);
            if (out == 0 || (address & 3) != 0 || !_ports.linkWrite)
                return retInt(kZmError);
            _ports.linkWrite(kLinkFtAddr, address);
            _memory.Write32(out, _ports.linkRead(kLinkFtData));
            return retInt(kZmOk);
        }
        case ZmFtHostCmd:
            if (_ports.linkWrite)
                _ports.linkWrite(kLinkFtHost, (cpu.Arg(0) & 0xFF) | ((cpu.Arg(1) & 0xFF) << 8));
            return retInt(kZmOk);
        case ZmFtClose:
            if (_app.ftOpened && _ports.linkRead)
            {
                _app.ftOpened = false;
                const uint32_t st = _ports.linkRead(kLinkFtStat);
                _ports.linkWrite(kLinkFtStat, ((st >> 27) & 1u) | (((st >> 13) & 1u) << 1));
            }
            return ret(0);
        case ZmCoreBack:
            return retInt(kZmOk);
        case ZmLinkRead:
        case ZmLinkWrite:
        case ZmLinkBurst:
        {
            const uint32_t address = cpu.Arg(0);
            const uint32_t words = cpu.Arg(1);
            const uint32_t count = cpu.Arg(2);
            if ((address & 3) != 0 || count == 0 || (slot != ZmLinkBurst && count > 16) ||
                _memory.Bytes(words, count * 4) == nullptr || !_ports.linkRead)
                return retInt(kZmError);
            for (uint32_t i = 0; i < count; i++)
            {
                if (slot == ZmLinkRead)
                    _memory.Write32(words + 4 * i, _ports.linkRead(address + 4 * i));
                else if (slot == ZmLinkWrite)
                    _ports.linkWrite(address + 4 * i, _memory.Read32(words + 4 * i));
                else
                    _ports.linkWrite(address, _memory.Read32(words + 4 * i));
            }
            return retInt(kZmOk);
        }
        case ZmUiAvailable:
            return ret(0);
        case ZmAppDone:
            if (_inCommand)
            {
                if (_ports.appDone)
                    _ports.appDone();
                else
                    _app.done = true;
            }
            return ret(0);
        case ZmLinkMedia:
        {
            const uint8_t first = static_cast<uint8_t>(cpu.Arg(0));
            const uint8_t last = static_cast<uint8_t>(cpu.Arg(1));
            const int result = _ports.mediaRange ? _ports.mediaRange(first, last) : kZmOk;
            if (result == kZmOk)
            {
                _mediaFirst = first;
                _mediaLast = last;
            }
            return retInt(result);
        }
        case ZmPcmLowLatency:
            return ret(cpu.Arg(0) ? 6 * 240 : 8 * 480);
        case ZmNetRead:
        case ZmSockOpen:
        case ZmSockSend:
        case ZmSockRecv:
        case ZmNetResolve:
        case ZmWifiInfo:
        case ZmSdList:
        case ZmSdRead:
        case ZmSdWrite:
        case ZmSdOpen:
        case ZmSdPread:
        case ZmSdPwrite:
        case ZmSdCreate:
        case ZmWifiRssi:
        case ZmSockOpenTls:
        case ZmHandoffPut:
        case ZmSd2Open:
        case ZmSd2Read:
        case ZmSd2Stream:
        default:
            return retInt(kZmError);
    }
}

/// endregion

/// region <zm_core_t pieces>

uint32_t EspAppHost::CoreAlloc(uint32_t size, uint32_t caps)
{
    const uint32_t heapCaps = (caps & kZmMemInternal) ? EspMemory::kCapInternal : EspMemory::kCapSpiram;
    const uint32_t otherCaps = (caps & kZmMemInternal) ? EspMemory::kCapSpiram : EspMemory::kCapInternal;
    if (size > UINT32_MAX - kAllocHeader)
        return 0;
    uint32_t header = _memory.HeapCapsMalloc(size + kAllocHeader, heapCaps);
    if (header == 0)
        header = _memory.HeapCapsMalloc(size + kAllocHeader, otherCaps);
    if (header == 0)
        return 0;
    _memory.Write32(header + 0, 0);
    _memory.Write32(header + 4, 0);
    _memory.Write32(header + 8, caps);
    _memory.Write32(header + 12, kAllocMagic);
    _app.allocations.push_back(header);
    return header + kAllocHeader;
}

void EspAppHost::CoreFree(uint32_t pointer)
{
    if (pointer < kAllocHeader)
        return;
    const uint32_t header = pointer - kAllocHeader;
    if (_memory.Read32(header + 12) != kAllocMagic)
        return;
    auto it = std::find(_app.allocations.begin(), _app.allocations.end(), header);
    if (it != _app.allocations.end())
        _app.allocations.erase(it);
    _memory.Write32(header + 12, 0);
    _memory.HeapCapsFree(header);
}

uint32_t EspAppHost::CoreRealloc(uint32_t pointer, uint32_t size)
{
    if (pointer == 0)
        return CoreAlloc(size, 0x01);
    if (size == 0)
    {
        CoreFree(pointer);
        return 0;
    }
    const uint32_t header = pointer - kAllocHeader;
    if (_memory.Read32(header + 12) != kAllocMagic)
        return 0;
    const uint32_t old = _memory.AllocatedSize(header) - kAllocHeader;
    const uint32_t fresh = CoreAlloc(size, _memory.Read32(header + 8));
    if (fresh == 0)
        return 0;
    const uint32_t keep = std::min(old, size);
    std::memcpy(_memory.Bytes(fresh, keep), _memory.Bytes(pointer, keep), keep);
    CoreFree(pointer);
    return fresh;
}

int EspAppHost::TaskStart(uint32_t fn, uint32_t arg, uint32_t stackBytes, uint8_t priority)
{
    int appTasks = 0;
    for (auto& task : _tasks)
        appTasks += !task->host && task->state != TaskState::Done;
    if (appTasks >= kMaxAppTasks)
        return kZmBusy;
    // The caller sits at its trap: creating the task must not disturb the loaded CPU state
    std::vector<uint8_t> caller;
    _cpu->SaveState(caller);
    const bool psram = (stackBytes & kTaskStackPsram) != 0;
    uint32_t size = std::max<uint32_t>(stackBytes & ~kTaskStackPsram, 2048);
    size = (size + 15u) & ~15u;
    Task* task = NewTask("zm-app", std::min<uint8_t>(priority, 20), false, size, psram);
    if (task == nullptr)
    {
        _cpu->LoadState(caller);
        return kZmNoMemory;
    }
    const uint32_t args[2] = {fn, arg};
    _cpu->LoadState(task->cpu);
    _cpu->BeginCall(RomSymbol("zm_rom_task_entry"), args, 2, (task->stack + size - 16) & ~15u, _sentinel);
    _cpu->SaveState(task->cpu);
    _cpu->LoadState(caller);
    return kZmOk;
}

uint32_t EspAppHost::SyncCreate(bool mutex)
{
    for (int i = 0; i < kMaxSync; i++)
    {
        SyncObject& object = _sync[i];
        if (object.used)
            continue;
        object = SyncObject{};
        object.used = true;
        object.mutex = mutex;
        // A small internal block stands for the FreeRTOS handle (the app only passes it back)
        object.handle = _memory.HeapCapsMalloc(16, EspMemory::kCapInternal);
        return object.handle;
    }
    return 0;
}

EspAppHost::SyncObject* EspAppHost::Sync(uint32_t handle)
{
    if (handle == 0)
        return nullptr;
    for (SyncObject& object : _sync)
    {
        if (object.used && object.handle == handle)
            return &object;
    }
    return nullptr;
}

void EspAppHost::SyncRelease(SyncObject& object)
{
    // The highest-priority waiter (FIFO among equals) takes it
    Task* waiter = nullptr;
    const TaskState waitState = object.mutex ? TaskState::WaitMutex : TaskState::WaitSignal;
    for (auto& task : _tasks)
    {
        if (task->state != waitState || task->waitObject != object.handle)
            continue;
        if (waiter == nullptr || task->priority > waiter->priority)
            waiter = task.get();
    }
    if (object.mutex)
    {
        object.owner = waiter != nullptr ? waiter->id : 0;
    }
    else if (waiter == nullptr)
    {
        object.given = true;
        return;
    }
    if (waiter != nullptr)
    {
        waiter->pendingReturn = object.mutex ? 0 : static_cast<uint32_t>(kZmOk);
        waiter->hasPending = true;
        MakeReady(*waiter);
    }
}

bool EspAppHost::PcmWriteStep(Task& task)
{
    const uint8_t channels = _pcm.channels == 1 ? 1 : 2;
    const size_t capacity = _pcm.ring.size() / 2;
    while (task.pcmAccepted < task.pcmFrames)
    {
        if (!_pcm.open || capacity == 0)
            break;
        uint32_t part = std::min<uint32_t>(task.pcmFrames - task.pcmAccepted, kPcmChunk);
        const size_t room = capacity - _pcm.queued;
        if (room == 0)
        {
            if (_nowUs >= task.pcmDeadlineUs)
                break;
            task.wakeUs = _nowUs + kPcmWaitUs;
            Block(task, TaskState::WaitPcm);
            return false;
        }
        part = static_cast<uint32_t>(std::min<size_t>(part, room));
        const uint32_t source = task.pcmAddress + task.pcmAccepted * channels * 4;
        const uint8_t* bytes = _memory.Bytes(source, part * channels * 4);
        if (bytes == nullptr)
            break;
        for (uint32_t i = 0; i < part; i++)
        {
            uint32_t bits;
            std::memcpy(&bits, bytes + (i * channels) * 4, 4);
            const float left = FloatFromBits(bits);
            float right = left;
            if (channels == 2)
            {
                std::memcpy(&bits, bytes + (i * channels + 1) * 4, 4);
                right = FloatFromBits(bits);
            }
            const size_t at = (_pcm.head + _pcm.queued) % capacity;
            _pcm.ring[at * 2] = PcmSample(left);
            _pcm.ring[at * 2 + 1] = PcmSample(right);
            _pcm.queued++;
        }
        task.pcmAccepted += part;
    }
    task.pendingReturn = task.pcmAccepted;
    task.hasPending = true;
    return true;
}

void EspAppHost::PcmPlay(uint64_t fromUs, uint64_t toUs)
{
    if (!_pcm.open || _pcm.paused || _pcm.ring.empty())
        return;
    // Frames due in [fromUs, toUs) at the open rate
    const uint64_t scaled = _pcm.playedFraction + (toUs - fromUs) * _pcm.rate;
    uint64_t due = scaled / 1'000'000;
    _pcm.playedFraction = scaled % 1'000'000;
    const size_t capacity = _pcm.ring.size() / 2;
    // audio_player.cpp:1246-1249: volume 0..255 scaled to 256 at unity
    const int32_t volume = _pcm.volume == 255 ? 256 : _pcm.volume;
    int16_t out[2 * 256];
    while (due > 0)
    {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(due, 256));
        for (size_t i = 0; i < n; i++)
        {
            int16_t left = 0, right = 0;
            if (_pcm.queued > 0)
            {
                left = static_cast<int16_t>((_pcm.ring[_pcm.head * 2] * volume) >> 8);
                right = static_cast<int16_t>((_pcm.ring[_pcm.head * 2 + 1] * volume) >> 8);
                _pcm.head = (_pcm.head + 1) % capacity;
                _pcm.queued--;
            }
            out[i * 2] = left;
            out[i * 2 + 1] = right;
        }
        if (_ports.audio)
            _ports.audio(out, n, _pcm.rate);
        due -= n;
    }
}

void EspAppHost::Log(const std::string& line)
{
    if (_ports.log)
        _ports.log(line);
}

/// endregion

/// region <App lifecycle>

void EspAppHost::StartApp(const std::string& name, std::vector<uint8_t> elf,
                          std::function<void(int, const std::string&, const std::string&)> done)
{
    // The same app already running is not restarted (startApp)
    if (_app.address != 0 && _app.name == name)
    {
        if (done)
            done(kZmOk, _memory.ReadString(_memory.Read32(_app.address + 8)),
                 _memory.ReadString(_memory.Read32(_app.address + 12)));
        return;
    }
    StopApp();
    LoadApp(name, elf, std::move(done));
}

void EspAppHost::LoadApp(const std::string& name, const std::vector<uint8_t>& elf,
                         std::function<void(int, const std::string&, const std::string&)> done)
{
    auto fail = [done](int result) {
        if (done)
            done(result, "", "");
    };
    ElfHost host(_memory);
    std::string error;
    if (!EspElfLoad(elf.data(), elf.size(), host, _app.image, error))
    {
        Log(name + ": relocate failed (" + error + ")");
        _lastError = error;
        fail(kZmError);
        return;
    }
    _cpu->InvalidateCode(_app.image.textBlock + kEspIbusOffset, _app.image.sections[EspElfImage::Text].size);
    _cpu->InvalidateCode(_app.image.textBlock, _app.image.sections[EspElfImage::Text].size);
    _app.name = name;

    // argv = {name, core, &app_out, NULL}
    const uint32_t argv = _argvBlock;
    const uint32_t program = _argvBlock + 16;
    const uint32_t appOut = _argvBlock + 32;
    _memory.WriteString(program, name.substr(0, 12));
    _memory.Write32(argv, program);
    _memory.Write32(argv + 4, _core);
    _memory.Write32(argv + 8, appOut);
    _memory.Write32(argv + 12, 0);
    _memory.Write32(appOut, 0);

    HostCall mainCall;
    mainCall.pc = _app.image.entry;
    mainCall.args[0] = 3;
    mainCall.args[1] = argv;
    mainCall.count = 2;
    mainCall.done = [this, appOut, fail, done, name](uint32_t entryResult) {
        const uint32_t app = _memory.Read32(appOut);
        const bool ok = static_cast<int32_t>(entryResult) == 1 && app != 0 && _memory.Bytes(app, 36) != nullptr &&
                        (_memory.Read32(app) >> 16) == 1 && (_memory.Read32(app) & 0xFFFF) >= 36 &&
                        _memory.Read32(app + 20) != 0;
        if (!ok)
        {
            Log(name + ": interface " + std::to_string(static_cast<int32_t>(entryResult)) + ", not 1");
            ForceCleanup();
            fail(kZmError);
            return;
        }
        _app.address = app;
        _app.commandFirst = *_memory.Bytes(app + 16, 1);
        _app.commandLast = *_memory.Bytes(app + 17, 1);
        _app.start = _memory.Read32(app + 20);
        _app.stop = _memory.Read32(app + 24);
        _app.command = _memory.Read32(app + 28);
        _app.poll = _memory.Read32(app + 32);
        if (_ports.declared)
            _ports.declared(_memory.ReadString(_memory.Read32(app + 8), 24),
                            _memory.ReadString(_memory.Read32(app + 12), 24), _app.commandFirst, _app.commandLast);

        HostCall startCall;
        startCall.pc = _app.start;
        startCall.args[0] = _core;
        startCall.count = 1;
        startCall.done = [this, done, name, app](uint32_t result) {
            const std::string appName = _memory.ReadString(_memory.Read32(app + 8), 24);
            const std::string version = _memory.ReadString(_memory.Read32(app + 12), 24);
            if (static_cast<int32_t>(result) != kZmOk)
            {
                Log(name + ": start failed (" + std::to_string(static_cast<int32_t>(result)) + ")");
                if (done)
                    done(static_cast<int32_t>(result), appName, version);
                StopApp();
                return;
            }
            _app.started = true;
            _app.lastCommandUs = _nowUs;
            char text[96];
            std::snprintf(text, sizeof(text), "app %s %s %s started, commands %02X..%02X", name.c_str(),
                          appName.c_str(), version.c_str(), _app.commandFirst, _app.commandLast);
            Log(text);
            if (done)
                done(kZmOk, appName, version);
        };
        // Runs next on the loop task, right after main returns
        // (the Return trap popped main already: start goes first)
        _loop->calls.push_front(std::move(startCall));
    };
    QueueHostCall(*_loop, std::move(mainCall));
}

bool EspAppHost::OwnsCommand(uint8_t command) const
{
    return _app.address != 0 && _app.command != 0 && command >= _app.commandFirst && command <= _app.commandLast;
}

void EspAppHost::Command(uint8_t command, const uint8_t* data, size_t size)
{
    if (!OwnsCommand(command))
        return;
    size = std::min<size_t>(size, 8192);
    HostCall call;
    call.pc = _app.command;
    call.args[0] = command;
    call.args[1] = _commandBuffer;
    call.args[2] = static_cast<uint32_t>(size);
    call.count = 3;
    call.payload = std::make_shared<std::vector<uint8_t>>(data, data + size);
    call.done = [this](uint32_t result) {
        _inCommand = false;
        _lastCommandResult = static_cast<int32_t>(result);
        if (_app.done)
        {
            _app.done = false;
            Log("app " + _app.name + ": done, unloading");
            StopApp();
        }
    };
    _app.lastCommandUs = _nowUs;
    QueueHostCall(*_loop, std::move(call));
}

bool EspAppHost::LoopIdle() const
{
    return _loop->calls.empty() && _loop->state == TaskState::HostIdle;
}

bool EspAppHost::RunUntilLoopIdle(uint64_t limitUs)
{
    // 20 us steps (4800 instructions at 240 MHz): the ESP's clock moves about as far as the call really takes
    constexpr uint64_t kStepUs = 20;
    const uint64_t until = _nowUs + limitUs;
    while (!LoopIdle())
    {
        if (_nowUs >= until)
            return false;
        Advance(_nowUs + kStepUs);
    }
    return true;
}

size_t EspAppHost::PendingCommands() const
{
    return _loop->calls.size();
}

void EspAppHost::StopApp()
{
    if (_app.address == 0 && _app.image.textBlock == 0)
        return;
    ForceCleanup();
}

void EspAppHost::ForceCleanup()
{
    // stopApp order: app pointer and media range, app->stop (not run: tasks are killed outright here), tasks,
    // ft_close, PCM, allocations, ELF
    _mediaFirst = _mediaLast = 0;
    for (auto& task : _tasks)
    {
        if (!task->host && task->state != TaskState::Done)
        {
            task->state = TaskState::Done;
            _memory.HeapCapsFree(task->stack);
        }
    }
    for (Task* host : {_loop, _net})
    {
        host->calls.clear();
        host->callRunning = false;
        host->hasPending = false;
        host->state = TaskState::HostIdle;
        _cpu->ResetState();
        _cpu->SaveState(host->cpu);
    }
    _inCommand = false;
    if (_app.ftOpened && _ports.linkRead)
    {
        const uint32_t st = _ports.linkRead(kLinkFtStat);
        _ports.linkWrite(kLinkFtStat, ((st >> 27) & 1u) | (((st >> 13) & 1u) << 1));
    }
    for (SyncObject& object : _sync)
    {
        if (object.used)
            _memory.HeapCapsFree(object.handle);
        object = SyncObject{};
    }
    _pcm.open = false;
    _pcm.queued = 0;
    for (uint32_t header : _app.allocations)
        _memory.HeapCapsFree(header);
    ElfHost host(_memory);
    EspElfUnload(host, _app.image);
    _app = AppState{};
}

/// endregion

bool EspAppHost::PeekAppByte(uint32_t elfAddress, uint8_t& value) const
{
    if (_app.address == 0)
        return false;
    const uint32_t address = _app.image.MapSymbol(elfAddress);
    const uint8_t* bytes = address != 0 ? _memory.Bytes(address, 1) : nullptr;
    if (bytes == nullptr)
        return false;
    value = *bytes;
    return true;
}

}  // namespace vdac4
