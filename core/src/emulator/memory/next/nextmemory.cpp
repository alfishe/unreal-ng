#include "stdafx.h"

#include "nextmemory.h"

#include "debugger/ttd/ttddirtytracker.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/ulacontention.h"
#include "common/filehelper.h"
#include "common/modulelogger.h"

#include <cstring>

NextMemory::NextMemory(EmulatorContext* context) : Memory(context)
{
    _fastIf = std::make_unique<MemoryInterface>(&Memory::MemoryReadFast, static_cast<MemoryWriteCallback>(&NextMemory::SlotWriteFast));
    _debugIf = std::make_unique<MemoryInterface>(&Memory::MemoryReadDebug, static_cast<MemoryWriteCallback>(&NextMemory::SlotWriteDebug));
    _fastContendedIf = std::make_unique<MemoryInterface>(static_cast<MemoryReadCallback>(&NextMemory::SlotReadContendedFast),
                                                         static_cast<MemoryWriteCallback>(&NextMemory::SlotWriteContendedFast));
    _debugContendedIf = std::make_unique<MemoryInterface>(static_cast<MemoryReadCallback>(&NextMemory::SlotReadContendedDebug),
                                                          static_cast<MemoryWriteCallback>(&NextMemory::SlotWriteContendedDebug));
    _toolReadRedirect = true;  // DirectReadFromZ80Memory asks the slot table
    ResetMmu();
}

MemoryInterface* NextMemory::ModelMemoryInterface(bool debug, bool contended)
{
    if (contended || _sramWait28)  // the 28 MHz read waits ride on the contended accesses
        return debug ? _debugContendedIf.get() : _fastContendedIf.get();
    return debug ? _debugIf.get() : _fastIf.get();
}

/// region <Access>

// The contended accesses: the wait of the video logic first, then the plain access (memorycontended.cpp has the
// 16K-window version). No snow: the FPGA's ULA does not have it
uint8_t NextMemory::SlotReadContendedFast(uint16_t addr, bool isExecution)
{
    if (_slotContended[addr >> 13])
    {
        _contentionCpu->InsertWaitStates(_contentionUla->DelayAt(_contentionCpu->AccessStartT()));
        const uint8_t value = MemoryReadFast(addr, isExecution);
        _contentionUla->LatchContendedByte(value);
        return value;
    }
    if (_sramWait28 && _slotSramWait[addr >> 13])
        _contentionCpu->InsertWaitStates(1);
    return MemoryReadFast(addr, isExecution);
}

uint8_t NextMemory::SlotReadContendedDebug(uint16_t addr, bool isExecution)
{
    if (_slotContended[addr >> 13])
    {
        const uint8_t wait = _contentionUla->DelayAt(_contentionCpu->AccessStartT());
        _contentionCpu->InsertWaitStates(wait);
        _contentionUla->CountAccess(isExecution ? CONTENTION_FETCH : CONTENTION_READ, wait);
        const uint8_t value = MemoryReadDebug(addr, isExecution);
        _contentionUla->LatchContendedByte(value);
        return value;
    }
    if (_sramWait28 && _slotSramWait[addr >> 13])
        _contentionCpu->InsertWaitStates(1);
    return MemoryReadDebug(addr, isExecution);
}

void NextMemory::SlotWriteContendedFast(uint16_t addr, uint8_t value)
{
    if (_slotContended[addr >> 13])
    {
        _contentionCpu->InsertWaitStates(_contentionUla->DelayAt(_contentionCpu->AccessStartT()));
        _contentionUla->LatchContendedByte(value);
    }
    SlotWriteFast(addr, value);
}

void NextMemory::SlotWriteContendedDebug(uint16_t addr, uint8_t value)
{
    if (_slotContended[addr >> 13])
    {
        const uint8_t wait = _contentionUla->DelayAt(_contentionCpu->AccessStartT());
        _contentionCpu->InsertWaitStates(wait);
        _contentionUla->CountAccess(CONTENTION_WRITE, wait);
        _contentionUla->LatchContendedByte(value);
    }
    SlotWriteDebug(addr, value);
}

uint8_t NextMemory::MemoryReadFast(uint16_t addr, [[maybe_unused]] bool isExecution)
{
    return _memory[_readOff[addr >> 13] + (addr & (kSlotSize - 1))];
}

uint8_t NextMemory::MemoryReadDebug(uint16_t addr, bool isExecution)
{
    const unsigned slot = addr >> 13;
    const uint8_t result = _memory[_readOff[slot] + (addr & (kSlotSize - 1))];
    ReadDebugEffects(addr, result, isExecution, _physPage[slot]);
    return result;
}

void NextMemory::SlotWriteFast(uint16_t addr, uint8_t value)
{
    _memory[_writeOff[addr >> 13] + (addr & (kSlotSize - 1))] = value;
}

void NextMemory::SlotWriteDebug(uint16_t addr, uint8_t value)
{
    const unsigned slot = addr >> 13;
    _memory[_writeOff[slot] + (addr & (kSlotSize - 1))] = value;
    WriteDebugEffects(addr, value, _physPage[slot]);
}

uint8_t NextMemory::ToolReadRedirect(uint16_t addr, [[maybe_unused]] uint8_t normal) const
{
    return PeekSlot(addr);
}

uint8_t NextMemory::PeekSlot(uint16_t addr) const
{
    return _memory[_readOff[addr >> 13] + (addr & (kSlotSize - 1))];
}

void NextMemory::PokeSlot(uint16_t addr, uint8_t value)
{
    const unsigned slot = addr >> 13;
    // A tool's poke is not the CPU's write: it reaches ROM too (as DirectWrite does)
    _memory[_readOff[slot] + (addr & (kSlotSize - 1))] = value;
    if (_feature_ttd_enabled && _ttdDirtyTracker != nullptr && _physPage[slot] != ttd::kPhysPageNone)
        _ttdDirtyTracker->MarkDirty(_physPage[slot]);
    if (addr >= 0x4000 && addr <= 0x5B00)
        _state->video_memory_changed = true;
}

/// endregion

/// region <Config mode and the boot ROM>

void NextMemory::SetConfigMode(bool on)
{
    _configMode = on;
    Remap();
}

void NextMemory::SetBootRomEnabled(bool on)
{
    _bootRom = on;
    Remap();
}

void NextMemory::SetConfigBank(uint8_t bank)
{
    _cfgBank = bank & 0x3F;
    Remap();
}

void NextMemory::Remap()
{
    for (unsigned s = 0; s < kSlots; s++)
        MapSlot(s);
    SyncWindows();
}

const uint8_t* NextMemory::DivMmcRamBank(unsigned bank)
{
    // SRAM #020000 + bank x 8K: system area pages 8-15, two banks to a 16K page
    return ROMPageHostAddress(static_cast<uint8_t>(8 + ((bank & 15) >> 1))) + ((bank & 1) ? kSlotSize : 0);
}

void NextMemory::SetLayer2View(const Layer2View& view)
{
    if (view.read == _l2View.read && view.write == _l2View.write && view.segment == _l2View.segment && view.bank == _l2View.bank &&
        view.offset == _l2View.offset)
        return;
    _l2View = view;
    Remap();
}

void NextMemory::SetDivMmcView(const DivMmcView& view)
{
    if (view.mapped == _divView.mapped && view.bank3AtZero == _divView.bank3AtZero && view.bank == _divView.bank)
        return;
    _divView = view;
    Remap();
}

bool NextMemory::Layer2ReadsAt(uint16_t address) const
{
    if (!_l2View.read)
        return false;
    const unsigned slot = address >> 13;
    return slot < 2 || (_l2View.segment == 3 && slot < 6);
}

void NextMemory::SetMultifaceActive(bool active)
{
    if (active == _mfActive)
        return;
    _mfActive = active;
    Remap();
}

uint8_t NextMemory::EffectiveRom() const
{
    const bool lock1 = (_alt & 0x20) != 0;
    const bool lock0 = (_alt & 0x10) != 0;
    switch (_machineType)
    {
        case 1:
            return 0;
        case 3:
            return (lock1 || lock0) ? static_cast<uint8_t>((lock1 << 1) | lock0) : _rom;
        default:
            return (lock1 || lock0) ? static_cast<uint8_t>(lock1) : static_cast<uint8_t>(_rom & 1);
    }
}

/// Alt ROM 0 (128K) is the 16K page 6 of the system area, alt ROM 1 (48K) page 7
uint8_t NextMemory::AltPage() const
{
    const bool lock1 = (_alt & 0x20) != 0;
    const bool lock0 = (_alt & 0x10) != 0;
    bool alt48;
    if (_machineType == 1)
        alt48 = !(!lock1 && lock0);
    else
        alt48 = (lock1 || lock0) ? lock1 : ((_rom & 1) != 0);
    return alt48 ? 7 : 6;
}

bool NextMemory::BasicRomVisible() const
{
    if ((_alt & 0xC0) == 0x80)
        return AltPage() == 7;
    const uint8_t rom = EffectiveRom();
    switch (_machineType)
    {
        case 1: return rom == 0;
        case 3: return rom == 3;
        default: return rom == 1;
    }
}

void NextMemory::SetMachineType(uint8_t type)
{
    if (type == _machineType)
        return;
    _machineType = type;
    Remap();
}

void NextMemory::SetAltRomRegister(uint8_t value)
{
    _alt = value;
    Remap();
}

void NextMemory::SetContentionRule(uint8_t timing)
{
    _contentionRule = timing;
    ApplyContentionFlags();
}

void NextMemory::RefreshSlotContention()
{
    ApplyContentionFlags();
}

/// The slot flags from the MMU values and the rule; the 16K flags of the ULA component (port contention reads the
/// high byte of the address through them) are the OR of the halves
void NextMemory::ApplyContentionFlags()
{
    for (unsigned s = 0; s < kSlots; s++)
    {
        const uint8_t v = _mmu[s];
        bool contended = false;
        if (v < 16)  // RAM banks 0-7 only
        {
            const unsigned bank = v >> 1;
            switch (_contentionRule)
            {
                case 1: contended = bank == 5; break;
                case 2: contended = (bank & 1) != 0; break;
                case 3: contended = bank >= 4; break;
                default: break;
            }
        }
        _slotContended[s] = contended;
    }
    if (UlaContention* ula = _context ? _context->pUlaContention : nullptr)
        for (unsigned w = 0; w < 4; w++)
            ula->SetSlotContended(static_cast<uint8_t>(w), _slotContended[w * 2] || _slotContended[w * 2 + 1]);
}

void NextMemory::OnRomLoaded([[maybe_unused]] uint16_t imageBanks)
{
    const std::string path = _context->config.next_boot_rom_path;
    if (path.empty())
        return;
    std::string resolved = FileHelper::NormalizePath(path);
    if (!FileHelper::FileExists(resolved))
        resolved = FileHelper::PathCombine(FileHelper::GetExecutablePath(), resolved);
    if (!FileHelper::FileExists(resolved))
        resolved = FileHelper::PathCombine(FileHelper::GetResourcesPath(), path);
    FILE* file = FileHelper::OpenFile(resolved, "rb");
    if (!file)
    {
        MLOGERROR("NextMemory: boot ROM '%s' not found", path.c_str());
        return;
    }
    uint8_t* page = ROMPageHostAddress(kBootRomPage);
    std::memset(page, 0xFF, PAGE_SIZE);
    const size_t size = std::fread(page, 1, kBootRomSize, file);
    std::fclose(file);
    if (size != kBootRomSize)
    {
        MLOGERROR("NextMemory: boot ROM '%s' is %zu bytes, expected %u", path.c_str(), size, kBootRomSize);
        return;
    }
    // Power-on state of the real board: an empty system area (the firmware loads it), config mode, boot ROM on
    for (unsigned p = 0; p < kSystemAreaPages; p++)
        std::memset(ROMPageHostAddress(static_cast<uint8_t>(p)), 0xFF, PAGE_SIZE);
    _bootRomLoaded = true;
    _configMode = true;
    _bootRom = true;
    Remap();
}

/// endregion

/// region <Slot table>

void NextMemory::MapSlot(unsigned slot)
{
    const uint8_t value = _mmu[slot];
    const uint32_t trash = static_cast<uint32_t>(TRASH_MEMORY_OFFSET);
    const bool bootRomHere = slot == 0 && _configMode && _bootRom && _bootRomLoaded;
    if (slot < 2 && _mfActive && !bootRomHere)
    {
        // Multiface (zxnext.vhd: sram address #014000 + A13, the ROM half read-only, no DivMMC / Layer 2 / MMU override)
        const uint8_t* mem = ROMPageHostAddress(5) + slot * kSlotSize;
        _readOff[slot] = static_cast<uint32_t>(mem - _memory);
        _writeOff[slot] = slot == 1 ? _readOff[slot] : trash;
        _physPage[slot] = ttd::kPhysPageNone;
        _kind[slot] = slot == 0 ? "multiface rom" : "multiface ram";
    }
    else if (slot < 2 && _divView.mapped && !bootRomHere)
    {
        // DivMMC: slot 0 the ROM (or RAM bank 3 with MAPRAM), slot 1 the selected RAM bank
        const uint8_t* mem;
        bool writable = false;
        if (slot == 0)
            mem = _divView.bank3AtZero ? DivMmcRamBank(3) : ROMPageHostAddress(4);
        else
        {
            mem = DivMmcRamBank(_divView.bank);
            writable = !(_divView.bank3AtZero && _divView.bank == 3);
        }
        _readOff[slot] = static_cast<uint32_t>(mem - _memory);
        _writeOff[slot] = writable ? _readOff[slot] : trash;
        _physPage[slot] = ttd::kPhysPageNone;
        _kind[slot] = (slot == 0 && !_divView.bank3AtZero) ? "divmmc rom" : "divmmc ram";
    }
    else if (value == kMmuRom && bootRomHere)
    {
        // the boot ROM: read-only
        _readOff[slot] = static_cast<uint32_t>(ROMPageHostAddress(kBootRomPage) - _memory);
        _writeOff[slot] = trash;
        _physPage[slot] = ttd::kPhysPageNone;
        _kind[slot] = "boot rom";
    }
    else if (value == kMmuRom && slot < 2 && _configMode)
    {
        // config mapping: NR #04's 16K SRAM bank, writable (the firmware loads the ROMs through it)
        const uint8_t* mem = _cfgBank < kSystemAreaPages ? ROMPageHostAddress(_cfgBank) : RAMPageAddress(_cfgBank - kSystemAreaPages);
        _readOff[slot] = _writeOff[slot] = static_cast<uint32_t>(mem + slot * kSlotSize - _memory);
        _physPage[slot] = _cfgBank < kSystemAreaPages ? ttd::kPhysPageNone : static_cast<ttd::PhysPage>(_cfgBank - kSystemAreaPages);
        _kind[slot] = "config bank";
    }
    else if (value == kMmuRom)
    {
        const uint8_t* rom = ROMPageHostAddress(EffectiveRom()) + (slot & 1) * kSlotSize;
        _readOff[slot] = static_cast<uint32_t>(rom - _memory);
        _writeOff[slot] = trash;
        _physPage[slot] = ttd::kPhysPageNone;
        _kind[slot] = "rom";
        if (_alt & 0x80)
        {
            // the alternate ROM (system page 6 / 7): replaces the ROM for reads, or is written through the ROM area
            const uint8_t* alt = ROMPageHostAddress(AltPage()) + (slot & 1) * kSlotSize;
            const uint32_t altOff = static_cast<uint32_t>(alt - _memory);
            if (_alt & 0x40)
            {
                _writeOff[slot] = altOff;
                _kind[slot] = "rom (alt rom written)";
            }
            else
            {
                _readOff[slot] = altOff;
                _kind[slot] = "alt rom";
            }
        }
    }
    else if (value < kRamPages8K && (value >> 1) < MAX_RAM_PAGES)
    {
        const uint8_t* ram = RAMPageAddress(value >> 1) + (value & 1) * kSlotSize;
        _readOff[slot] = _writeOff[slot] = static_cast<uint32_t>(ram - _memory);
        _physPage[slot] = static_cast<ttd::PhysPage>(value >> 1);
        _kind[slot] = "ram";
    }
    else
    {
        _readOff[slot] = _writeOff[slot] = trash;
        _physPage[slot] = ttd::kPhysPageNone;
        _kind[slot] = "unmapped";
    }
    if (!(slot < 2 && (_divView.mapped || _mfActive) && !bootRomHere) && !bootRomHere)
        ApplyLayer2(slot);
    // the 28 MHz read wait: everything but the boot ROM, unmapped slots (sram_pre_active = 0) and the bank 7 BRAM page (#0E)
    const char* kind = _kind[slot];
    _slotSramWait[slot] = std::strcmp(kind, "boot rom") != 0 && std::strcmp(kind, "unmapped") != 0 &&
                          !(std::strcmp(kind, "ram") == 0 && value == 0x0E);
}

/// Layer 2 over the slot the MMU (or the ROM) would show: reads and writes separately
void NextMemory::ApplyLayer2(unsigned slot)
{
    if (!_l2View.read && !_l2View.write)
        return;
    const bool all = _l2View.segment == 3;
    if (!(slot < 2 || (all && slot < 6)))
        return;
    const unsigned bank16 = static_cast<unsigned>(_l2View.bank) + (all ? (slot >> 1) : _l2View.segment) + _l2View.offset;
    if (bank16 >= MAX_RAM_PAGES)
        return;
    const uint32_t off = static_cast<uint32_t>(RAMPageAddress(static_cast<uint16_t>(bank16)) + (slot & 1) * kSlotSize - _memory);
    if (_l2View.read)
    {
        _readOff[slot] = off;
        _kind[slot] = "layer 2";
    }
    if (_l2View.write)
        _writeOff[slot] = off;
}

void NextMemory::SyncWindows()
{
    for (unsigned w = 0; w < 4; w++)
    {
        const unsigned lo = w * 2;
        _bank_read[w] = _memory + _readOff[lo];
        _bank_write[w] = _memory + _writeOff[lo];
        _bank_mode[w] = _mmu[lo] == kMmuRom ? BANK_ROM : BANK_RAM;
        _bank_ram_page_cache[w] = _physPage[lo];
    }
    ApplyContentionFlags();
}

void NextMemory::ResetMmu()
{
    _rom = 0;
    _dffd = 0;
    _allRam = false;
    for (unsigned s = 0; s < kSlots; s++)
    {
        _mmu[s] = kResetMmu[s];
        MapSlot(s);
    }
    SyncWindows();
}

void NextMemory::SetMmu(unsigned slot, uint8_t value)
{
    slot &= 7;
    _mmu[slot] = value;
    MapSlot(slot);
    SyncWindows();
}

void NextMemory::SetRomSelect(uint8_t rom)
{
    _rom = rom & 3;
    for (unsigned s = 0; s < kSlots; s++)
        if (_mmu[s] == kMmuRom)
            MapSlot(s);
    SyncWindows();
}

void NextMemory::SetExtendedBank(uint8_t value)
{
    _dffd = value & 0x0F;
}

void NextMemory::ApplyClassicPaging(uint8_t p7ffd, uint8_t p1ffd, bool changeBank)
{
    // The ports act on the ROM slots (0, 1) and the bank slots (6, 7); the slots in between keep what NR #52-#55 set,
    // except that leaving the all-RAM mode puts them back. In all-RAM mode the ports own all eight slots
    const bool allRam = (p1ffd & 1) != 0;
    _rom = static_cast<uint8_t>((((p1ffd >> 2) & 1) << 1) | ((p7ffd >> 4) & 1));
    if (allRam)
    {
        // 16K banks per window; the 8K pages of bank b are 2b and 2b+1
        static const uint8_t cfg[4][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {4, 5, 6, 3}, {4, 7, 6, 3}};
        const uint8_t* c = cfg[(p1ffd >> 1) & 3];
        for (unsigned w = 0; w < 4; w++)
        {
            _mmu[w * 2] = static_cast<uint8_t>(c[w] * 2);
            _mmu[w * 2 + 1] = static_cast<uint8_t>(c[w] * 2 + 1);
        }
    }
    else
    {
        _mmu[0] = _mmu[1] = kMmuRom;
        if (_allRam)
        {
            for (unsigned s = 2; s < 6; s++)
                _mmu[s] = kResetMmu[s];
        }
        if (changeBank || _allRam)
        {
            const uint8_t bank = static_cast<uint8_t>(((_dffd & 0x0F) << 3) | (p7ffd & 7));
            _mmu[6] = static_cast<uint8_t>(bank * 2);
            _mmu[7] = static_cast<uint8_t>(bank * 2 + 1);
        }
    }
    _allRam = allRam;
    for (unsigned s = 0; s < kSlots; s++)
        MapSlot(s);
    SyncWindows();
}

bool NextMemory::UpdateModelBanks()
{
    // Called by UpdateZ80Banks (snapshot load, TTD restore): the table follows the latches
    EmulatorState& state = _context->emulatorState;
    state.flags &= ~(CF_DOSPORTS | CF_Z80FBUS | CF_LEAVEDOSRAM | CF_LEAVEDOSADR | CF_SETDOSROM);
    ApplyClassicPaging(state.p7FFD, state.p1FFD);
    return true;
}

/// endregion
