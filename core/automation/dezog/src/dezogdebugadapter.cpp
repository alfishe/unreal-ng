#include "dezogdebugadapter.h"

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "common/uuid.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/video/screen.h"

#include <atomic>
#include <cstdlib>
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

    if (bank >= MAX_RAM_PAGES)
        return;

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

    Memory* memory = emulator->GetMemory();
    if (!memory || bank >= MAX_RAM_PAGES)
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

/// endregion </Machine>

/// region <Session lifecycle>

void DezogDebugAdapter::onSessionOpened()
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

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

namespace
{
ttd::TimeTravelManager* ttdManagerOf(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context ? context->pTimeTravelManager : nullptr;
}
}  // namespace

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
    if (!mgr || mgr->IsRecording())
        return;

    // Never restart while browsing: StartRecording wipes the timeline
    if (getHistoryCursor() >= 0)
        return;

    if (!mgr->StartRecording())
        std::cerr << "[DZRP] TTD StartRecording failed - instruction history unavailable\n";
}

void DezogDebugAdapter::onDebuggerEdit(Emulator& emulator, const char* what)
{
    if (!isHistoryEnabled())
        return;

    ttd::TimeTravelManager* mgr = ttdManagerOf(emulator);
    if (!mgr || !mgr->IsRecording())
        return;

    // Marker for tooling/consistency, then a fresh baseline so replay starts
    // from the edited state (a mid-frame checkpoint, exactly like StartRecording
    // on connect). History before the edit is dropped - same segment semantics
    // as any other nondeterministic barrier.
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
    std::vector<uint8_t> snapshot;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_historyCursor < 0)
            return;
        snapshot = _presentSnapshot;
    }

    // Restore the exact present state snapshotted on entry, then start a fresh
    // recording from there. The pre-stop timeline is dropped: a mid-frame present
    // is always beyond the last frame-boundary checkpoint, so it cannot be a TTD
    // seek/ResumeRecordingFrom target. DeZog re-derives history going forward.
    if (!restoreSnapshotBytes(emulator, snapshot))
        std::cerr << "[DZRP] leaveHistory: failed to restore present snapshot\n";

    ttd::TimeTravelManager* mgr = ttdManagerOf(emulator);
    if (mgr)
    {
        if (mgr->IsRecording())
            mgr->StopRecording();
        if (isHistoryEnabled())
            mgr->StartRecording();
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _historyCursor = -1;
        _historyPositions.clear();
        _presentSnapshot.clear();
    }
}

std::optional<dzrp::IDebugInterface::HistoryEntry> DezogDebugAdapter::getHistoryEntry(uint32_t index)
{
    if (!isHistoryEnabled())
        return std::nullopt;

    auto emulator = resolveEmulator();
    if (!emulator || !emulator->IsPaused())
        return std::nullopt;

    ttd::TimeTravelManager* mgr = ttdManagerOf(*emulator);
    if (!mgr || mgr->GetCheckpointCount() == 0)
        return std::nullopt;

    // Entering history: snapshot the present state (for a robust return trip),
    // remember its TimePoint, and freeze the timeline.
    if (getHistoryCursor() < 0)
    {
        std::vector<uint8_t> snap = captureSnapshotBytes(*emulator);
        ttd::TTDTimePoint present = mgr->CurrentPosition();
        if (mgr->IsRecording())
            mgr->StopRecording();
        std::lock_guard<std::mutex> lock(_mutex);
        _present = {present.frame, present.tInFrame};
        _presentSnapshot = std::move(snap);
        _historyPositions.clear();
        _historyCursor = 0;  // provisional; finalized below (marks "browsing")
    }

    // Extend the position cache up to `index` by stepping strictly backward in
    // time, skipping the "absorb" steps that re-land on the current instruction.
    bool outOfRange = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        while (_historyPositions.size() <= index)
        {
            ttd::TTDTimePoint from;
            if (_historyPositions.empty())
                from = {_present.first, _present.second};
            else
                from = {_historyPositions.back().first, _historyPositions.back().second};

            // Start stepping from `from` (cache hits / forward moves may have
            // left the emulator elsewhere).
            mgr->SeekTo(from);

            ttd::TTDTimePoint landed = from;
            bool moved = false;
            for (int guard = 0; guard < 16; ++guard)  // absorb ≤ 2 in practice
            {
                if (!mgr->StepBackInstruction())
                    break;
                landed = mgr->CurrentPosition();
                if (landed < from)
                {
                    moved = true;
                    break;
                }
            }

            if (!moved)
            {
                outOfRange = true;
                break;
            }

            _historyPositions.emplace_back(landed.frame, landed.tInFrame);
        }
    }

    if (outOfRange)
    {
        // No earlier instruction — restore the present and resume recording.
        leaveHistory(*emulator);
        return std::nullopt;
    }

    // Materialize the requested index.
    ttd::TTDTimePoint target;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        target = {_historyPositions[index].first, _historyPositions[index].second};
        _historyCursor = static_cast<int64_t>(index);
    }
    mgr->SeekTo(target);

    HistoryEntry entry;
    entry.regs = getRegisters();
    entry.slots = getSlots();

    Memory* memory = emulator->GetMemory();
    if (memory)
    {
        for (int i = 0; i < 4; ++i)
            entry.opcodes[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(entry.regs.pc + i));
        uint16_t lo = memory->DirectReadFromZ80Memory(entry.regs.sp);
        uint16_t hi = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(entry.regs.sp + 1));
        entry.spContent = static_cast<uint16_t>(lo | (hi << 8));
    }

    return entry;
}

/// endregion </Instruction history (TTD)>
