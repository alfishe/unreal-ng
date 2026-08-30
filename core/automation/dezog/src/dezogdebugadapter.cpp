#include "dezogdebugadapter.h"

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "common/uuid.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/video/screen.h"

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
// Unique temp file name per process/call for snapshot round-trips
std::filesystem::path makeSnapshotTempPath()
{
    static std::atomic<uint32_t> counter{0};
    // Process-unique prefix without platform pid headers: address of a static is
    // stable within a process and (with ASLR) differs across processes.
    auto processTag = reinterpret_cast<uintptr_t>(&counter);
    std::string name = "unreal-dezog-" + std::to_string(static_cast<unsigned long long>(processTag)) + "-" +
                       std::to_string(counter.fetch_add(1)) + ".sna";
    return std::filesystem::temp_directory_path() / name;
}

uint8_t memoryTypeFromAccess(dzrp::WatchAccess access)
{
    uint8_t type = BRK_MEM_NONE;
    if (static_cast<uint8_t>(access) & static_cast<uint8_t>(dzrp::WatchAccess::READ))
        type |= BRK_MEM_READ;
    if (static_cast<uint8_t>(access) & static_cast<uint8_t>(dzrp::WatchAccess::WRITE))
        type |= BRK_MEM_WRITE;
    return type;
}
}  // namespace

/// region <Construction / binding>

DezogDebugAdapter::DezogDebugAdapter(std::shared_ptr<Emulator> emulator) : _emulator(std::move(emulator))
{
    if (const char* env = std::getenv(HISTORY_ENV_VAR))
        _historyEnabled = !(env[0] == '0' && env[1] == '\0');

    subscribe();
}

DezogDebugAdapter::~DezogDebugAdapter()
{
    unsubscribe();

    // Best effort: leave no dezog-owned breakpoints behind in the emulator
    try
    {
        clearTemporaryBreakpoints();
    }
    catch (...)
    {
    }
}

void DezogDebugAdapter::setEmulator(std::shared_ptr<Emulator> emulator)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _emulator = std::move(emulator);
}

std::shared_ptr<Emulator> DezogDebugAdapter::getEmulator() const
{
    return resolveEmulator();
}

void DezogDebugAdapter::setPauseNotifier(PauseNotifier notifier)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _notifier = std::move(notifier);
}

std::shared_ptr<Emulator> DezogDebugAdapter::resolveEmulator() const
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_emulator)
            return _emulator;
    }

    EmulatorManager* manager = EmulatorManager::GetInstance();
    if (!manager)
        return nullptr;

    return manager->GetMostRecentEmulator();
}

void DezogDebugAdapter::ensureDebugEnabled(Emulator& emulator)
{
    if (!emulator.IsDebug())
        emulator.DebugOn();

    EmulatorContext* context = emulator.GetContext();
    if (context && context->pFeatureManager)
    {
        context->pFeatureManager->setFeature(Features::kDebugMode, true);
        context->pFeatureManager->setFeature(Features::kBreakpoints, true);
    }
}

/// endregion </Construction / binding>

/// region <Notifications>

void DezogDebugAdapter::subscribe()
{
    if (_subscribed)
        return;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&DezogDebugAdapter::onBreakpointMessage);
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, observerInstance, callback);
    _subscribed = true;
}

void DezogDebugAdapter::unsubscribe()
{
    if (!_subscribed)
        return;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callback = static_cast<ObserverCallbackMethod>(&DezogDebugAdapter::onBreakpointMessage);
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, observerInstance, callback);
    _subscribed = false;
}

void DezogDebugAdapter::onBreakpointMessage(int /*id*/, Message* message)
{
    if (!message || !message->obj)
        return;

    auto* payload = dynamic_cast<BreakpointTriggeredPayload*>(message->obj);
    if (!payload)
        return;

    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    // Instance filter: a nil payload id means a legacy (untagged) sender → accept;
    // otherwise only events from the emulator this adapter is bound to count.
    if (!payload->emulatorId.isNil() && !(payload->emulatorId == emulator->GetUUID()))
        return;

    uint16_t breakpointId = static_cast<uint16_t>(payload->_payloadNumber);
    uint16_t address = payload->address;

    // Classify: execution breakpoint vs. memory watchpoint
    dzrp::BreakReason reason = dzrp::BreakReason::BREAKPOINT;
    if (BreakpointManager* bpManager = emulator->GetBreakpointManager())
    {
        if (BreakpointDescriptor* desc = bpManager->GetBreakpointById(breakpointId))
        {
            const Z80State* z80 = emulator->GetZ80State();
            bool isExec = (desc->memoryType & BRK_MEM_EXECUTE) && z80 && z80->pc == address;
            if (!isExec && (desc->memoryType & (BRK_MEM_READ | BRK_MEM_WRITE)))
            {
                // Combined R/W watchpoints cannot tell direction from the payload;
                // report WRITE only when the watchpoint is write-only.
                bool writeOnly = (desc->memoryType & BRK_MEM_WRITE) && !(desc->memoryType & BRK_MEM_READ);
                reason = writeOnly ? dzrp::BreakReason::WATCHPOINT_WRITE : dzrp::BreakReason::WATCHPOINT_READ;
            }
        }
    }

    // DeZog expects the stop address to be PC for breakpoints
    const Z80State* z80 = emulator->GetZ80State();
    uint16_t stopAddr = (reason == dzrp::BreakReason::BREAKPOINT && z80) ? z80->pc : address;

    notify(reason, stopAddr);
}

void DezogDebugAdapter::notify(dzrp::BreakReason reason, uint16_t addr)
{
    PauseNotifier notifier;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        notifier = _notifier;
    }

    if (notifier)
        notifier(reason, addr, 0);
}

/// endregion </Notifications>

/// region <Execution control>

void DezogDebugAdapter::pause()
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    if (!emulator->IsPaused())
    {
        // Pause() blocks until the Z80 thread has parked (WaitForPauseConfirmation)
        emulator->Pause();
    }

    leaveHistory(*emulator);

    const Z80State* z80 = emulator->GetZ80State();
    notify(dzrp::BreakReason::MANUAL, z80 ? z80->pc : 0);
}

void DezogDebugAdapter::resume()
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    if (!_temporaryBreakpoints.empty())
        ensureDebugEnabled(*emulator);

    leaveHistory(*emulator);
    ensureHistoryRecording(*emulator);

    emulator->Resume();
}

bool DezogDebugAdapter::isPaused() const
{
    auto emulator = resolveEmulator();
    return emulator ? emulator->IsPaused() : true;
}

/// endregion </Execution control>

/// region <Registers>

DezogDebugAdapter::Registers DezogDebugAdapter::getRegisters() const
{
    Registers regs{};

    auto emulator = resolveEmulator();
    if (!emulator)
        return regs;

    const Z80State* z80 = emulator->GetZ80State();
    if (!z80)
        return regs;

    regs.pc = z80->pc;
    regs.sp = z80->sp;
    regs.af = z80->af;
    regs.bc = z80->bc;
    regs.de = z80->de;
    regs.hl = z80->hl;
    regs.ix = z80->ix;
    regs.iy = z80->iy;
    regs.af2 = z80->alt.af;
    regs.bc2 = z80->alt.bc;
    regs.de2 = z80->alt.de;
    regs.hl2 = z80->alt.hl;
    regs.r = z80->r_low;
    regs.i = z80->i;
    regs.im = z80->im;

    return regs;
}

void DezogDebugAdapter::setRegister(dzrp::RegisterId regId, uint16_t value)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    leaveHistory(*emulator);

    Z80State* z80 = emulator->GetZ80State();
    if (!z80)
        return;

    const uint8_t lo = static_cast<uint8_t>(value & 0xFF);

    using R = dzrp::RegisterId;
    switch (regId)
    {
        case R::PC: z80->pc = value; break;
        case R::SP: z80->sp = value; break;
        case R::AF: z80->af = value; break;
        case R::BC: z80->bc = value; break;
        case R::DE: z80->de = value; break;
        case R::HL: z80->hl = value; break;
        case R::IX: z80->ix = value; break;
        case R::IY: z80->iy = value; break;
        case R::AF2: z80->alt.af = value; break;
        case R::BC2: z80->alt.bc = value; break;
        case R::DE2: z80->alt.de = value; break;
        case R::HL2: z80->alt.hl = value; break;
        case R::IM: z80->im = static_cast<uint8_t>(lo > 2 ? 2 : lo); break;

        case R::F: z80->f = lo; break;
        case R::A: z80->a = lo; break;
        case R::C: z80->c = lo; break;
        case R::B: z80->b = lo; break;
        case R::E: z80->e = lo; break;
        case R::D: z80->d = lo; break;
        case R::L: z80->l = lo; break;
        case R::H: z80->h = lo; break;
        case R::IXL: z80->xl = lo; break;
        case R::IXH: z80->xh = lo; break;
        case R::IYL: z80->yl = lo; break;
        case R::IYH: z80->yh = lo; break;

        case R::F2: z80->alt.f = lo; break;
        case R::A2: z80->alt.a = lo; break;
        case R::C2: z80->alt.c = lo; break;
        case R::B2: z80->alt.b = lo; break;
        case R::E2: z80->alt.e = lo; break;
        case R::D2: z80->alt.d = lo; break;
        case R::L2: z80->alt.l = lo; break;
        case R::H2: z80->alt.h = lo; break;

        case R::R: z80->r_low = lo; break;
        case R::I: z80->i = lo; break;

        default:
            std::cerr << "[DZRP] setRegister: unsupported register id " << static_cast<int>(regId) << "\n";
            break;
    }

    onDebuggerEdit(*emulator, "dezog set register");
}

/// endregion </Registers>

/// region <Memory>

std::vector<uint8_t> DezogDebugAdapter::readMemory(uint16_t addr, uint16_t len) const
{
    std::vector<uint8_t> result(len, 0);

    auto emulator = resolveEmulator();
    if (!emulator)
        return result;

    Memory* memory = emulator->GetMemory();
    if (!memory)
        return result;

    for (uint32_t i = 0; i < len; ++i)
    {
        result[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>((addr + i) & 0xFFFF));
    }

    return result;
}

void DezogDebugAdapter::writeMemory(uint16_t addr, const std::vector<uint8_t>& data)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    leaveHistory(*emulator);

    Memory* memory = emulator->GetMemory();
    if (!memory)
        return;

    for (size_t i = 0; i < data.size(); ++i)
    {
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>((addr + i) & 0xFFFF), data[i]);
    }

    if (!data.empty())
        onDebuggerEdit(*emulator, "dezog write memory");
}

/// endregion </Memory>

/// region <ZRCP capabilities>

bool DezogDebugAdapter::stepOnce()
{
    auto emulator = resolveEmulator();
    if (!emulator || !emulator->IsPaused())
        return false;

    leaveHistory(*emulator);
    ensureHistoryRecording(*emulator);

    // Single instruction from the control thread - the same call the CLI 'stepin' uses
    emulator->RunSingleCPUCycle(false);

    return true;
}

std::string DezogDebugAdapter::disassembleInstruction(uint16_t addr, uint8_t* lenOut)
{
    auto emulator = resolveEmulator();
    if (!emulator)
    {
        if (lenOut)
            *lenOut = 1;
        return {};
    }

    ensureDebugEnabled(*emulator);

    DebugManager* debugManager = emulator->GetDebugManager();
    if (!debugManager || !debugManager->GetDisassembler())
    {
        if (lenOut)
            *lenOut = 1;
        return {};
    }

    Z80Disassembler* disassembler = debugManager->GetDisassembler().get();

    std::vector<uint8_t> buffer = readMemory(addr, static_cast<uint16_t>(Z80Disassembler::MAX_INSTRUCTION_LENGTH));

    uint8_t len = 0;
    DecodedInstruction decoded;
    std::string text = disassembler->disassembleSingleCommand(buffer, addr, &len, &decoded);

    // Safe default when the decoder could not determine the length
    if (len == 0)
        len = 1;
    if (lenOut)
        *lenOut = len;
    return text;
}

/// endregion </ZRCP capabilities>

/// region <Banking>

std::vector<uint8_t> DezogDebugAdapter::getSlots() const
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return {};

    Memory* memory = emulator->GetMemory();
    if (!memory)
        return {};

    if (getMachineType() == dzrp::MachineType::ZX48K)
    {
        // DeZog MemoryModelZx48k: slot 0 = ROM (bank 0), slot 1 = RAM 0x4000-0xFFFF (bank 1)
        return {0, 1};
    }

    std::vector<uint8_t> slots(4);
    slots[0] = static_cast<uint8_t>(ROM_BANK_BASE + (memory->GetROMPage() & 0x01));
    slots[1] = static_cast<uint8_t>(memory->GetRAMPageForBank1());
    slots[2] = static_cast<uint8_t>(memory->GetRAMPageForBank2());
    slots[3] = static_cast<uint8_t>(memory->GetRAMPageForBank3());
    return slots;
}

void DezogDebugAdapter::setSlot(uint8_t slot, uint8_t bank)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    leaveHistory(*emulator);

    Memory* memory = emulator->GetMemory();
    if (!memory)
        return;

    if (slot == 0)
    {
        uint16_t romPage;
        if (bank == ROM0_ALIAS)
            romPage = 0;
        else if (bank == ROM1_ALIAS)
            romPage = 1;
        else if (bank >= ROM_BANK_BASE)
            romPage = static_cast<uint16_t>(bank - ROM_BANK_BASE);
        else
            romPage = bank;

        if (romPage < MAX_ROM_PAGES)
        {
            memory->SetROMPage(romPage, true);
            onDebuggerEdit(*emulator, "dezog set rom slot");
        }
        return;
    }

    // bank is uint8_t, so every value is < MAX_RAM_PAGES (256); no check needed.

    switch (slot)
    {
        case 1: memory->SetRAMPageToBank1(bank); break;
        case 2: memory->SetRAMPageToBank2(bank); break;
        case 3: memory->SetRAMPageToBank3(bank, true); break;
        default: return;
    }

    onDebuggerEdit(*emulator, "dezog set slot");
}

void DezogDebugAdapter::writeBank(uint8_t bank, const std::vector<uint8_t>& data)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    leaveHistory(*emulator);

    // Note: bank is uint8_t (< MAX_RAM_PAGES == 256); Memory::RAMPageAddress
    // bounds-checks internally and returns nullptr for out-of-range pages.
    Memory* memory = emulator->GetMemory();
    if (!memory)
        return;

    uint8_t* page = memory->RAMPageAddress(bank);
    if (!page)
        return;

    size_t len = data.size() < PAGE_SIZE ? data.size() : PAGE_SIZE;
    std::memcpy(page, data.data(), len);

    if (len > 0)
        onDebuggerEdit(*emulator, "dezog write bank");
}

/// endregion </Banking>

/// region <Breakpoints>

uint16_t DezogDebugAdapter::addBreakpoint(uint16_t addr, uint8_t bank, const std::string& /*condition*/,
                                          bool temporary)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return 0;

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
        return 0;

    ensureDebugEnabled(*emulator);

    uint16_t id;
    if (bank == 0)
    {
        id = bpManager->AddExecutionBreakpoint(addr, BREAKPOINT_OWNER);
    }
    else
    {
        // Wire format is bank+1
        uint8_t dzrpBank = static_cast<uint8_t>(bank - 1);
        if (dzrpBank >= ROM_BANK_BASE)
            id = bpManager->AddExecutionBreakpointInPage(addr, static_cast<uint8_t>(dzrpBank - ROM_BANK_BASE),
                                                         BANK_ROM, BREAKPOINT_OWNER);
        else
            id = bpManager->AddExecutionBreakpointInPage(addr, dzrpBank, BANK_RAM, BREAKPOINT_OWNER);
    }

    if (id == BRK_INVALID)
        return 0;

    if (temporary)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _temporaryBreakpoints.insert(id);
    }

    return id;
}

void DezogDebugAdapter::removeBreakpoint(uint16_t id)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _temporaryBreakpoints.erase(id);
    }

    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    if (BreakpointManager* bpManager = emulator->GetBreakpointManager())
        bpManager->RemoveBreakpointByID(id);
}

void DezogDebugAdapter::clearTemporaryBreakpoints()
{
    std::set<uint16_t> ids;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ids.swap(_temporaryBreakpoints);
    }

    if (ids.empty())
        return;

    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
        return;

    for (uint16_t id : ids)
        bpManager->RemoveBreakpointByID(id);
}

size_t DezogDebugAdapter::getTemporaryBreakpointCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _temporaryBreakpoints.size();
}

bool DezogDebugAdapter::isBreakpointTemporary(uint16_t id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _temporaryBreakpoints.count(id) != 0;
}

/// endregion </Breakpoints>

/// region <Watchpoints>

bool DezogDebugAdapter::addWatchpoint(uint16_t addr, uint8_t bank, uint16_t size, dzrp::WatchAccess access)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return false;

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
        return false;

    uint8_t memoryType = memoryTypeFromAccess(access);
    if (memoryType == BRK_MEM_NONE || size == 0)
        return false;

    WatchKey key{addr, bank, size, static_cast<uint8_t>(access)};
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_watchpoints.count(key))
            return true;  // Idempotent
    }

    ensureDebugEnabled(*emulator);

    std::vector<uint16_t> ids;
    ids.reserve(size);

    uint32_t end = static_cast<uint32_t>(addr) + size;
    if (end > 0x10000)
        end = 0x10000;

    for (uint32_t a = addr; a < end; ++a)
    {
        uint16_t id;
        if (bank == 0)
        {
            id = bpManager->AddCombinedMemoryBreakpoint(static_cast<uint16_t>(a), memoryType, BREAKPOINT_OWNER);
        }
        else
        {
            uint8_t dzrpBank = static_cast<uint8_t>(bank - 1);
            if (dzrpBank >= ROM_BANK_BASE)
                id = bpManager->AddCombinedMemoryBreakpointInPage(static_cast<uint16_t>(a), memoryType,
                                                                  static_cast<uint8_t>(dzrpBank - ROM_BANK_BASE),
                                                                  BANK_ROM, BREAKPOINT_OWNER);
            else
                id = bpManager->AddCombinedMemoryBreakpointInPage(static_cast<uint16_t>(a), memoryType, dzrpBank,
                                                                  BANK_RAM, BREAKPOINT_OWNER);
        }

        if (id != BRK_INVALID)
            ids.push_back(id);
    }

    if (ids.empty())
        return false;

    std::lock_guard<std::mutex> lock(_mutex);
    _watchpoints[key] = std::move(ids);
    return true;
}

void DezogDebugAdapter::removeWatchpoint(uint16_t addr, uint8_t bank, uint16_t size, dzrp::WatchAccess access)
{
    WatchKey key{addr, bank, size, static_cast<uint8_t>(access)};

    std::vector<uint16_t> ids;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _watchpoints.find(key);
        if (it == _watchpoints.end())
            return;
        ids = std::move(it->second);
        _watchpoints.erase(it);
    }

    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
        return;

    for (uint16_t id : ids)
        bpManager->RemoveBreakpointByID(id);
}

size_t DezogDebugAdapter::getWatchpointCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _watchpoints.size();
}

/// endregion </Watchpoints>

/// region <State>

std::vector<uint8_t> DezogDebugAdapter::captureSnapshotBytes(Emulator& emulator) const
{
    std::filesystem::path path = makeSnapshotTempPath();
    std::vector<uint8_t> bytes;

    if (emulator.SaveSnapshot(path.string()))
    {
        std::ifstream in(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    else
    {
        std::cerr << "[DZRP] captureSnapshotBytes: SaveSnapshot failed\n";
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return bytes;
}

bool DezogDebugAdapter::restoreSnapshotBytes(Emulator& emulator, const std::vector<uint8_t>& bytes) const
{
    if (bytes.empty())
        return false;

    std::filesystem::path path = makeSnapshotTempPath();
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    bool ok = emulator.LoadSnapshot(path.string());
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return ok;
}

std::vector<uint8_t> DezogDebugAdapter::captureState() const
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return {};

    const_cast<DezogDebugAdapter*>(this)->leaveHistory(*emulator);

    return captureSnapshotBytes(*emulator);
}

void DezogDebugAdapter::restoreState(const std::vector<uint8_t>& state)
{
    auto emulator = resolveEmulator();
    if (!emulator || state.empty())
        return;

    leaveHistory(*emulator);

    if (!restoreSnapshotBytes(*emulator, state))
        std::cerr << "[DZRP] restoreState: LoadSnapshot failed\n";

    // LoadSnapshot invalidates the TTD session - start a fresh history from here
    ensureHistoryRecording(*emulator);
}

/// endregion </State>

/// region <Machine>

dzrp::MachineType DezogDebugAdapter::getMachineType() const
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return dzrp::MachineType::UNKNOWN;

    EmulatorContext* context = emulator->GetContext();
    if (!context)
        return dzrp::MachineType::UNKNOWN;

    switch (context->config.mem_model)
    {
        case MM_SPECTRUM48:
            return dzrp::MachineType::ZX48K;
        default:
            // Pentagon / 128K / +3 / Scorpion / ATM / ... all expose a 128K-style 4-slot map
            return dzrp::MachineType::ZX128K;
    }
}

void DezogDebugAdapter::setBorder(uint8_t color)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    EmulatorContext* context = emulator->GetContext();
    if (context && context->pScreen)
        context->pScreen->SetBorderColor(static_cast<uint8_t>(color & 0x07));
}

uint8_t DezogDebugAdapter::readPort(uint16_t port) const
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return 0;

    MainLoop* mainLoop = emulator->GetMainLoop();
    if (!mainLoop || !mainLoop->GetCPU())
        return 0;

    Ports* ports = mainLoop->GetCPU()->GetPorts();
    return ports ? ports->In(port) : 0;
}

void DezogDebugAdapter::writePort(uint16_t port, uint8_t value)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    MainLoop* mainLoop = emulator->GetMainLoop();
    if (!mainLoop || !mainLoop->GetCPU())
        return;

    if (Ports* ports = mainLoop->GetCPU()->GetPorts())
        ports->Out(port, value);
}

bool DezogDebugAdapter::waitForTarget(uint32_t timeoutMs) const
{
    // DeZog may connect while the host UI (unreal-qt) is still creating its
    // emulator. Poll briefly so CMD_INIT can report a real machine type
    // instead of UNKNOWN(0), which DeZog rejects ("Unknown machine type 0").
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (!resolveEmulator())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return true;
}

/// endregion </Machine>

namespace
{
ttd::TimeTravelManager* ttdManagerOf(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context ? context->pTimeTravelManager : nullptr;
}
}  // namespace

/// region <Session lifecycle>

void DezogDebugAdapter::onSessionOpened()
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    // Attaching a debugger stops the target: DeZog (with startAutomatically=false)
    // assumes the machine is already paused after CMD_INIT and sends a
    // "stop on start" without pausing it itself, so the registers/call-stack
    // panels only populate if we hand it a stopped CPU. Pause with broadcast=true
    // so the host UI (e.g. unreal-qt) reflects the paused state and does not
    // immediately resume it.
    if (!emulator->IsPaused())
        emulator->Pause(true);

    ensureHistoryRecording(*emulator);
}

void DezogDebugAdapter::onSessionClosed()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _temporaryBreakpoints.clear();
        _watchpoints.clear();
    }

    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    leaveHistory(*emulator);

    // End the live-history mode: recording stops, the timeline is kept for
    // the scrubber/.ttd flows (§6.3.1).
    if (ttd::TimeTravelManager* mgr = ttdManagerOf(*emulator))
        mgr->EndDebuggerLiveHistory();

    if (BreakpointManager* bpManager = emulator->GetBreakpointManager())
    {
        // Collect first: removing while iterating the manager's map is unsafe
        std::vector<uint16_t> owned;
        for (const auto& [id, desc] : bpManager->GetAllBreakpoints())
        {
            if (desc && desc->owner == BREAKPOINT_OWNER)
                owned.push_back(id);
        }
        for (uint16_t id : owned)
            bpManager->RemoveBreakpointByID(id);

        if (!owned.empty())
            std::cout << "[DZRP] Session closed: removed " << owned.size() << " dezog breakpoint(s)\n";
    }

    if (emulator->IsPaused())
        emulator->Resume();
}

/// endregion </Session lifecycle>

/// region <Instruction history (TTD)>

void DezogDebugAdapter::setHistoryEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _historyEnabled = enabled;
}

bool DezogDebugAdapter::isHistoryEnabled() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _historyEnabled;
}

int64_t DezogDebugAdapter::getHistoryCursor() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _historyCursor;
}

bool DezogDebugAdapter::isHistoryAvailable() const
{
    if (!isHistoryEnabled())
        return false;
    auto emulator = resolveEmulator();
    return emulator && ttdManagerOf(*emulator) != nullptr;
}

bool DezogDebugAdapter::isHistoryRecording() const
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return false;
    ttd::TimeTravelManager* mgr = ttdManagerOf(*emulator);
    return mgr && mgr->IsRecording();
}

void DezogDebugAdapter::ensureHistoryRecording(Emulator& emulator)
{
    if (!isHistoryEnabled())
        return;

    ttd::TimeTravelManager* mgr = ttdManagerOf(emulator);
    if (!mgr)
        return;

    // Debug sessions record via the TTD DebuggerLive mode: continuous
    // capture that survives browse cycles. A plain StartRecording here
    // would wipe the timeline on every browse exit (reverse-debugging.md
    // §6.2). Idempotent - safe to call while browsing.
    if (!mgr->BeginDebuggerLiveHistory())
        std::cerr << "[DZRP] TTD BeginDebuggerLiveHistory failed - instruction history unavailable\n";
}

void DezogDebugAdapter::onDebuggerEdit(Emulator& emulator, const char* what)
{
    if (!isHistoryEnabled())
        return;

    ttd::TimeTravelManager* mgr = ttdManagerOf(emulator);
    if (!mgr || !mgr->IsRecording())
        return;

    // Marker for tooling/consistency, then a fresh baseline so replay starts
    // from the edited state (a mid-frame checkpoint, exactly like
    // StartRecording on connect). History before the edit is dropped - same
    // segment semantics as any other nondeterministic barrier.
    //
    // NOTE (§6.4): this applies in DebuggerLive too. A marker-only handling
    // was tried and is UNSOUND: replay re-executes from the last checkpoint,
    // so after an out-of-band edit (registers/PC/memory) everything between
    // that checkpoint and the marker decodes as fabricated execution that
    // never ran. Restarting at the edited state is the only correct Phase-1
    // semantics; journaling debugger writes (§6.5 Phase 3) will remove the
    // wipe. DeZog breakpoints are native objects, NOT memory edits - they
    // never trigger this.
    mgr->RecordExternalEvent(ttd::TTDExternalEventKind::DebuggerEdit, what);
    mgr->StopRecording();
    if (!mgr->StartRecording())
        std::cerr << "[DZRP] TTD restart after debugger edit failed\n";
}

void DezogDebugAdapter::leaveHistoryIfBrowsing()
{
    if (getHistoryCursor() < 0)
        return;
    auto emulator = resolveEmulator();
    if (emulator)
        leaveHistory(*emulator);
}

void DezogDebugAdapter::leaveHistory(Emulator& emulator)
{
    uint64_t presentFrame = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_historyCursor < 0)
            return;
        presentFrame = _present.first;
    }

    ttd::TimeTravelManager* mgr = ttdManagerOf(emulator);

    // Browsing is read-only under DebuggerLive: the machine never moved and
    // recording never stopped, so there is nothing to restore or restart -
    // only a stale same-frame partial build must go, because a re-pause in
    // this frame before the next boundary must not observe it (the engine
    // clears the cache on the boundary itself).
    //
    // If another surface took over the machine mid-browse (Detached scrub /
    // reset), drop only our bookkeeping and leave recording alone - that
    // surface owns the machine now.
    if (mgr && mgr->GetState() != ttd::TTDSessionState::Detached &&
        mgr->CurrentPosition().frame >= presentFrame)
    {
        mgr->ClearFrameCache();
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _historyCursor = -1;
        _historySegs.clear();
    }
}

// Map a global DeZog history index → (frame, entry-in-frame). Extends
// _historySegs backward from the present one frame at a time, memoizing each
// frame's visible-entry count so a frame is only decoded once for counting.
// Control-thread only (browsing happens while paused).
bool DezogDebugAdapter::resolveHistoryIndex(Emulator& emulator, uint32_t index, uint64_t& frameOut,
                                            uint32_t& entryIdxOut)
{
    ttd::TimeTravelManager* mgr = ttdManagerOf(emulator);
    if (!mgr)
        return false;

    const uint64_t presentFrame = _present.first;
    const uint32_t presentTInFrame = _present.second;
    // Frames below the recording baseline have no checkpoints (every
    // seek/build for them fails), so the walk can stop there instead of
    // probing each empty frame down to 0.
    const uint64_t earliestFrame = mgr->GetEarliestRecordedFrame();

    // Extend segments until `index` is covered or history is exhausted.
    for (;;)
    {
        if (!_historySegs.empty())
        {
            const HistoryFrameSeg& last = _historySegs.back();
            if (index < last.cumBefore + last.count)
                break;  // covered by an existing segment
            if (last.frame == 0 || last.frame <= earliestFrame)
                return false;  // no earlier frame — index is past the oldest instruction
        }

        uint64_t nextFrame;
        uint64_t cumBefore;
        bool isPresent;
        if (_historySegs.empty())
        {
            nextFrame = presentFrame;
            cumBefore = 0;
            isPresent = true;
        }
        else
        {
            nextFrame = _historySegs.back().frame - 1;
            cumBefore = _historySegs.back().cumBefore + _historySegs.back().count;
            isPresent = false;
        }

        const ttd::TTDFrameCache* c = mgr->GetFrameCache(nextFrame);
        uint32_t count = 0;
        if (c)
        {
            if (isPresent)
            {
                // Present frame is partial: only instructions executed *before*
                // the stop are history. Entries ascend by tInFrame, so count the
                // leading run with tInFrame < presentTInFrame.
                for (const auto& e : c->entries)
                {
                    if (e.tInFrame < presentTInFrame)
                        ++count;
                    else
                        break;
                }
            }
            else
            {
                count = static_cast<uint32_t>(c->entries.size());
            }
        }
        _historySegs.push_back({nextFrame, count, cumBefore, isPresent});
    }

    for (const HistoryFrameSeg& seg : _historySegs)
    {
        if (seg.count != 0 && index >= seg.cumBefore && index < seg.cumBefore + seg.count)
        {
            const uint32_t localOffset = static_cast<uint32_t>(index - seg.cumBefore);
            // Within a frame, entries ascend by tInFrame while the DeZog index
            // counts *backward*, so the most recent visible entry (largest
            // tInFrame, which is index seg.count-1 of the visible run) is offset 0.
            entryIdxOut = seg.count - 1 - localOffset;
            frameOut = seg.frame;
            return true;
        }
    }
    return false;
}

std::optional<dzrp::IDebugInterface::HistoryEntry> DezogDebugAdapter::getHistoryEntry(uint32_t index)
{
    // STRATEGY: serve each entry from the TTD per-frame decode cache
    // (TimeTravelManager::GetFrameCache) — O(1) after a one-time ~ms frame fill.
    // See docs/inprogress/2026-08-27-dezog-integration/reverse-debugging.md §5
    // ("Best strategy for DeZog"). To revert to seek-per-read, replace the
    // resolveHistoryIndex + GetFrameCache block with a SeekTo per index (doc §3).
    if (!isHistoryEnabled())
        return std::nullopt;

    auto emulator = resolveEmulator();
    if (!emulator || !emulator->IsPaused())
        return std::nullopt;

    ttd::TimeTravelManager* mgr = ttdManagerOf(*emulator);
    if (!mgr || mgr->GetCheckpointCount() == 0)
        return std::nullopt;

    // Entering history: remember the present TimePoint (partial-frame
    // counting in resolveHistoryIndex). Browse is read-only under the
    // DebuggerLive mode - recording never stops; GetFrameCache builds under
    // the paused exemption. No snapshot, no restore (§6.3.2).
    if (getHistoryCursor() < 0)
    {
        if (!mgr->IsDebuggerLive())
            return std::nullopt;  // history requires the live-history mode
        ttd::TTDTimePoint present = mgr->CurrentPosition();
        std::lock_guard<std::mutex> lock(_mutex);
        _present = {present.frame, present.tInFrame};
        _historySegs.clear();
        _historyCursor = 0;  // provisional; finalized below (marks "browsing")
    }

    uint64_t frame = 0;
    uint32_t entryIdx = 0;
    if (!resolveHistoryIndex(*emulator, index, frame, entryIdx))
    {
        // Past the oldest recorded instruction. NON-destructive: under the
        // cache strategy nothing was moved, so the browsable history and the
        // memoized segments stay valid - a follow-up in-range query must
        // still succeed. The browse ends on the next forward command via
        // leaveHistory.
        return std::nullopt;
    }

    const ttd::TTDFrameCache* c = mgr->GetFrameCache(frame);
    if (!c || entryIdx >= c->entries.size())
    {
        // Segment/cache disagreement — treat like out-of-range (non-destructive).
        return std::nullopt;
    }
    const ttd::TTDFrameCacheEntry& e = c->entries[entryIdx];

    HistoryEntry entry;
    entry.regs.pc = e.pc;   entry.regs.sp = e.sp;
    entry.regs.af = e.af;   entry.regs.bc = e.bc;   entry.regs.de = e.de;   entry.regs.hl = e.hl;
    entry.regs.ix = e.ix;   entry.regs.iy = e.iy;
    entry.regs.af2 = e.af2; entry.regs.bc2 = e.bc2; entry.regs.de2 = e.de2; entry.regs.hl2 = e.hl2;
    entry.regs.r = e.r;     entry.regs.i = e.i;     entry.regs.im = e.im;
    // The TTD cache stores raw slots (slot 0 = ROM page 0/1); clients get the
    // same dzrp encoding getSlots() returns (ROM_BANK_BASE + page for slot 0),
    // so history entries decode identically to live register responses.
    if (getMachineType() == dzrp::MachineType::ZX48K)
        entry.slots = {0, 1};
    else
        entry.slots = {static_cast<uint8_t>(ROM_BANK_BASE + (e.slots[0] & 0x01)),
                       e.slots[1], e.slots[2], e.slots[3]};
    for (int i = 0; i < 4; ++i)
        entry.opcodes[i] = e.opcodes[i];
    entry.spContent = e.spContent;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _historyCursor = static_cast<int64_t>(index);
    }
    return entry;
}

/// endregion </Instruction history (TTD)>
