#include "dezogdebugadapter.h"

#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"
#include "common/uuid.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/coordinator/machineop.h"
#include "debugger/ttd/ttdsession.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"
#include "debugger/ports/nextregwrite.h"
#include "debugger/ports/portwrite.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/video/screen.h"
#include "loaders/snapshot/snapshotcapture.h"

#include <algorithm>
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

/// The ROM page `base` points at (MEMORY_UNMAPPABLE: the model has no such ROM)
uint16_t RomPageOf(Memory& memory, uint8_t* base)
{
    return base ? memory.GetROMPageFromAddress(base) : MEMORY_UNMAPPABLE;
}
}  // namespace

/// region <DZRP bank numbering>

uint8_t DezogDebugAdapter::bankForRomPage(Memory& memory, uint16_t page)
{
    if (page == RomPageOf(memory, memory.base_128_rom))
        return ROM_BANK_BASE;
    if (page == RomPageOf(memory, memory.base_sos_rom))
        return ROM_BANK_BASE + 1;
    return static_cast<uint8_t>(OTHER_ROM_BANK_BASE + (page & 0x7F));
}

uint16_t DezogDebugAdapter::romPageForBank(Memory& memory, uint8_t bank)
{
    if (bank == ROM_BANK_BASE || bank == ROM0_ALIAS)
        return RomPageOf(memory, memory.base_128_rom);
    if (bank == ROM_BANK_BASE + 1 || bank == ROM1_ALIAS)
        return RomPageOf(memory, memory.base_sos_rom);
    if (bank >= OTHER_ROM_BANK_BASE && bank - OTHER_ROM_BANK_BASE < MAX_ROM_PAGES)
        return static_cast<uint16_t>(bank - OTHER_ROM_BANK_BASE);
    return MEMORY_UNMAPPABLE;
}

bool DezogDebugAdapter::physicalForBank(uint8_t bank, uint16_t addr, BreakpointSpec& spec) const
{
    auto emulator = resolveEmulator();
    Memory* memory = emulator ? emulator->GetMemory() : nullptr;
    if (!memory)
        return false;

    // A breakpoint on a bank fires only through the slot of its address, while that slot shows the bank
    spec.hasPage = true;
    spec.slotOnly = true;
    switch (getMachineType())
    {
        case dzrp::MachineType::ZXNEXT:
        {
            NextMemory* next = dynamic_cast<NextMemory*>(memory);
            if (!next)
                return false;
            // An 8K bank is half of a 16K page: the page and the offset in it
            const uint16_t offset = static_cast<uint16_t>(((bank & 1) << 13) | (addr & (NextMemory::kSlotSize - 1)));
            if (bank < NextMemory::kRamPages8K)
            {
                spec.pageType = BANK_RAM;
                spec.page = static_cast<uint8_t>(bank >> 1);
            }
            else if (bank >= NEXT_ROM_BANK_FIRST)
            {
                // DeZog's ROM banks (0xFC-0xFF, the lower / upper half): the ROM an MMU value of #FF shows now
                spec.pageType = BANK_ROM;
                spec.page = next->RomReadPage();
            }
            else
                return false;
            spec.pageOffset = offset;
            return true;
        }
        case dzrp::MachineType::ZX48K:
            // DeZog MemoryModelZx48k: bank 0 the ROM (0000-3FFF), bank 1 the RAM (4000-FFFF)
            if (bank == 0 && addr < 0x4000)
            {
                const uint16_t page = RomPageOf(*memory, memory->base_sos_rom);
                if (page == MEMORY_UNMAPPABLE)
                    return false;
                spec.pageType = BANK_ROM;
                spec.page = static_cast<uint8_t>(page);
                return true;
            }
            if (bank == 1 && addr >= 0x4000)
            {
                const uint16_t page = memory->GetRAMPageForBank(static_cast<uint8_t>(addr >> 14));
                if (page == MEMORY_UNMAPPABLE)
                    return false;
                spec.pageType = BANK_RAM;
                spec.page = static_cast<uint8_t>(page);
                return true;
            }
            return false;
        default:
        {
            const uint16_t romPage = romPageForBank(*memory, bank);
            if (romPage != MEMORY_UNMAPPABLE)
            {
                spec.pageType = BANK_ROM;
                spec.page = static_cast<uint8_t>(romPage);
                return true;
            }
            if (bank >= OTHER_ROM_BANK_BASE)
                return false;
            spec.pageType = BANK_RAM;
            spec.page = bank;
            return true;
        }
    }
}

/// endregion </DZRP bank numbering>

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

    // A breakpoint stop parks the machine until someone resumes it; posted after the pause, the event can arrive when
    // the stop is already over (a dropped session's cleanup resumed it) and must not reach the next session
    // (a machine not started at all only steps directly: its stops are always current)
    if (emulator->IsRunning() && !emulator->IsPaused())
        return;

    const uint16_t address = payload->address;

    // The access the breakpoint fired on tells execution from a read or a write watchpoint (an "rw" one included)
    dzrp::BreakReason reason = dzrp::BreakReason::BREAKPOINT;
    if (payload->kind == BreakpointHitKind::MemoryRead)
        reason = dzrp::BreakReason::WATCHPOINT_READ;
    else if (payload->kind == BreakpointHitKind::MemoryWrite)
        reason = dzrp::BreakReason::WATCHPOINT_WRITE;

    // A stop of a kind DeZog has no breakpoint for: as BREAKPOINT_HIT it finds none at the PC and runs on
    // (dzrpremote.ts checkPcBreakpoints), so it is another reason with the text DeZog shows
    const bool deZogKind = payload->kind == BreakpointHitKind::Execute || payload->kind == BreakpointHitKind::MemoryRead ||
                           payload->kind == BreakpointHitKind::MemoryWrite;
    if (!deZogKind || payload->cpu != "main")
    {
        char where[16];
        std::snprintf(where, sizeof(where), " #%04X", payload->address);
        std::string text = "Breakpoint " + std::to_string(payload->_payloadNumber) + ": " +
                           BreakpointHitKindName(payload->kind) + where;
        if (payload->cpu != "main")
            text += " on cpu " + payload->cpu;
        const Z80State* z80 = emulator->GetZ80State();
        notify(dzrp::BreakReason::OTHER, z80 ? z80->pc : payload->address, text);
        return;
    }

    // DeZog expects the stop address to be PC for breakpoints
    const Z80State* z80 = emulator->GetZ80State();
    const uint16_t stopAddr = (reason == dzrp::BreakReason::BREAKPOINT && z80) ? z80->pc : address;

    notify(reason, stopAddr);
}

std::string DezogDebugAdapter::stopText() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _stopText;
}

void DezogDebugAdapter::notify(dzrp::BreakReason reason, uint16_t addr, const std::string& text)
{
    PauseNotifier notifier;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        notifier = _notifier;
        _stopText = text;
    }

    if (notifier)
        notifier(reason, addr, wireBankAt(addr));
}

uint8_t DezogDebugAdapter::wireBankAt(uint16_t addr) const
{
    // NTF_PAUSE carries the bank of the stop address the way a long address does (dzrpbufferremote.ts: longAddr +=
    // data[5] << 16): bank + 1, 0 none. DeZog finds the breakpoint by that long address
    const std::vector<uint8_t> slots = getSlots();
    if (slots.empty())
        return 0;
    const unsigned slot = slots.size() == 8 ? (addr >> 13) : slots.size() == 4 ? (addr >> 14) : (addr < 0x4000 ? 0 : 1);
    uint8_t bank = slot < slots.size() ? slots[slot] : 0;
    // DeZog's Next model calls the ROM in slot 0 0xFE (dzrpremote.ts getRegistersFromEmulator)
    if (slots.size() == 8 && slot == 0 && bank == NextMemory::kMmuRom)
        bank = NEXT_ROM_LOWER;
    return static_cast<uint8_t>(bank + 1);
}

/// endregion </Notifications>

/// region <Execution control>

void DezogDebugAdapter::pause()
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    // The break reports the PC: read it parked. A pause behind a command (a direct run) applies when it ends, and a
    // pause somebody else asked for may not be confirmed yet
    if (!emulator->IsPaused())
        emulator->PauseAndWait(sessionRequester(), std::chrono::seconds(5), "dezog");
    emulator->WaitForPauseConfirmation();

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

    emulator->Resume(sessionRequester());
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
    regs.r = static_cast<uint8_t>((z80->r_low & 0x7F) | (z80->r_hi & 0x80));  // bit 7 is kept apart (LD R,A)
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

    editMachine(*emulator, "dezog set register", [&]() {
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

            case R::R: z80->r_low = lo; z80->r_hi = static_cast<uint8_t>(lo & 0x80); break;
            case R::I: z80->i = lo; break;

            default:
                std::cerr << "[DZRP] setRegister: unsupported register id " << static_cast<int>(regId) << "\n";
                break;
        }
    });
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

    if (data.empty())
        return;
    editMachine(*emulator, "dezog write memory", [&]() {
        for (size_t i = 0; i < data.size(); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>((addr + i) & 0xFFFF), data[i]);
    });
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

    // Single instruction from the control thread. It always executes (ZEsarUX cpu-step): an execution breakpoint
    // at PC is passed whether the machine stopped on it or stepped onto it, and also after a history read
    // (leaveHistory) ended the stop; breakpoints the instruction reaches still fire
    emulator->StepPastBreakpointAtPc();

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

    if (getMachineType() == dzrp::MachineType::ZXNEXT)
    {
        // DeZog MemoryModelZxNext: eight 8K slots, the bank of each is its MMU register (NR #50-#57; 0xFF the ROM)
        NextMemory* next = dynamic_cast<NextMemory*>(memory);
        if (!next)
            return {};
        std::vector<uint8_t> mmu(NextMemory::kSlots);
        for (unsigned s = 0; s < NextMemory::kSlots; s++)
            mmu[s] = next->GetMmu(s);
        return mmu;
    }

    if (getMachineType() == dzrp::MachineType::ZX48K)
    {
        // DeZog MemoryModelZx48k: slot 0 = ROM (bank 0), slot 1 = RAM 0x4000-0xFFFF (bank 1)
        return {0, 1};
    }

    std::vector<uint8_t> slots(4);
    // Slot 0: the ROM's DZRP bank (8 the 128 ROM, 9 the 48 BASIC, 0x80 + page another ROM), or a RAM page there
    slots[0] = memory->IsBank0ROM() ? bankForRomPage(*memory, memory->GetROMPage())
                                    : static_cast<uint8_t>(memory->GetRAMPageForBank0());
    slots[1] = static_cast<uint8_t>(memory->GetRAMPageForBank1());
    slots[2] = static_cast<uint8_t>(memory->GetRAMPageForBank2());
    slots[3] = static_cast<uint8_t>(memory->GetRAMPageForBank3());
    return slots;
}

uint8_t DezogDebugAdapter::nextRomSelect() const
{
    auto emulator = resolveEmulator();
    NextMemory* next = emulator ? dynamic_cast<NextMemory*>(emulator->GetMemory()) : nullptr;
    return next ? next->GetRomSelect() : 0;
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

    if (getMachineType() == dzrp::MachineType::ZXNEXT)
    {
        // The slot is an MMU register of the board (NR #50 + slot), the bank its value
        if (slot < NextMemory::kSlots)
            writeNextReg(static_cast<uint8_t>(NextBoard::kRegMmu0 + slot), bank);
        return;
    }

    if (slot == 0)
    {
        // The ROM banks getSlots reports (8, 9, 0x80 + page) and the 0xFE / 0xFF aliases; slot 0 takes no RAM bank
        const uint16_t romPage = romPageForBank(*memory, bank);
        if (romPage < MAX_ROM_PAGES)
            editMachine(*emulator, "dezog set rom slot", [&]() { memory->SetROMPage(romPage, true); });
        return;
    }

    // bank is uint8_t, so every value is < MAX_RAM_PAGES (256); no check needed.

    if (slot < 1 || slot > 3)
        return;
    editMachine(*emulator, "dezog set slot", [&]() {
        // Windows 1/2 follow no port latch: a debugger-forced mapping (checkpoints carry it)
        if (slot == 3)
            memory->SetRAMPageToBank3(bank, true);
        else
            memory->SetDebuggerRAMPageToBank(slot, bank);
    });
}

void DezogDebugAdapter::writeBank(uint8_t bank, const std::vector<uint8_t>& data)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;

    leaveHistory(*emulator);

    Memory* memory = emulator->GetMemory();
    if (!memory || data.empty())
        return;

    // DeZog's Next model: an 8K bank (loadBinSna writes a 16K bank as two); RAM only, as dezogif takes it
    if (NextMemory* next = dynamic_cast<NextMemory*>(memory))
    {
        if (bank >= NextMemory::kRamPages8K)
            return;
        const size_t len = std::min<size_t>(data.size(), NextMemory::kSlotSize);
        editMachine(*emulator, "dezog write bank", [&]() {
            for (size_t i = 0; i < len; ++i)
                if (uint8_t* byte = nextBankByte(*next, bank, static_cast<uint16_t>(i)))
                    *byte = data[i];
            next->MarkRamPageEdited(static_cast<uint16_t>(bank >> 1));
        });
        return;
    }

    // Note: bank is uint8_t (< MAX_RAM_PAGES == 256); Memory::RAMPageAddress
    // bounds-checks internally and returns nullptr for out-of-range pages.
    uint8_t* page = memory->RAMPageAddress(bank);
    if (!page)
        return;

    const size_t len = data.size() < PAGE_SIZE ? data.size() : PAGE_SIZE;
    editMachine(*emulator, "dezog write bank", [&]() { std::memcpy(page, data.data(), len); });
}

/// region <ZX Next>

uint8_t* DezogDebugAdapter::nextBankByte(NextMemory& memory, uint8_t bank, uint16_t offset)
{
    // The ROM banks (#FF, DeZog's 0xFC-0xFE): the 16K ROM area 0x0000-0x3FFF, the offset from its start, wrapping
    // inside it. While slot 0 / 1 has MMU #FF it is what the CPU reads there (the boot ROM, the DivMMC or Multiface
    // overlay, the alternate ROM, as NextMemory::MapSlot maps it); otherwise the ROM such a slot would read
    if (bank >= NEXT_ROM_BANK_FIRST)
    {
        constexpr uint32_t kRomSize = 0x4000;
        const unsigned slot = (offset & (kRomSize - 1)) >> 13;
        if (NextMemory::MmuIsRom(slot, memory.GetMmu(slot)))
            return memory.SlotReadHostAddress(slot, offset);
        uint8_t* rom = memory.ROMPageHostAddress(memory.RomReadPage());
        return rom ? rom + (offset & (kRomSize - 1)) : nullptr;
    }
    if (bank >= NextMemory::kRamPages8K)
        return nullptr;
    uint8_t* page = memory.RAMPageAddress(bank >> 1);
    return page ? page + (bank & 1) * NextMemory::kSlotSize + (offset & (NextMemory::kSlotSize - 1)) : nullptr;
}

std::vector<uint8_t> DezogDebugAdapter::readBankMemory(uint8_t bank, uint16_t offset, uint16_t size) const
{
    std::vector<uint8_t> result(size, 0);
    auto emulator = resolveEmulator();
    NextMemory* memory = emulator ? dynamic_cast<NextMemory*>(emulator->GetMemory()) : nullptr;
    if (!memory)
        return result;
    for (uint32_t i = 0; i < size; ++i)
        if (const uint8_t* byte = nextBankByte(*memory, bank, static_cast<uint16_t>(offset + i)))
            result[i] = *byte;
    return result;
}

void DezogDebugAdapter::writeBankMemory(uint8_t bank, uint16_t offset, const std::vector<uint8_t>& data)
{
    auto emulator = resolveEmulator();
    NextMemory* memory = emulator ? dynamic_cast<NextMemory*>(emulator->GetMemory()) : nullptr;
    if (!memory || data.empty())
        return;
    leaveHistory(*emulator);
    editMachine(*emulator, "dezog write bank memory", [&]() {
        for (size_t i = 0; i < data.size(); ++i)
            if (uint8_t* byte = nextBankByte(*memory, bank, static_cast<uint16_t>(offset + i)))
                *byte = data[i];
    });
}

namespace
{
PortDecoder_Next* NextPorts(const std::shared_ptr<Emulator>& emulator)
{
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    return context ? dynamic_cast<PortDecoder_Next*>(context->pPortDecoder) : nullptr;
}
}  // namespace

std::vector<uint8_t> DezogDebugAdapter::getSpritesPalette(uint8_t which) const
{
    // NR #43 bits 6:4 select the palette: 0 ULA, 1 Layer 2, 2 sprites, 3 tilemap; bit 6 the second one of each (sprites: 2 and 6)
    constexpr unsigned kSpritePalette1 = 2, kSecondPaletteBit = 4;
    constexpr unsigned kEntries = 256;
    constexpr uint16_t kLowBlueBit = 0x001, kPriorityBit = 0x200;
    constexpr uint8_t kPriorityFlag = 0x80;  // as NR #44 reads it back
    PortDecoder_Next* ports = NextPorts(resolveEmulator());
    if (!ports)
        return {};
    std::vector<uint8_t> out;
    out.reserve(kEntries * 2);
    for (unsigned i = 0; i < kEntries; i++)
    {
        const uint16_t entry = ports->Board().Video().PaletteEntry(kSpritePalette1 + ((which & 1) ? kSecondPaletteBit : 0), i);
        out.push_back(static_cast<uint8_t>(entry >> 1));
        out.push_back(static_cast<uint8_t>((entry & kLowBlueBit) | ((entry & kPriorityBit) ? kPriorityFlag : 0)));
    }
    return out;
}

std::vector<uint8_t> DezogDebugAdapter::getSprites(uint8_t index, uint8_t count) const
{
    constexpr unsigned kAttributeBytes = 5;
    PortDecoder_Next* ports = NextPorts(resolveEmulator());
    if (!ports)
        return {};
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(count) * kAttributeBytes);
    for (unsigned i = 0; i < count; i++)
        for (unsigned b = 0; b < kAttributeBytes; b++)
            out.push_back(ports->Board().Sprites().Attribute(index + i, b));  // the sprite number wraps at 128
    return out;
}

std::vector<uint8_t> DezogDebugAdapter::getSpritePatterns(uint8_t index, uint8_t count) const
{
    constexpr unsigned kPatternBytes = 256;
    PortDecoder_Next* ports = NextPorts(resolveEmulator());
    if (!ports)
        return {};
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(count) * kPatternBytes);
    for (unsigned i = 0; i < static_cast<unsigned>(count) * kPatternBytes; i++)
        out.push_back(ports->Board().Sprites().PatternByte(index * kPatternBytes + i));  // the 16K pattern RAM wraps
    return out;
}

std::vector<uint8_t> DezogDebugAdapter::getSpritesClipWindowAndControl() const
{
    constexpr unsigned kSpriteWindow = 1;  // Layer 2, sprites, ULA/LoRes, tilemap
    constexpr unsigned kCoordinates = 4;   // x left, x right, y top, y bottom
    constexpr uint8_t kRegSpriteControl = 0x15;
    PortDecoder_Next* ports = NextPorts(resolveEmulator());
    if (!ports)
        return {};
    std::vector<uint8_t> out;
    for (unsigned c = 0; c < kCoordinates; c++)
        out.push_back(ports->Board().Video().Clip(kSpriteWindow, c));
    out.push_back(ports->Board().Stored(kRegSpriteControl));
    return out;
}

uint8_t DezogDebugAdapter::readNextReg(uint8_t reg) const
{
    auto emulator = resolveEmulator();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    PortDecoder_Next* decoder = context ? dynamic_cast<PortDecoder_Next*>(context->pPortDecoder) : nullptr;
    return decoder ? decoder->Board().Read(reg) : 0;  // the board's own read: no breakpoint, no journal
}

void DezogDebugAdapter::writeNextReg(uint8_t reg, uint8_t value)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;
    leaveHistory(*emulator);
    // The NEXTREG instruction's door, as a tool edit: the journal sees it, no breakpoint fires
    NextRegWriteControl::Write(emulator.get(), reg, value, NextRegWriteControl::Door::NextReg, "dezog");
}

/// endregion </ZX Next>

/// endregion </Banking>

/// region <Breakpoints>

uint16_t DezogDebugAdapter::addBreakpoint(uint16_t addr, uint8_t bank, const std::string& condition,
                                          bool temporary)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return 0;

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
        return 0;

    ensureDebugEnabled(*emulator);

    BreakpointSpec spec;
    spec.address = addr;
    spec.owner = BREAKPOINT_OWNER;
    // DeZog's own: removing it (or clearing a temporary at a user breakpoint's address) leaves the others
    spec.shared = false;
    spec.condition = condition;
    // Wire format is bank+1 (0: none)
    if (bank != 0 && !physicalForBank(static_cast<uint8_t>(bank - 1), addr, spec))
    {
        std::cout << "[DZRP] Breakpoint at bank " << (bank - 1) << " address " << addr << " refused: no such bank\n";
        return 0;
    }
    std::string error;
    const uint16_t id = bpManager->AddBreakpoint(spec, error);
    if (id == BRK_INVALID && !condition.empty())
        std::cout << "[DZRP] Breakpoint condition '" << condition << "' refused: " << error << "\n";

    if (id == BRK_INVALID)
        return 0;

    if (temporary)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _temporaryBreakpoints.insert(id);
        if (_temporaryBreakpoints.size() > _maxTemporaryBreakpoints)
            _maxTemporaryBreakpoints = _temporaryBreakpoints.size();
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

size_t DezogDebugAdapter::getMaxTemporaryBreakpointCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _maxTemporaryBreakpoints;
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

    // One range breakpoint of DeZog's own (shared = false: an equal CLI / GUI watchpoint keeps its id). On a bank:
    // the part in the start's slot watches that bank there; the rest is a plain CPU range, as DeZog itself checks
    // only the start's bank (dzrpremote.ts getWatchpointsByAddress)
    std::vector<uint16_t> ids;
    const uint32_t last = std::min<uint32_t>(static_cast<uint32_t>(addr) + size - 1, 0xFFFF);
    auto add = [&](uint32_t from, uint32_t to, bool banked) -> bool {
        BreakpointSpec spec;
        spec.access = memoryType;
        spec.address = static_cast<uint16_t>(from);
        spec.hasEnd = to != from;
        spec.addressEnd = static_cast<uint16_t>(to);
        spec.owner = BREAKPOINT_OWNER;
        spec.shared = false;
        if (banked && !physicalForBank(static_cast<uint8_t>(bank - 1), spec.address, spec))
            return false;
        std::string error;
        const uint16_t id = bpManager->AddBreakpoint(spec, error);
        if (id == BRK_INVALID)
            return false;
        ids.push_back(id);
        return true;
    };
    bool ok;
    if (bank == 0)
        ok = add(addr, last, false);
    else
    {
        const unsigned slotShift = getMachineType() == dzrp::MachineType::ZXNEXT ? 13 : 14;
        const uint32_t slotLast = (static_cast<uint32_t>(addr) | ((1u << slotShift) - 1));
        ok = add(addr, std::min(last, slotLast), true);
        if (ok && last > slotLast)
            ok = add(slotLast + 1, last, false);
    }
    if (!ok)
    {
        for (uint16_t id : ids)
            bpManager->RemoveBreakpointByID(id);
        return false;
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
        std::cerr << "[DZRP] captureSnapshotBytes: " << emulator.LastSaveResult().text << "\n";
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
    coordinator::SourceScope source(coordinator::OpSource::DeZog);   // the operation log names who loaded it
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

    // A snapshot load ends the TTD session (the one rule) - start a fresh history from here
    ensureHistoryRecording(*emulator);
}

namespace
{
std::string lowerExtension(const std::string& path)
{
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.')
        ext.erase(0, 1);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

/// The format of a snapshot image by its content (a file whose extension says nothing: DeZog's ".zsf")
std::string detectSnapshotExtension(const std::vector<uint8_t>& bytes)
{
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "ZXST", 4) == 0)
        return "szx";
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "Next", 4) == 0)
        return "nex";
    constexpr size_t kSna48 = 49179, kSna128 = 131103, kSna128Extra = 147487;
    if (bytes.size() == kSna48 || bytes.size() == kSna128 || bytes.size() == kSna128Extra)
        return "sna";
    return "z80";
}
}  // namespace

std::string DezogDebugAdapter::saveSnapshotFile(const std::string& path)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return "no emulator";
    if (path.empty())
        return "No file name";
    leaveHistory(*emulator);

    // A format this emulator writes: by the extension, as ZEsarUX does
    if (snapshot::SaveFormatFromExtension(lowerExtension(path)))
        return emulator->SaveSnapshot(path) ? std::string() : emulator->LastSaveResult().text;

    // Another extension (DeZog's state save names the file .zsf, ZEsarUX's own format): the fullest format the machine
    // can be saved in now, under that name; snapshot-load reads it back by its content
    const snapshot::SaveFormats formats = emulator->SnapshotSaveFormats();
    for (snapshot::SaveFormat format :
         {snapshot::SaveFormat::Szx, snapshot::SaveFormat::Z80, snapshot::SaveFormat::Nex, snapshot::SaveFormat::Sna})
    {
        if (!formats.Has(format) || !formats.For(format).available)
            continue;
        std::filesystem::path temp = makeSnapshotTempPath();
        temp.replace_extension(snapshot::ToText(format));
        std::string error;
        if (!emulator->SaveSnapshot(temp.string()))
            error = emulator->LastSaveResult().text;
        else
        {
            std::error_code ec;
            std::filesystem::copy_file(temp, path, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
                error = "can not write " + path + ": " + ec.message();
        }
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        return error;
    }
    return "no snapshot format available for this machine now";
}

std::string DezogDebugAdapter::loadSnapshotFile(const std::string& path)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return "no emulator";
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return "can not open " + path;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    leaveHistory(*emulator);

    const std::string ext = lowerExtension(path);
    const auto& known = Emulator::SupportedSnapshotExtensions();
    const bool byName = std::find(known.begin(), known.end(), ext) != known.end();
    coordinator::SourceScope source(coordinator::OpSource::DeZog);
    const bool ok = byName ? emulator->LoadSnapshot(path)
                           : emulator->LoadSnapshotData(bytes, detectSnapshotExtension(bytes), path);
    // A snapshot load ends the TTD session - start a fresh history from here
    ensureHistoryRecording(*emulator);
    return ok ? std::string() : "loading " + path + " failed";
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
        case MM_NEXT:
            return dzrp::MachineType::ZXNEXT;
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
    // The machine's decoder, as a CPU IN would read it (no breakpoint fires, the clock stays)
    auto emulator = resolveEmulator();
    uint8_t value = 0xFF;
    if (emulator)
        PortWrite::Read(emulator.get(), port, value, BREAKPOINT_OWNER);
    return value;
}

void DezogDebugAdapter::writePort(uint16_t port, uint8_t value)
{
    // The machine's decoder (paging, AY, the Next's sprite ports ...), as a tool edit
    auto emulator = resolveEmulator();
    if (!emulator)
        return;
    leaveHistory(*emulator);
    PortWrite::Write(emulator.get(), port, value, BREAKPOINT_OWNER);
}

void DezogDebugAdapter::setInterrupts(bool enabled)
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return;
    leaveHistory(*emulator);
    Z80State* z80 = emulator->GetZ80State();
    if (!z80)
        return;
    editMachine(*emulator, "dezog interrupts", [&]() { z80->iff1 = z80->iff2 = enabled ? 1 : 0; });
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
ttd::TTDSessionRef ttdManagerOf(Emulator& emulator)
{
    return ttd::TTDSessionRef(emulator.GetContext());
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
    // The session holds the emulator's run control while DeZog is connected: the user sees it at the GUI
    if (EmulatorContext* context = emulator->GetContext())
    {
        const unreal::UUID owner = unreal::UUID::Generate();
        std::string reason;
        std::string peer;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            peer = _peer;
        }
        if (context->TakeRunControl(owner, "dezog", &reason, _sessionClientName, std::chrono::milliseconds(0), peer))
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _sessionOwner = owner;
        }
        else
            std::cout << "[DZRP] Run control not taken: " << reason << "\n";
    }

    if (!emulator->IsPaused())
        emulator->Pause(sessionRequester(), "dezog attach");

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

    // The debugger's history ends: a background recording it started stops,
    // the timeline is kept for the scrubber/.ttd flows (§6.3.1)
    if (ttd::TTDSessionRef mgr = ttdManagerOf(*emulator))
        mgr->ReleaseBackgroundRecording();

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

    // Only a pause DeZog set itself (attach, its pause command) is lifted: a pause the user or another surface asked
    // for stays (execution arbiter, owner-only run)
    // The session's own pause (attach, its pause command) goes with its requester, a breakpoint's with DeZog alone
    // (after the claim is released: a run asked without it would be refused)
    unreal::UUID owner;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        owner = std::exchange(_sessionOwner, unreal::UUID());
    }
    EmulatorContext* context = emulator->GetContext();
    if (emulator->IsPaused() && !owner.isNil() && context && context->HasRunControl(owner))
        emulator->Resume(execution::Requester{execution::Origin::DeZog, owner.toString()}, true);
    if (context && !owner.isNil())
        context->ReleaseRunControl(owner);
    if (emulator->IsPaused())
        emulator->Resume(execution::Requester{execution::Origin::DeZog, {}}, true);
}

void DezogDebugAdapter::onClientConnected(const std::string& peer)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _peer = peer;
}

bool DezogDebugAdapter::controlTakenBack() const
{
    unreal::UUID owner;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        owner = _sessionOwner;
    }
    if (owner.isNil())
        return false;
    auto emulator = resolveEmulator();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    return context && context->IsRunControlRevoked(owner);
}

execution::Requester DezogDebugAdapter::sessionRequester() const
{
    unreal::UUID owner;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        owner = _sessionOwner;
    }
    auto emulator = resolveEmulator();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    if (!owner.isNil() && context && context->HasRunControl(owner))
        return execution::Requester{execution::Origin::DeZog, owner.toString()};
    return execution::Requester{execution::Origin::DeZog, {}};
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
    return emulator && static_cast<bool>(ttdManagerOf(*emulator));
}

bool DezogDebugAdapter::isHistoryRecording() const
{
    auto emulator = resolveEmulator();
    if (!emulator)
        return false;
    ttd::TTDSessionRef mgr = ttdManagerOf(*emulator);
    return mgr && mgr->IsRecording();
}

void DezogDebugAdapter::ensureHistoryRecording(Emulator& emulator)
{
    if (!isHistoryEnabled())
        return;

    ttd::TTDSessionRef mgr = ttdManagerOf(emulator);
    if (!mgr)
        return;

    // A debug session uses the recording that runs, or a background one: it
    // survives browse cycles and refuses nothing (an outside change ends its
    // session and the next starts at once). Idempotent - safe while browsing
    if (!mgr->HoldBackgroundRecording())
        std::cerr << "[DZRP] TTD history unavailable"
                  << (mgr->GetUnavailableReason().empty() ? "" : ": " + mgr->GetUnavailableReason()) << "\n";
}

void DezogDebugAdapter::editMachine(Emulator& emulator, const char* what, const std::function<void()>& edit)
{
    // The engine records the edit as an event carrying what it changed (memory,
    // registers, paging latches): replay reproduces it and the history before
    // it stays (D9). The marker, the bytes and the debugger snapshot's seq are
    // Emulator::EditMemoryFromTool's
    if (emulator.GetContext() && emulator.GetContext()->pTimeTravelController)
    {
        // The callers leave the history first (leaveHistory): a refusal (the recorded past) is not expected here
        if (const std::string refused = emulator.EditMemoryFromTool(what, edit); !refused.empty())
            std::cerr << "[DZRP] " << what << " refused: " << refused << "\n";
        return;
    }

    // v1 - its replay cannot cross the edit: restart recording at the edited state
    emulator.NoteDebugChange();   // the debugger snapshot's seq
    edit();
    if (!isHistoryEnabled())
        return;

    ttd::TTDSessionRef mgr = ttdManagerOf(emulator);
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

    ttd::TTDSessionRef mgr = ttdManagerOf(emulator);

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
    ttd::TTDSessionRef mgr = ttdManagerOf(emulator);
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
    // (TimeTravelController::GetFrameCache) — O(1) after a one-time ~ms frame fill.
    // See docs/inprogress/2026-08-27-dezog-integration/reverse-debugging.md §5
    // ("Best strategy for DeZog"). To revert to seek-per-read, replace the
    // resolveHistoryIndex + GetFrameCache block with a SeekTo per index (doc §3).
    if (!isHistoryEnabled())
        return std::nullopt;

    auto emulator = resolveEmulator();
    if (!emulator || !emulator->IsPaused())
        return std::nullopt;

    ttd::TTDSessionRef mgr = ttdManagerOf(*emulator);
    if (!mgr || mgr->GetCheckpointCount() == 0)
        return std::nullopt;

    // Entering history: remember the present TimePoint (partial-frame
    // counting in resolveHistoryIndex). Browse is read-only - recording never
    // stops; GetFrameCache builds under the paused exemption. No snapshot, no
    // restore (§6.3.2).
    if (getHistoryCursor() < 0)
    {
        if (!mgr->IsRecording())
            return std::nullopt;  // history requires a recording
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
    // The TTD cache stores raw slots (slot 0 = the physical ROM page); clients get the
    // same dzrp encoding getSlots() returns (bankForRomPage for slot 0),
    // so history entries decode identically to live register responses.
    Memory* memory = emulator->GetMemory();
    if (getMachineType() == dzrp::MachineType::ZX48K)
        entry.slots = {0, 1};
    else
        entry.slots = {memory ? bankForRomPage(*memory, e.bankSlots[0]) : ROM_BANK_BASE,
                       e.bankSlots[1], e.bankSlots[2], e.bankSlots[3]};
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
